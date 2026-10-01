#include "formats/decode/stream.h"
#include "formats/decode/metadata.h"

typedef struct lnd_mp3_stream {
    lnd_decode_tags tags;
    mp3dec_t decoder;
    float pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    uint64_t skip_bytes;
    uint64_t position;
    uint64_t length;
    int delay;
    bool first;
} lnd_mp3_stream;

static void *lnd_mp3_stream_create(void) {
    lnd_mp3_stream *s = lnd_alloc_zero(sizeof *s);
    if (s) {
        mp3dec_init(&s->decoder);
        s->first = true;
    }
    return s;
}

static int32_t lnd_mp3_stream_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    lnd_mp3_stream *s = state;
    *pcm = (LND_PCM){0};
    if (s->skip_bytes) {
        *used = (size_t)LND_MIN(bytes, s->skip_bytes);
        lnd_decode_tags_append(&s->tags, data, *used);
        s->skip_bytes -= *used;
        return !*used && end ? LND_ERR_FORMAT : LND_SOURCE_READY;
    }
    if (!bytes) return end ? LND_SOURCE_EOF : LND_SOURCE_WAITING;
    if (bytes >= 3 && !memcmp(data, "TAG", 3)) {
        if (bytes < 128) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        *used = 128;
        return LND_SOURCE_READY;
    }
    if (bytes >= 3 && !memcmp(data, "ID3", 3)) {
        if (bytes < 10) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        if ((data[6] | data[7] | data[8] | data[9]) & 0x80) return LND_ERR_FORMAT;
        s->skip_bytes = ((uint32_t)data[6] << 21 | (uint32_t)data[7] << 14 | (uint32_t)data[8] << 7 | data[9]) + ((data[5] & 0x10) ? 10 : 0);
        lnd_decode_tags_start(&s->tags, (size_t)s->skip_bytes + 10, LND_METADATA_ID3V2);
        lnd_decode_tags_append(&s->tags, data, 10);
        *used = 10;
        return LND_SOURCE_READY;
    }
    if (bytes < 4) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
    if (!hdr_valid(data)) return LND_ERR_FORMAT;
    int size = hdr_frame_bytes(data, 0) + hdr_padding(data);
    if (size <= 4) return LND_ERR_UNSUPPORTED;
    if (bytes < (size_t)size) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
    if (s->first) {
        s->first = false;
        uint32_t frames = 0;
        int delay = 0, padding = 0;
        if (mp3dec_check_vbrtag(data, size, &frames, &delay, &padding) > 0) {
            s->delay = delay;
            uint64_t total = (uint64_t)frames * hdr_frame_samples(data);
            uint64_t trim = (uint64_t)LND_MAX(delay, 0) + (uint64_t)LND_MAX(padding, 0);
            s->length = total > trim ? total - trim : 0;
            *used = (size_t)size;
            return LND_SOURCE_READY;
        }
    }
    mp3dec_frame_info_t frame = {0};
    int got = mp3dec_decode_frame(&s->decoder, data, size, s->pcm, &frame);
    if (frame.frame_bytes != size || got < 0 || frame.channels < 1 || frame.hz < 1) return LND_ERR_FORMAT;
    *used = (size_t)size;
    info->format = LND_FORMAT_F32;
    info->channels = (uint32_t)frame.channels;
    info->sample_rate_hz = (uint32_t)frame.hz;
    info->length_frames = s->length;
    size_t skip = LND_MIN((size_t)LND_MAX(s->delay, 0), (size_t)got);
    s->delay -= (int)skip;
    size_t take = (size_t)got - skip;
    if (s->length) take = (size_t)LND_MIN(take, s->length - LND_MIN(s->position, s->length));
    *pcm = (LND_PCM){
        .data = s->pcm + skip * info->channels, .frames = take, .channels = info->channels, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_INTERLEAVED};
    s->position += take;
    return LND_SOURCE_READY;
}

static void lnd_mp3_stream_close(void *state) {
    lnd_decode_tags_clear(&((lnd_mp3_stream *)state)->tags);
    lnd_free(state);
}

static int32_t lnd_mp3_stream_metadata(void *state, LND_METADATA *m, uint64_t *revision) {
    return lnd_decode_tags_get(&((lnd_mp3_stream *)state)->tags, m, revision);
}

static const LND_CODEC_STREAM lnd_mp3_stream_ops = {
    .metadata = lnd_mp3_stream_metadata, .create = lnd_mp3_stream_create, .step = lnd_mp3_stream_step, .close = lnd_mp3_stream_close};
