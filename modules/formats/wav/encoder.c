#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_output.h"
#include "pcm/audio/convert.h"
#include "src/format.h"
#if LND_MODULE_METADATA_WAVE
#include "lindar_metadata_wave.h"
#endif

#include <string.h>

typedef struct lnd_wav_enc {
    lnd_io *io;
    int32_t format;
    uint32_t channels;
    uint64_t frames;
    uint64_t data_offset;
    uint8_t *scratch;
    uint32_t scratch_frames;
} lnd_wav_enc;

static void lnd_wav_put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void lnd_wav_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int32_t lnd_wav_enc_close(void *state);

static int32_t lnd_wav_enc_open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state) {
    (void)extension;
    int32_t format = params->format ? params->format : LND_FORMAT_S16;
    lnd_wav_enc *w = lnd_alloc_zero(sizeof *w);
    if (!w) return LND_ERR_OUT_OF_MEMORY;
    w->io = io;
    w->format = format;
    w->channels = params->channels;
    w->scratch_frames = 4096;
    w->scratch = lnd_alloc((size_t)w->scratch_frames * params->channels * lnd_format_bytes(format));
    if (!w->scratch) {
        lnd_free(w);
        return LND_ERR_OUT_OF_MEMORY;
    }
    uint32_t bits = lnd_format_bits(format);
    uint32_t block = params->channels * bits / 8;
    bool is_float = lnd_format_is_float(format);
    bool extensible = params->channels > 2 || bits > 16;
    bool unknown = !LND_IoCanSeek(io) || (params->flags & LND_OUTPUT_STREAMING);
    uint32_t size_field = unknown ? 0xFFFFFFFFu : 0;
    uint8_t h[68];
    size_t n = 0;
    memcpy(h + n, "RIFF", 4);
    n += 4;
    lnd_wav_put32(h + n, size_field);
    n += 4;
    memcpy(h + n, "WAVEfmt ", 8);
    n += 8;
    lnd_wav_put32(h + n, extensible ? 40 : 16);
    n += 4;
    lnd_wav_put16(h + n, extensible ? 0xFFFE : (is_float ? 3 : 1));
    n += 2;
    lnd_wav_put16(h + n, (uint16_t)params->channels);
    n += 2;
    lnd_wav_put32(h + n, params->sample_rate_hz);
    n += 4;
    lnd_wav_put32(h + n, params->sample_rate_hz * block);
    n += 4;
    lnd_wav_put16(h + n, (uint16_t)block);
    n += 2;
    lnd_wav_put16(h + n, (uint16_t)bits);
    n += 2;
    if (extensible) {
        lnd_wav_put16(h + n, 22);
        n += 2;
        lnd_wav_put16(h + n, (uint16_t)bits);
        n += 2;
        lnd_wav_put32(h + n,
                      params->channels == 1 ? 0x4 : (params->channels == 2 ? 0x3 : (params->channels >= 32 ? 0xFFFFFFFFu : (1u << params->channels) - 1)));
        n += 4;
        lnd_wav_put16(h + n, is_float ? 3 : 1);
        n += 2;
        static const uint8_t guid_tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        memcpy(h + n, guid_tail, 14);
        n += 14;
    }
    memcpy(h + n, "data", 4);
    n += 4;
    lnd_wav_put32(h + n, size_field);
    n += 4;
    void *metadata = nullptr;
    size_t metadata_size = 0;
#if LND_MODULE_METADATA_WAVE
    if (params->metadata) {
        int32_t result = LND_MetadataWaveCreateBuffer(params->metadata, params->sample_rate_hz, params->metadata_flags, &metadata, &metadata_size);
        if (result != LND_OK) {
            lnd_wav_enc_close(w);
            return result;
        }
    }
#endif
    bool written = LND_IoWrite(io, h, n - 8) == n - 8 && (!metadata_size || LND_IoWrite(io, metadata, metadata_size) == metadata_size) &&
                   LND_IoWrite(io, h + n - 8, 8) == 8;
    lnd_free(metadata);
    if (!written) {
        lnd_wav_enc_close(w);
        return LND_ERR_IO;
    }
    w->data_offset = n + metadata_size;
    *state = w;
    return LND_OK;
}

static int32_t lnd_wav_enc_write(void *state, const float *pcm, uint64_t frames) {
    lnd_wav_enc *w = state;
    size_t frame_bytes = (size_t)w->channels * lnd_format_bytes(w->format);
    for (uint64_t done = 0; done < frames;) {
        uint32_t nb = (uint32_t)LND_MIN(frames - done, (uint64_t)w->scratch_frames);
        const void *src = pcm + done * w->channels;
        if (w->format != LND_FORMAT_F32) {
            lnd_pcm_from_f32(w->format, pcm + done * w->channels, w->scratch, (size_t)nb * w->channels);
            src = w->scratch;
        }
        if (LND_IoWrite(w->io, src, nb * frame_bytes) != nb * frame_bytes) return LND_ERR_IO;
        done += nb;
    }
    w->frames += frames;
    return LND_OK;
}

static int32_t lnd_wav_enc_close(void *state) {
    lnd_wav_enc *w = state;
    int32_t r = LND_OK;
    if (w->data_offset) {
        uint64_t data_bytes = w->frames * w->channels * lnd_format_bytes(w->format);
        if (data_bytes & 1) {
            uint8_t pad = 0;
            LND_IoWrite(w->io, &pad, 1);
        }
        uint64_t end = LND_IoGetPositionBytes(w->io);
        if (LND_IoCanSeek(w->io) && data_bytes < 0xFFFFFFFFull) {
            uint8_t v[4];
            lnd_wav_put32(v, (uint32_t)(end - 8));
            if (LND_IoSeekBytes(w->io, 4) != LND_OK || LND_IoWrite(w->io, v, 4) != 4) r = LND_ERR_IO;
            lnd_wav_put32(v, (uint32_t)data_bytes);
            if (LND_IoSeekBytes(w->io, w->data_offset - 4) != LND_OK || LND_IoWrite(w->io, v, 4) != 4) r = LND_ERR_IO;
            LND_IoSeekBytes(w->io, end);
        }
    }
    lnd_free(w->scratch);
    lnd_free(w);
    return r;
}

const LND_ENCODER lnd_encoder_wav = {
    .name = "wav",
    .extensions = "wav;wave",
    .flags = LND_MODULE_METADATA_WAVE ? LND_ENCODER_FLAG_METADATA : 0,
    .open = lnd_wav_enc_open,
    .write = lnd_wav_enc_write,
    .close = lnd_wav_enc_close,
};
