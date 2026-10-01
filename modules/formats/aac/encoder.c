#include <aacenc_lib.h>

#include "src/alloc.h"
#include "src/platform.h"
#include "mp4.h"
#include "io/io.h"
#include "lindar_output.h"
#include "io/output/output.h"

#include <math.h>
#include <string.h>

typedef struct lnd_aac_enc {
    HANDLE_AACENCODER enc;
    lnd_io *io;
    lnd_mp4_mux mux;
    bool mp4;
    bool mux_ready;
    uint32_t channels;
    uint32_t frame_length;
    uint32_t filled;
    int16_t *frame;
    uint8_t out[16384];
    uint64_t frames;
} lnd_aac_enc;

static int32_t lnd_aac_enc_close(void *state);

static int32_t lnd_aac_encode(lnd_aac_enc *s, int in_samples) {
    void *in_ptr = s->frame;
    INT in_id = IN_AUDIO_DATA, in_size = (INT)((size_t)(in_samples > 0 ? in_samples : 0) * sizeof(int16_t)), in_el = sizeof(int16_t);
    AACENC_BufDesc in_desc = {.numBufs = 1, .bufs = &in_ptr, .bufferIdentifiers = &in_id, .bufSizes = &in_size, .bufElSizes = &in_el};
    void *out_ptr = s->out;
    INT out_id = OUT_BITSTREAM_DATA, out_size = sizeof s->out, out_el = 1;
    AACENC_BufDesc out_desc = {.numBufs = 1, .bufs = &out_ptr, .bufferIdentifiers = &out_id, .bufSizes = &out_size, .bufElSizes = &out_el};
    AACENC_InArgs in_args = {.numInSamples = in_samples};
    AACENC_OutArgs out_args = {0};
    AACENC_ERROR err = aacEncEncode(s->enc, &in_desc, &out_desc, &in_args, &out_args);
    if (err == AACENC_ENCODE_EOF) return 1;
    if (err != AACENC_OK) return LND_ERR_IO;
    if (out_args.numOutBytes > 0) {
        if (s->mp4) return lnd_mp4_sample(&s->mux, s->out, (uint32_t)out_args.numOutBytes);
        if (LND_IoWrite(s->io, s->out, (size_t)out_args.numOutBytes) != (size_t)out_args.numOutBytes) return LND_ERR_IO;
    }
    return LND_OK;
}

