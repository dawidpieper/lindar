#include "lame/include/lame.h"

#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_output.h"
#include "io/output/output.h"
#if LND_MODULE_METADATA_ID3V2
#include "lindar_metadata_id3v2.h"
#endif

#include <string.h>

#define LND_LAME_BLOCK 4096

typedef struct lnd_mp3_enc {
    lame_global_flags *gf;
    lnd_io *io;
    uint32_t channels;
    uint8_t *out;
    size_t out_cap;
    uint64_t start;
    bool tag;
    bool started;
} lnd_mp3_enc;

static int32_t lnd_mp3_enc_close(void *state);

static int32_t lnd_mp3_enc_open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state) {
    (void)extension;
    if (params->channels > 2) return LND_ERR_UNSUPPORTED;
    lnd_mp3_enc *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->channels = params->channels;
    s->out_cap = LND_LAME_BLOCK * 5 / 4 + 7200;
    s->out = lnd_alloc(s->out_cap);
    s->gf = lame_init();
    if (!s->out || !s->gf) {
        lnd_mp3_enc_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    lame_global_flags *gf = s->gf;
    lame_set_in_samplerate(gf, (int)params->sample_rate_hz);
    lame_set_out_samplerate(gf, (int)params->sample_rate_hz);
    lame_set_num_channels(gf, (int)params->channels);
    char opt[16];
    bool plain = lnd_encoder_option(params->options, "mode", opt, sizeof opt) && strcmp(opt, "stereo") == 0;
    lame_set_mode(gf, params->channels == 1 ? MONO : (plain ? STEREO : JOINT_STEREO));
    int64_t algorithm = lnd_encoder_option_int(params->options, "algorithm", -1);
    if (algorithm >= 0 && algorithm <= 9) lame_set_quality(gf, (int)algorithm);
    bool vbr = params->mode == LND_ENCODER_MODE_VBR || (params->quality && !params->bitrate_kbps && params->mode == LND_ENCODER_MODE_DEFAULT);
    if (vbr) {
        lame_set_VBR(gf, vbr_mtrh);
        lame_set_VBR_q(gf, (int)(9 - lnd_encoder_level(params, 5, 9)));
        if (params->bitrate_kbps) lame_set_VBR_mean_bitrate_kbps(gf, (int)params->bitrate_kbps);
    } else if (params->mode == LND_ENCODER_MODE_ABR) {
        lame_set_VBR(gf, vbr_abr);
        lame_set_VBR_mean_bitrate_kbps(gf, params->bitrate_kbps ? (int)params->bitrate_kbps : 128);
    } else {
        lame_set_VBR(gf, vbr_off);
        lame_set_brate(gf, params->bitrate_kbps ? (int)params->bitrate_kbps : 128);
    }
    s->tag = LND_IoCanSeek(io) && !(params->flags & LND_OUTPUT_STREAMING);
    lame_set_bWriteVbrTag(gf, s->tag);
    if (lame_init_params(gf) < 0) {
        lnd_mp3_enc_close(s);
        return LND_ERR_UNSUPPORTED;
    }
#if LND_MODULE_METADATA_ID3V2
    if (params->metadata) {
        void *metadata = nullptr;
        size_t bytes = 0;
        int32_t result = LND_MetadataId3v2CreateBuffer(params->metadata, 0, params->metadata_flags, &metadata, &bytes);
        if (!result && LND_IoWrite(io, metadata, bytes) != bytes) result = LND_ERR_IO;
        lnd_free(metadata);
        if (result) {
            lnd_mp3_enc_close(s);
            return result;
        }
    }
#endif
    s->started = true;
    s->start = LND_IoGetPositionBytes(io);
    *state = s;
    return LND_OK;
}

static int32_t lnd_mp3_enc_write(void *state, const float *pcm, uint64_t frames) {
    lnd_mp3_enc *s = state;
    for (uint64_t done = 0; done < frames;) {
        int n = (int)LND_MIN(frames - done, (uint64_t)LND_LAME_BLOCK);
        const float *src = pcm + done * s->channels;
        int got = s->channels == 1 ? lame_encode_buffer_ieee_float(s->gf, src, nullptr, n, s->out, (int)s->out_cap)
                                   : lame_encode_buffer_interleaved_ieee_float(s->gf, src, n, s->out, (int)s->out_cap);
        if (got < 0) return LND_ERR_IO;
        if (got > 0 && LND_IoWrite(s->io, s->out, (size_t)got) != (size_t)got) return LND_ERR_IO;
        done += (uint64_t)n;
    }
    return LND_OK;
}

static int32_t lnd_mp3_enc_close(void *state) {
    lnd_mp3_enc *s = state;
    int32_t r = LND_OK;
    if (s->gf && s->out && s->started) {
        int got = lame_encode_flush(s->gf, s->out, (int)s->out_cap);
        if (got > 0 && LND_IoWrite(s->io, s->out, (size_t)got) != (size_t)got) r = LND_ERR_IO;
        if (r == LND_OK && s->tag) {
            size_t tag = lame_get_lametag_frame(s->gf, s->out, s->out_cap);
            if (tag > 0 && tag <= s->out_cap) {
                uint64_t end = LND_IoGetPositionBytes(s->io);
                if (LND_IoSeekBytes(s->io, s->start) != LND_OK || LND_IoWrite(s->io, s->out, tag) != tag) r = LND_ERR_IO;
                LND_IoSeekBytes(s->io, end);
            }
        }
    }
    if (s->gf) lame_close(s->gf);
    lnd_free(s->out);
    lnd_free(s);
    return r;
}

const LND_ENCODER lnd_encoder_mp3 = {
    .name = "mp3",
    .extensions = "mp3",
    .flags = LND_MODULE_METADATA_ID3V2 ? LND_ENCODER_FLAG_METADATA : 0,
    .open = lnd_mp3_enc_open,
    .write = lnd_mp3_enc_write,
    .close = lnd_mp3_enc_close,
};
