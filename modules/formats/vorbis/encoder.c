#include <vorbis/vorbisenc.h>

#include "src/alloc.h"
#include "src/platform.h"
#include "formats/ogg/mux.h"
#include "lindar_output.h"
#include "io/output/output.h"

#include <string.h>

typedef struct lnd_vorbis_enc {
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    lnd_ogg_mux ogg;
    uint32_t channels;
    bool streaming;
    bool analysis;
} lnd_vorbis_enc;

static int32_t lnd_vorbis_drain(lnd_vorbis_enc *s) {
    ogg_packet op;
    while (vorbis_analysis_blockout(&s->vd, &s->vb) == 1) {
        if (vorbis_analysis(&s->vb, nullptr) != 0 || vorbis_bitrate_addblock(&s->vb) != 0) return LND_ERR_IO;
        while (vorbis_bitrate_flushpacket(&s->vd, &op) == 1) {
            int32_t r = lnd_ogg_packet(&s->ogg, &op);
            if (r == LND_OK) r = lnd_ogg_pages(&s->ogg, s->streaming);
            if (r != LND_OK) return r;
        }
    }
    return LND_OK;
}

static int32_t lnd_vorbis_enc_close(void *state);

static int32_t lnd_vorbis_enc_open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state) {
    (void)extension;
    if (params->channels > 255) return LND_ERR_UNSUPPORTED;
    lnd_vorbis_enc *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->channels = params->channels;
    s->streaming = (params->flags & LND_OUTPUT_STREAMING) != 0;
    vorbis_info_init(&s->vi);
    vorbis_comment_init(&s->vc);
    int r;
    if (params->bitrate_kbps) {
        long nominal = (long)params->bitrate_kbps * 1000;
        long bound = params->mode == LND_ENCODER_MODE_CBR ? nominal : -1;
        r = vorbis_encode_init(&s->vi, (long)params->channels, (long)params->sample_rate_hz, bound, nominal, bound);
    } else {
        float q = params->quality ? -0.1f + 1.1f * (float)params->quality / 100.0f : 0.4f;
        r = vorbis_encode_init_vbr(&s->vi, (long)params->channels, (long)params->sample_rate_hz, q);
    }
    if (r != 0) {
        lnd_vorbis_enc_close(s);
        return LND_ERR_UNSUPPORTED;
    }
    vorbis_comment_add_tag(&s->vc, "ENCODER", "LINDAR");
    if (vorbis_analysis_init(&s->vd, &s->vi) != 0 || vorbis_block_init(&s->vd, &s->vb) != 0) {
        lnd_vorbis_enc_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    s->analysis = true;
    int32_t rc = lnd_ogg_init(&s->ogg, io, (int)(uintptr_t)s ^ (int)params->sample_rate_hz);
    if (rc == LND_OK) {
        ogg_packet h, hc, hcb;
        vorbis_analysis_headerout(&s->vd, &s->vc, &h, &hc, &hcb);
        rc = lnd_ogg_packet(&s->ogg, &h);
        if (rc == LND_OK) rc = lnd_ogg_packet(&s->ogg, &hc);
        if (rc == LND_OK) rc = lnd_ogg_packet(&s->ogg, &hcb);
        if (rc == LND_OK) rc = lnd_ogg_pages(&s->ogg, true);
    }
    if (rc != LND_OK) {
        lnd_vorbis_enc_close(s);
        return rc;
    }
    *state = s;
    return LND_OK;
}

static int32_t lnd_vorbis_enc_write(void *state, const float *pcm, uint64_t frames) {
    lnd_vorbis_enc *s = state;
    uint32_t ch = s->channels;
    for (uint64_t done = 0; done < frames;) {
        uint32_t nb = (uint32_t)LND_MIN(frames - done, (uint64_t)1024);
        float **buf = vorbis_analysis_buffer(&s->vd, (int)nb);
        if (!buf) return LND_ERR_OUT_OF_MEMORY;
        const float *src = pcm + done * ch;
        for (uint32_t c = 0; c < ch; c++) {
            for (uint32_t f = 0; f < nb; f++) buf[c][f] = src[f * ch + c];
        }
        vorbis_analysis_wrote(&s->vd, (int)nb);
        int32_t r = lnd_vorbis_drain(s);
        if (r != LND_OK) return r;
        done += nb;
    }
    return LND_OK;
}

static int32_t lnd_vorbis_enc_flush(void *state) {
    lnd_vorbis_enc *s = state;
    return lnd_ogg_pages(&s->ogg, true);
}

static int32_t lnd_vorbis_enc_close(void *state) {
    lnd_vorbis_enc *s = state;
    int32_t r = LND_OK;
    if (s->analysis && s->ogg.ready) {
        vorbis_analysis_wrote(&s->vd, 0);
        r = lnd_vorbis_drain(s);
        if (r == LND_OK) r = lnd_ogg_pages(&s->ogg, true);
    }
    lnd_ogg_clear(&s->ogg);
    if (s->analysis) {
        vorbis_block_clear(&s->vb);
        vorbis_dsp_clear(&s->vd);
    }
    vorbis_comment_clear(&s->vc);
    vorbis_info_clear(&s->vi);
    lnd_free(s);
    return r;
}

const LND_ENCODER lnd_encoder_vorbis = {
    .name = "vorbis",
    .extensions = "ogg;oga",
    .open = lnd_vorbis_enc_open,
    .write = lnd_vorbis_enc_write,
    .flush = lnd_vorbis_enc_flush,
    .close = lnd_vorbis_enc_close,
};
