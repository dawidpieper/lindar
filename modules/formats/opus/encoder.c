#include <opus.h>
#include <opus_multistream.h>

#include "src/alloc.h"
#include "src/config.h"
#include "src/platform.h"
#include "formats/ogg/mux.h"
#include "lindar_output.h"
#include "io/output/output.h"
#include "pcm/audio/source.h"
#if LND_MODULE_METADATA_COMMENTS
#include "lindar_metadata_comments.h"
#endif

#include <string.h>
#include <limits.h>

typedef struct lnd_opus_feed {
    lnd_source base;
    const float *data;
    uint64_t frames;
} lnd_opus_feed;

static uint64_t lnd_opus_feed_read(lnd_source *src, float *dst, uint64_t frames) {
    lnd_opus_feed *f = (lnd_opus_feed *)src;
    uint64_t n = LND_MIN(frames, f->frames);
    if (n) memcpy(dst, f->data, (size_t)(n * src->channels) * sizeof(float));
    f->data += n * src->channels;
    f->frames -= n;
    lnd_store_relaxed(&src->pos, lnd_load_relaxed(&src->pos) + n);
    return n;
}

static void lnd_opus_feed_free(lnd_source *src) { (void)src; }

static const lnd_source_vt lnd_opus_feed_vt = {
    .read = lnd_opus_feed_read,
    .free = lnd_opus_feed_free,
};

typedef struct lnd_opus_enc {
    OpusEncoder *enc;
    OpusMSEncoder *ms;
    lnd_ogg_mux ogg;
    uint32_t channels;
    uint32_t enc_rate;
    uint32_t frame_size;
    lnd_opus_feed feed;
    lnd_source *resampler;
    float *rs;
    uint32_t rs_frames;
    float *frame;
    uint32_t filled;
    uint8_t packet[8192];
    uint64_t granule;
    uint64_t valid;
    uint64_t packetno;
    uint32_t preskip;
    bool streaming;
} lnd_opus_enc;

static bool lnd_opus_rate_ok(uint32_t sample_rate_hz) { return sample_rate_hz == 8000 || sample_rate_hz == 12000 || sample_rate_hz == 16000 || sample_rate_hz == 24000 || sample_rate_hz == 48000; }

static int lnd_opus_ctl_int(lnd_opus_enc *s, int request, int value) {
    return s->ms ? opus_multistream_encoder_ctl(s->ms, request, value) : opus_encoder_ctl(s->enc, request, value);
}

static void lnd_opus_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void lnd_opus_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int32_t lnd_opus_emit(lnd_opus_enc *s, bool last) {
    int n = s->ms ? opus_multistream_encode_float(s->ms, s->frame, (int)s->frame_size, s->packet, sizeof s->packet)
                  : opus_encode_float(s->enc, s->frame, (int)s->frame_size, s->packet, sizeof s->packet);
    if (n < 0) return LND_ERR_IO;
    uint64_t step = (uint64_t)s->frame_size * 48000 / s->enc_rate;
    s->granule += step;
    uint64_t final = s->preskip + s->valid * 48000 / s->enc_rate;
    ogg_packet op = {
        .packet = s->packet,
        .bytes = n,
        .b_o_s = 0,
        .e_o_s = last,
        .granulepos = (ogg_int64_t)(last && final < s->granule ? final : s->granule),
        .packetno = (ogg_int64_t)s->packetno++,
    };
    s->filled = 0;
    int32_t r = lnd_ogg_packet(&s->ogg, &op);
    return r == LND_OK ? lnd_ogg_pages(&s->ogg, s->streaming || last) : r;
}

static int32_t lnd_opus_push(lnd_opus_enc *s, const float *pcm, uint64_t frames) {
    uint32_t ch = s->channels;
    s->valid += frames;
    for (uint64_t done = 0; done < frames;) {
        uint32_t n = (uint32_t)LND_MIN(frames - done, (uint64_t)(s->frame_size - s->filled));
        memcpy(s->frame + (size_t)s->filled * ch, pcm + done * ch, (size_t)n * ch * sizeof(float));
        s->filled += n;
        done += n;
        if (s->filled == s->frame_size) {
            int32_t r = lnd_opus_emit(s, false);
            if (r != LND_OK) return r;
        }
    }
    return LND_OK;
}

static int32_t lnd_opus_enc_close(void *state);

