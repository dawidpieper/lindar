#include <FLAC/stream_encoder.h>

#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_output.h"
#include "io/output/output.h"

#include <math.h>
#include <string.h>

typedef struct lnd_flac_enc {
    FLAC__StreamEncoder *enc;
    lnd_io *io;
    uint32_t channels;
    uint32_t bits;
    FLAC__int32 *scratch;
    uint32_t scratch_frames;
} lnd_flac_enc;

static FLAC__StreamEncoderWriteStatus lnd_flac_write_cb(const FLAC__StreamEncoder *enc, const FLAC__byte buffer[], size_t bytes, uint32_t samples,
                                                        uint32_t frame, void *client) {
    (void)enc;
    (void)samples;
    (void)frame;
    lnd_flac_enc *s = client;
    return LND_IoWrite(s->io, buffer, bytes) == bytes ? FLAC__STREAM_ENCODER_WRITE_STATUS_OK : FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
}

static FLAC__StreamEncoderSeekStatus lnd_flac_seek_cb(const FLAC__StreamEncoder *enc, FLAC__uint64 offset, void *client) {
    (void)enc;
    lnd_flac_enc *s = client;
    return LND_IoSeekBytes(s->io, offset) == LND_OK ? FLAC__STREAM_ENCODER_SEEK_STATUS_OK : FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
}

static FLAC__StreamEncoderTellStatus lnd_flac_tell_cb(const FLAC__StreamEncoder *enc, FLAC__uint64 *offset, void *client) {
    (void)enc;
    lnd_flac_enc *s = client;
    *offset = LND_IoGetPositionBytes(s->io);
    return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

static int32_t lnd_flac_enc_close(void *state);

static int32_t lnd_flac_enc_open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state) {
    (void)extension;
    int32_t format = params->format ? params->format : LND_FORMAT_S16;
    uint32_t bits = format == LND_FORMAT_U8 ? 8 : (format == LND_FORMAT_S16 ? 16 : (format == LND_FORMAT_S24 ? 24 : 0));
    if (!bits || params->channels > 8) return LND_ERR_UNSUPPORTED;
    lnd_flac_enc *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->channels = params->channels;
    s->bits = bits;
    s->scratch_frames = 4096;
    s->scratch = lnd_alloc((size_t)s->scratch_frames * params->channels * sizeof(FLAC__int32));
    s->enc = FLAC__stream_encoder_new();
    if (!s->scratch || !s->enc) {
        lnd_flac_enc_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    bool seekable = LND_IoCanSeek(io) && !(params->flags & LND_OUTPUT_STREAMING);
    FLAC__stream_encoder_set_channels(s->enc, params->channels);
    FLAC__stream_encoder_set_bits_per_sample(s->enc, bits);
    FLAC__stream_encoder_set_sample_rate(s->enc, params->sample_rate_hz);
    FLAC__stream_encoder_set_compression_level(s->enc, lnd_encoder_level(params, 5, 8));
    FLAC__stream_encoder_set_verify(s->enc, false);
    FLAC__stream_encoder_set_streamable_subset(s->enc, true);
    FLAC__StreamEncoderInitStatus st =
        FLAC__stream_encoder_init_stream(s->enc, lnd_flac_write_cb, seekable ? lnd_flac_seek_cb : nullptr, seekable ? lnd_flac_tell_cb : nullptr, nullptr, s);
    if (st != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        lnd_flac_enc_close(s);
        return st == FLAC__STREAM_ENCODER_INIT_STATUS_ENCODER_ERROR ? LND_ERR_IO : LND_ERR_UNSUPPORTED;
    }
    *state = s;
    return LND_OK;
}

static int32_t lnd_flac_enc_write(void *state, const float *pcm, uint64_t frames) {
    lnd_flac_enc *s = state;
    float scale = (float)(1 << (s->bits - 1));
    float hi = scale - 1.0f, lo = -scale;
    for (uint64_t done = 0; done < frames;) {
        uint32_t nb = (uint32_t)LND_MIN(frames - done, (uint64_t)s->scratch_frames);
        size_t samples = (size_t)nb * s->channels;
        const float *src = pcm + done * s->channels;
        for (size_t i = 0; i < samples; i++) {
            float v = src[i] * scale;
            v = v > hi ? hi : (v < lo ? lo : v);
            s->scratch[i] = (FLAC__int32)lrintf(v);
        }
        if (!FLAC__stream_encoder_process_interleaved(s->enc, s->scratch, nb)) return LND_ERR_IO;
        done += nb;
    }
    return LND_OK;
}

static int32_t lnd_flac_enc_close(void *state) {
    lnd_flac_enc *s = state;
    int32_t r = LND_OK;
    if (s->enc) {
        if (FLAC__stream_encoder_get_state(s->enc) == FLAC__STREAM_ENCODER_OK && !FLAC__stream_encoder_finish(s->enc)) r = LND_ERR_IO;
        FLAC__stream_encoder_delete(s->enc);
    }
    lnd_free(s->scratch);
    lnd_free(s);
    return r;
}

const LND_ENCODER lnd_encoder_flac = {
    .name = "flac",
    .extensions = "flac",
    .open = lnd_flac_enc_open,
    .write = lnd_flac_enc_write,
    .close = lnd_flac_enc_close,
};