static int32_t lnd_aac_enc_open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state) {
    static const CHANNEL_MODE modes[8] = {MODE_1, MODE_2, MODE_1_2, MODE_1_2_1, MODE_1_2_2, MODE_1_2_2_1, MODE_6_1, MODE_7_1_BACK};
    uint32_t ch = params->channels;
    if (ch > 8) return LND_ERR_UNSUPPORTED;
    char opt[16];
    uint32_t aot = 2;
    if (lnd_encoder_option(params->options, "aot", opt, sizeof opt)) {
        if (strcmp(opt, "he") == 0) aot = 5;
        else if (strcmp(opt, "hev2") == 0) aot = 29;
        else if (strcmp(opt, "ld") == 0) aot = 23;
        else if (strcmp(opt, "eld") == 0) aot = 39;
        else if (strcmp(opt, "lc") != 0) return LND_ERR_INVALID_ARG;
    }
    bool mp4 = extension && (strcmp(extension, "m4a") == 0 || strcmp(extension, "mp4") == 0 || strcmp(extension, "m4b") == 0 || strcmp(extension, "m4r") == 0);
    if (lnd_encoder_option(params->options, "container", opt, sizeof opt)) mp4 = strcmp(opt, "mp4") == 0 || strcmp(opt, "m4a") == 0;
    if (mp4 && !LND_IoCanSeek(io)) return LND_ERR_UNSUPPORTED;
    lnd_aac_enc *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->mp4 = mp4;
    s->channels = ch;
    if (aacEncOpen(&s->enc, 0, ch) != AACENC_OK) {
        lnd_aac_enc_close(s);
        return LND_ERR_UNSUPPORTED;
    }
    bool vbr = params->mode == LND_ENCODER_MODE_VBR || (params->quality && params->mode != LND_ENCODER_MODE_CBR && !params->bitrate_kbps);
    uint32_t bitrate_bps = params->bitrate_kbps ? params->bitrate_kbps * 1000 : (aot == 2 || aot == 23 ? 64000 : 32000) * ch;
    bool ok = aacEncoder_SetParam(s->enc, AACENC_AOT, aot) == AACENC_OK;
    ok = ok && aacEncoder_SetParam(s->enc, AACENC_SAMPLERATE, params->sample_rate_hz) == AACENC_OK;
    if (params->frame_size_frames) ok = ok && aacEncoder_SetParam(s->enc, AACENC_GRANULE_LENGTH, params->frame_size_frames) == AACENC_OK;
    ok = ok && aacEncoder_SetParam(s->enc, AACENC_CHANNELMODE, modes[ch - 1]) == AACENC_OK;
    ok = ok && aacEncoder_SetParam(s->enc, AACENC_CHANNELORDER, 1) == AACENC_OK;
    ok = ok && aacEncoder_SetParam(s->enc, AACENC_TRANSMUX, mp4 ? TT_MP4_RAW : TT_MP4_ADTS) == AACENC_OK;
    ok = ok && aacEncoder_SetParam(s->enc, AACENC_AFTERBURNER, (UINT)lnd_encoder_option_int(params->options, "afterburner", 1)) == AACENC_OK;
    if (mp4 && aot != 2 && aot != 23) ok = ok && aacEncoder_SetParam(s->enc, AACENC_SIGNALING_MODE, 1) == AACENC_OK;
    if (vbr) ok = ok && aacEncoder_SetParam(s->enc, AACENC_BITRATEMODE, LND_MAX(1u, lnd_encoder_level(params, 4, 5))) == AACENC_OK;
    else ok = ok && aacEncoder_SetParam(s->enc, AACENC_BITRATE, bitrate_bps) == AACENC_OK;
    if (!ok || aacEncEncode(s->enc, nullptr, nullptr, nullptr, nullptr) != AACENC_OK) {
        lnd_aac_enc_close(s);
        return LND_ERR_UNSUPPORTED;
    }
    AACENC_InfoStruct info;
    if (aacEncInfo(s->enc, &info) != AACENC_OK) {
        lnd_aac_enc_close(s);
        return LND_ERR_UNSUPPORTED;
    }
    s->frame_length = info.frameLength;
    s->frame = lnd_alloc((size_t)s->frame_length * ch * sizeof(int16_t));
    if (!s->frame) {
        lnd_aac_enc_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    if (mp4) {
        int32_t r = lnd_mp4_begin(&s->mux, io, params->sample_rate_hz, ch, s->frame_length, info.nDelay, bitrate_bps, info.confBuf, info.confSize);
        if (r != LND_OK) {
            lnd_aac_enc_close(s);
            return r;
        }
        s->mux_ready = true;
    }
    *state = s;
    return LND_OK;
}

static int32_t lnd_aac_enc_write(void *state, const float *pcm, uint64_t frames) {
    lnd_aac_enc *s = state;
    uint32_t ch = s->channels;
    for (uint64_t done = 0; done < frames;) {
        uint32_t n = (uint32_t)LND_MIN(frames - done, (uint64_t)(s->frame_length - s->filled));
        const float *src = pcm + done * ch;
        int16_t *dst = s->frame + (size_t)s->filled * ch;
        for (size_t i = 0; i < (size_t)n * ch; i++) {
            float v = src[i] * 32767.0f;
            dst[i] = (int16_t)lrintf(v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v));
        }
        s->filled += n;
        done += n;
        if (s->filled == s->frame_length) {
            int32_t r = lnd_aac_encode(s, (int)(s->frame_length * ch));
            s->filled = 0;
            if (r != LND_OK) return r == 1 ? LND_ERR_IO : r;
        }
    }
    s->frames += frames;
    return LND_OK;
}

static int32_t lnd_aac_enc_close(void *state) {
    lnd_aac_enc *s = state;
    int32_t r = LND_OK;
    if (s->enc && s->frame) {
        if (s->filled) {
            memset(s->frame + (size_t)s->filled * s->channels, 0, (size_t)(s->frame_length - s->filled) * s->channels * sizeof(int16_t));
            r = lnd_aac_encode(s, (int)(s->frame_length * s->channels));
            if (r == 1) r = LND_OK;
        }
        for (int i = 0; r == LND_OK && i < 64; i++) {
            r = lnd_aac_encode(s, -1);
            if (r == 1) {
                r = LND_OK;
                break;
            }
        }
        if (r == LND_OK && s->mux_ready) r = lnd_mp4_finish(&s->mux, s->frames);
    }
    if (s->mux_ready) lnd_mp4_free(&s->mux);
    if (s->enc) aacEncClose(&s->enc);
    lnd_free(s->frame);
    lnd_free(s);
    return r;
}

const LND_ENCODER lnd_encoder_aac = {
    .name = "aac",
    .extensions = "aac;adts;m4a;mp4;m4b;m4r",
    .flags = LND_ENCODER_FLAG_SEEK | LND_ENCODER_FLAG_FRAME_SIZE,
    .open = lnd_aac_enc_open,
    .write = lnd_aac_enc_write,
    .close = lnd_aac_enc_close,
};