static int32_t lnd_opus_enc_open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state) {
    (void)extension;
    uint32_t ch = params->channels;
    if (ch > 8) return LND_ERR_UNSUPPORTED;
    lnd_opus_enc *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->channels = ch;
    s->streaming = (params->flags & LND_OUTPUT_STREAMING) != 0;
    s->enc_rate = lnd_opus_rate_ok(params->sample_rate_hz) ? params->sample_rate_hz : 48000;
    s->frame_size = params->frame_size_frames ? params->frame_size_frames : s->enc_rate / 50;
    uint32_t frame = s->frame_size;
    uint32_t sample_rate_hz = s->enc_rate;
    if (frame != sample_rate_hz / 400 && frame != sample_rate_hz / 200 && frame != sample_rate_hz / 100 && frame != sample_rate_hz / 50 && frame != sample_rate_hz / 25 && frame != sample_rate_hz * 3 / 50) {
        lnd_free(s);
        return LND_ERR_INVALID_ARG;
    }
    s->frame = lnd_alloc((size_t)s->frame_size * ch * sizeof(float));
    if (!s->frame) {
        lnd_opus_enc_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    if (s->enc_rate != params->sample_rate_hz) {
        s->feed.base.vt = &lnd_opus_feed_vt;
        s->feed.base.channels = ch;
        s->feed.base.sample_rate_hz = params->sample_rate_hz;
        s->rs_frames = 4096;
        s->rs = lnd_alloc((size_t)s->rs_frames * ch * sizeof(float));
        s->resampler = lnd_resample_source_create(&s->feed.base, false, 48000, s->rs_frames, lnd_cfg_u32(LND_CFG_AUDIO_RESAMPLE_QUALITY));
        if (!s->rs || !s->resampler) {
            lnd_opus_enc_close(s);
            return LND_ERR_OUT_OF_MEMORY;
        }
        lnd_resample_source_set_live(s->resampler, true);
    }
    int err = OPUS_OK;
    int streams = 1, coupled = ch == 2 ? 1 : 0;
    unsigned char mapping[8] = {0, 1};
    if (ch <= 2) {
        s->enc = opus_encoder_create((opus_int32)s->enc_rate, (int)ch, OPUS_APPLICATION_AUDIO, &err);
    } else {
        s->ms = opus_multistream_surround_encoder_create((opus_int32)s->enc_rate, (int)ch, 1, &streams, &coupled, mapping, OPUS_APPLICATION_AUDIO, &err);
    }
    if (err != OPUS_OK || (!s->enc && !s->ms)) {
        lnd_opus_enc_close(s);
        return LND_ERR_UNSUPPORTED;
    }
    if (params->bitrate_kbps && (params->bitrate_kbps > INT32_MAX / 1000 || lnd_opus_ctl_int(s, OPUS_SET_BITRATE_REQUEST, (int)params->bitrate_kbps * 1000) != OPUS_OK)) {
        lnd_opus_enc_close(s);
        return LND_ERR_INVALID_ARG;
    }
    if (params->mode == LND_ENCODER_MODE_CBR) lnd_opus_ctl_int(s, OPUS_SET_VBR_REQUEST, 0);
    if (params->mode == LND_ENCODER_MODE_ABR) lnd_opus_ctl_int(s, OPUS_SET_VBR_CONSTRAINT_REQUEST, 1);
    lnd_opus_ctl_int(s, OPUS_SET_COMPLEXITY_REQUEST, (int)lnd_encoder_level(params, 10, 10));
    opus_int32 lookahead = 0;
    if (s->ms)
        opus_multistream_encoder_ctl(s->ms, OPUS_GET_LOOKAHEAD(&lookahead));
    else
        opus_encoder_ctl(s->enc, OPUS_GET_LOOKAHEAD(&lookahead));
    s->preskip = (uint32_t)((uint64_t)(lookahead > 0 ? lookahead : 0) * 48000 / s->enc_rate);
    int32_t r = lnd_ogg_init(&s->ogg, io, (int)(uintptr_t)s ^ 0x4F707573);
    if (r != LND_OK) {
        lnd_opus_enc_close(s);
        return r;
    }
    uint8_t head[64];
    size_t n = 0;
    memcpy(head, "OpusHead", 8);
    n = 8;
    head[n++] = 1;
    head[n++] = (uint8_t)ch;
    lnd_opus_le16(head + n, (uint16_t)s->preskip);
    n += 2;
    lnd_opus_le32(head + n, params->sample_rate_hz);
    n += 4;
    lnd_opus_le16(head + n, 0);
    n += 2;
    head[n++] = ch <= 2 ? 0 : 1;
    if (ch > 2) {
        head[n++] = (uint8_t)streams;
        head[n++] = (uint8_t)coupled;
        memcpy(head + n, mapping, ch);
        n += ch;
    }
    ogg_packet op = {.packet = head, .bytes = (long)n, .b_o_s = 1, .granulepos = 0, .packetno = 0};
    r = lnd_ogg_packet(&s->ogg, &op);
    if (r == LND_OK) r = lnd_ogg_pages(&s->ogg, true);
    static const char vendor[] = "LINDAR";
    static const char tag[] = "ENCODER=LINDAR";
    uint8_t tags[64];
    n = 0;
    memcpy(tags, "OpusTags", 8);
    n = 8;
    lnd_opus_le32(tags + n, (uint32_t)(sizeof vendor - 1));
    n += 4;
    memcpy(tags + n, vendor, sizeof vendor - 1);
    n += sizeof vendor - 1;
    lnd_opus_le32(tags + n, 1);
    n += 4;
    lnd_opus_le32(tags + n, (uint32_t)(sizeof tag - 1));
    n += 4;
    memcpy(tags + n, tag, sizeof tag - 1);
    n += sizeof tag - 1;
    void *custom_tags = nullptr;
#if LND_MODULE_METADATA_COMMENTS
    if (r == LND_OK && params->metadata) {
        r = LND_MetadataOpusCreateBuffer(params->metadata, params->metadata_flags, &custom_tags, &n);
        if (!r && n > LONG_MAX) r = LND_ERR_UNSUPPORTED;
    }
#endif
    ogg_packet ot = {.packet = custom_tags ? custom_tags : tags, .bytes = (long)n, .b_o_s = 0, .granulepos = 0, .packetno = 1};
    if (r == LND_OK) r = lnd_ogg_packet(&s->ogg, &ot);
    lnd_free(custom_tags);
    if (r == LND_OK) r = lnd_ogg_pages(&s->ogg, true);
    if (r != LND_OK) {
        lnd_opus_enc_close(s);
        return r;
    }
    s->packetno = 2;
    *state = s;
    return LND_OK;
}

static int32_t lnd_opus_feed_all(lnd_opus_enc *s, const float *pcm, uint64_t frames) {
    if (!s->resampler) return lnd_opus_push(s, pcm, frames);
    s->feed.data = pcm;
    s->feed.frames = frames;
    for (;;) {
        uint64_t got = lnd_source_read(s->resampler, s->rs, s->rs_frames);
        if (!got) return LND_OK;
        int32_t r = lnd_opus_push(s, s->rs, got);
        if (r != LND_OK) return r;
        if (got < s->rs_frames) return LND_OK;
    }
}

static int32_t lnd_opus_enc_write(void *state, const float *pcm, uint64_t frames) { return lnd_opus_feed_all(state, pcm, frames); }

static int32_t lnd_opus_enc_flush(void *state) {
    lnd_opus_enc *s = state;
    return lnd_ogg_pages(&s->ogg, true);
}

static int32_t lnd_opus_enc_close(void *state) {
    lnd_opus_enc *s = state;
    int32_t r = LND_OK;
    if (s->ogg.ready && (s->enc || s->ms)) {
        if (s->resampler) {
            float zeros[64 * LND_MAX_CHANNELS];
            memset(zeros, 0, sizeof zeros);
            uint64_t before = s->valid;
            r = lnd_opus_feed_all(s, zeros, 64);
            s->valid = before;
        }
        if (r == LND_OK) {
            memset(s->frame + (size_t)s->filled * s->channels, 0, (size_t)(s->frame_size - s->filled) * s->channels * sizeof(float));
            uint64_t final = s->preskip + s->valid * 48000 / s->enc_rate;
            uint64_t step = (uint64_t)s->frame_size * 48000 / s->enc_rate;
            while (s->granule + step < final && r == LND_OK) {
                r = lnd_opus_emit(s, false);
                memset(s->frame, 0, (size_t)s->frame_size * s->channels * sizeof(float));
            }
            if (r == LND_OK) r = lnd_opus_emit(s, true);
        }
    }
    lnd_ogg_clear(&s->ogg);
    if (s->enc) opus_encoder_destroy(s->enc);
    if (s->ms) opus_multistream_encoder_destroy(s->ms);
    lnd_source_free(s->resampler);
    lnd_free(s->rs);
    lnd_free(s->frame);
    lnd_free(s);
    return r;
}

const LND_ENCODER lnd_encoder_opus = {
    .name = "opus",
    .extensions = "opus",
    .flags = LND_ENCODER_FLAG_FRAME_SIZE | (LND_MODULE_METADATA_COMMENTS ? LND_ENCODER_FLAG_METADATA : 0),
    .open = lnd_opus_enc_open,
    .write = lnd_opus_enc_write,
    .flush = lnd_opus_enc_flush,
    .close = lnd_opus_enc_close,
};
