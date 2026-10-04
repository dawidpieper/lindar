#include "formats/decode/stream.h"
#include "formats/decode/metadata.h"
#include "formats/ogg/stream.h"

typedef struct lnd_opus_stream {
    lnd_decode_tags tags;
    lnd_ogg_stream ogg;
    OpusMSDecoder *decoder;
    OpusHead head;
    void *pcm;
    uint64_t decoded;
    uint64_t origin;
    uint64_t total;
    uint32_t headers;
    bool origin_known;
    bool raw;
} lnd_opus_stream;

static void *lnd_opus_stream_create(void) {
    lnd_opus_stream *s = lnd_alloc_zero(sizeof *s);
    if (s) lnd_ogg_stream_init(&s->ogg);
    return s;
}

static int32_t lnd_opus_stream_init(lnd_opus_stream *s, const uint8_t *head, size_t bytes) {
    if (opus_head_parse(&s->head, head, bytes) || s->head.channel_count < 1 || s->head.channel_count > LND_MAX_CHANNELS || s->head.mapping_family > 1)
        return LND_ERR_UNSUPPORTED;
    int error;
    s->decoder = opus_multistream_decoder_create(48000, s->head.channel_count, s->head.stream_count, s->head.coupled_count, s->head.mapping, &error);
    if (!s->decoder) return error == OPUS_ALLOC_FAIL ? LND_ERR_OUT_OF_MEMORY : LND_ERR_FORMAT;
    opus_multistream_decoder_ctl(s->decoder, OPUS_SET_GAIN(s->head.output_gain));
    s->pcm = lnd_alloc((size_t)5760 * s->head.channel_count * (LND_OPUS_INTEGER ? sizeof(int16_t) : sizeof(float)));
    return s->pcm ? LND_OK : LND_ERR_OUT_OF_MEMORY;
}

static int32_t lnd_opus_stream_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    lnd_opus_stream *s = state;
    *pcm = (LND_PCM){0};
    ogg_packet packet = {0};
    uint32_t declared = 0;
    if (s->raw) {
        if (!bytes && end) {
            info->length_frames = s->total;
            info->length_known = true;
            return LND_SOURCE_EOF;
        }
        if (bytes < 8) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        uint32_t length = (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 | data[3];
        declared = (uint32_t)data[4] << 24 | (uint32_t)data[5] << 16 | (uint32_t)data[6] << 8 | data[7];
        if (!length || length > 1275u * 48 * 255 || declared > 5760) return LND_ERR_FORMAT;
        if (length > bytes - 8) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        packet.packet = (uint8_t *)data + 8;
        packet.bytes = length;
        *used = (size_t)length + 8;
    } else {
        int result = s->ogg.opened ? ogg_stream_packetout(&s->ogg.stream, &packet) : 0;
        if (result < 0) return LND_ERR_FORMAT;
        if (!result) {
            int32_t r = lnd_ogg_stream_feed(&s->ogg, data, bytes, end, used);
            if (r == LND_SOURCE_EOF) {
                info->length_frames = s->total;
                info->length_known = true;
            }
            if (s->ogg.chain) {
                if (s->decoder) opus_multistream_decoder_destroy(s->decoder);
                s->decoder = nullptr;
                lnd_free(s->pcm);
                s->pcm = nullptr;
                s->headers = 0;
                s->decoded = s->origin = 0;
                s->origin_known = false;
                s->ogg.chain = false;
            }
            return r;
        }
        if (!s->headers) {
            int32_t r = lnd_opus_stream_init(s, packet.packet, (size_t)packet.bytes);
            if (r != LND_OK) return r;
            s->headers++;
        } else if (s->headers == 1) {
            if (packet.bytes < 8 || memcmp(packet.packet, "OpusTags", 8)) return LND_ERR_FORMAT;
            lnd_decode_tags_read(&s->tags, packet.packet, (size_t)packet.bytes, LND_METADATA_OPUS);
            s->headers++;
            return LND_SOURCE_READY;
        }
    }
    info->format = LND_OPUS_INTEGER ? LND_FORMAT_S16 : LND_FORMAT_F32;
    info->channels = (uint32_t)s->head.channel_count;
    info->sample_rate_hz = 48000;
    if (s->headers == 1) return LND_SOURCE_READY;
#if LND_OPUS_INTEGER
    int got = opus_multistream_decode(s->decoder, packet.packet, packet.bytes, s->pcm, 5760, 0);
#else
    int got = opus_multistream_decode_float(s->decoder, packet.packet, packet.bytes, s->pcm, 5760, 0);
#endif
    if (got < 0 || (declared && declared > (uint32_t)got)) return LND_ERR_FORMAT;
    uint64_t first = s->decoded;
    s->decoded += (uint32_t)got;
    size_t skip = (size_t)LND_MIN((uint64_t)got, first < s->head.pre_skip ? s->head.pre_skip - first : 0);
    size_t limit = s->raw && declared ? declared : (size_t)got;
    size_t take = limit - LND_MIN(skip, limit);
    if (!s->raw && !s->origin_known && packet.granulepos >= 0) {
        s->origin = (uint64_t)packet.granulepos > s->decoded ? (uint64_t)packet.granulepos - s->decoded : 0;
        s->origin_known = true;
    }
    if (packet.e_o_s && packet.granulepos >= 0) {
        uint64_t position = first + skip;
        uint64_t end = (uint64_t)packet.granulepos >= s->origin ? (uint64_t)packet.granulepos - s->origin : 0;
        take = (size_t)LND_MIN(take, end > position ? end - position : 0);
    }
    *pcm = (LND_PCM){.data = (uint8_t *)s->pcm + skip * info->channels * LND_PcmGetSampleBytes(info->format),
                     .frames = take,
                     .channels = info->channels,
                     .format = info->format,
                     .layout = LND_LAYOUT_INTERLEAVED};
    s->total += take;
    return LND_SOURCE_READY;
}

