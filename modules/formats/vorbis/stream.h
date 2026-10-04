#include "formats/decode/stream.h"
#include "formats/decode/metadata.h"
#include "formats/ogg/stream.h"

typedef struct lnd_vorbis_stream {
    lnd_decode_tags tags;
    lnd_ogg_stream ogg;
    vorbis_info info;
    vorbis_comment comment;
    vorbis_dsp_state dsp;
    vorbis_block block;
    uint32_t headers;
    uint64_t total;
    bool initialized;
    float *pcm;
} lnd_vorbis_stream;

static void *lnd_vorbis_stream_create(void) {
    lnd_vorbis_stream *s = lnd_alloc_zero(sizeof *s);
    if (s) {
        lnd_ogg_stream_init(&s->ogg);
        vorbis_info_init(&s->info);
        vorbis_comment_init(&s->comment);
    }
    return s;
}

static int32_t lnd_vorbis_stream_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    lnd_vorbis_stream *s = state;
    *pcm = (LND_PCM){0};
    if (s->initialized) {
        float **planes;
        int available = vorbis_synthesis_pcmout(&s->dsp, &planes);
        if (available > 0) {
            size_t take = LND_MIN((size_t)available, (size_t)4096);
            for (size_t f = 0; f < take; f++)
                for (uint32_t c = 0; c < info->channels; c++)
                    s->pcm[f * info->channels + c] = planes[c][f];
            vorbis_synthesis_read(&s->dsp, (int)take);
            *pcm = (LND_PCM){.data = s->pcm, .frames = take, .channels = info->channels, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_INTERLEAVED};
            s->total += take;
            return LND_SOURCE_READY;
        }
    }
    ogg_packet packet;
    int result = s->ogg.opened ? ogg_stream_packetout(&s->ogg.stream, &packet) : 0;
    if (result < 0) return LND_ERR_FORMAT;
    if (!result) {
        int32_t r = lnd_ogg_stream_feed(&s->ogg, data, bytes, end, used);
        if (r == LND_SOURCE_EOF) {
            info->length_frames = s->total;
            info->length_known = true;
        }
        if (s->ogg.chain) {
            if (s->initialized) {
                vorbis_block_clear(&s->block);
                vorbis_dsp_clear(&s->dsp);
                s->initialized = false;
            }
            vorbis_comment_clear(&s->comment);
            vorbis_info_clear(&s->info);
            vorbis_comment_init(&s->comment);
            vorbis_info_init(&s->info);
            lnd_free(s->pcm);
            s->pcm = nullptr;
            s->headers = 0;
            s->ogg.chain = false;
        }
        return r;
    }
    if (s->headers < 3) {
        if (vorbis_synthesis_headerin(&s->info, &s->comment, &packet)) return LND_ERR_FORMAT;
        if (s->headers == 1) lnd_decode_tags_read(&s->tags, packet.packet, (size_t)packet.bytes, LND_METADATA_VORBIS);
        if (++s->headers == 3) {
            if (s->info.channels < 1 || s->info.channels > LND_MAX_CHANNELS || s->info.rate < 1) return LND_ERR_FORMAT;
            if (vorbis_synthesis_init(&s->dsp, &s->info)) return LND_ERR_FORMAT;
            if (vorbis_block_init(&s->dsp, &s->block)) {
                vorbis_dsp_clear(&s->dsp);
                return LND_ERR_FORMAT;
            }
            s->initialized = true;
            info->channels = (uint32_t)s->info.channels;
            info->sample_rate_hz = (uint32_t)s->info.rate;
            info->format = LND_FORMAT_F32;
            s->pcm = lnd_alloc(4096 * info->channels * sizeof(float));
            if (!s->pcm) return LND_ERR_OUT_OF_MEMORY;
        }
        return LND_SOURCE_READY;
    }
    if (vorbis_synthesis(&s->block, &packet) || vorbis_synthesis_blockin(&s->dsp, &s->block)) return LND_ERR_FORMAT;
    return LND_SOURCE_READY;
}

static void lnd_vorbis_stream_close(void *state) {
    lnd_vorbis_stream *s = state;
    if (s->initialized) {
        vorbis_block_clear(&s->block);
        vorbis_dsp_clear(&s->dsp);
    }
    vorbis_comment_clear(&s->comment);
    vorbis_info_clear(&s->info);
    lnd_ogg_stream_clear(&s->ogg);
    lnd_free(s->pcm);
    lnd_decode_tags_clear(&s->tags);
    lnd_free(s);
}

static int32_t lnd_vorbis_stream_metadata(void *state, LND_METADATA *m, uint64_t *revision) {
    return lnd_decode_tags_get(&((lnd_vorbis_stream *)state)->tags, m, revision);
}

static const LND_CODEC_STREAM lnd_vorbis_stream_ops = {
    .metadata = lnd_vorbis_stream_metadata, .create = lnd_vorbis_stream_create, .step = lnd_vorbis_stream_step, .close = lnd_vorbis_stream_close};