static int32_t lnd_opus_stream_configure(void *state, const LND_CODEC_STREAM_CONFIG *config) {
    lnd_opus_stream *s = state;
    const uint8_t *p = config->data;
    if (config->bytes < 11 || p[0] != 0 || !p[1] || p[1] > LND_MAX_CHANNELS || p[10] > 1) return LND_ERR_UNSUPPORTED;
    size_t extra = p[10] ? (size_t)p[1] + 2 : 0;
    if (config->bytes != 11 + extra) return LND_ERR_FORMAT;
    uint8_t head[19 + 2 + LND_MAX_CHANNELS] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1};
    head[9] = p[1];
    head[10] = p[3];
    head[11] = p[2];
    for (unsigned i = 0; i < 4; i++)
        head[12 + i] = p[7 - i];
    head[16] = p[9];
    head[17] = p[8];
    head[18] = p[10];
    if (extra) memcpy(head + 19, p + 11, extra);
    if (config->trim_known) head[10] = head[11] = 0;
    int32_t r = lnd_opus_stream_init(s, head, 19 + extra);
    if (r == LND_OK) {
        s->raw = true;
        s->headers = 2;
    }
    return r;
}

static void lnd_opus_stream_close(void *state) {
    lnd_opus_stream *s = state;
    if (s->decoder) opus_multistream_decoder_destroy(s->decoder);
    lnd_ogg_stream_clear(&s->ogg);
    lnd_free(s->pcm);
    lnd_decode_tags_clear(&s->tags);
    lnd_free(s);
}

static int32_t lnd_opus_stream_metadata(void *state, LND_METADATA *m, uint64_t *revision) {
    return lnd_decode_tags_get(&((lnd_opus_stream *)state)->tags, m, revision);
}

static const LND_CODEC_STREAM lnd_opus_stream_ops = {.metadata = lnd_opus_stream_metadata,
                                                     .configure = lnd_opus_stream_configure,
                                                     .create = lnd_opus_stream_create,
                                                     .step = lnd_opus_stream_step,
                                                     .close = lnd_opus_stream_close};
