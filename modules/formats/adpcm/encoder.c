#include "block.h"
#include "src/alloc.h"
#include "io/output/output.h"
#include "pcm/audio/convert.h"

#include <string.h>

typedef struct lnd_adpcm_encoder {
    LND_IO *io;
    uint32_t tag, channels, block, samples, filled, header_size;
    uint64_t frames, bytes;
    int16_t *pcm;
    uint8_t *encoded;
    bool failed;
} lnd_adpcm_encoder;

static void lnd_adpcm_put16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void lnd_adpcm_put32(uint8_t *p, uint32_t value) {
    lnd_adpcm_put16(p, (uint16_t)value);
    lnd_adpcm_put16(p + 2, (uint16_t)(value >> 16));
}

static int32_t lnd_adpcm_emit(lnd_adpcm_encoder *s) {
    if (s->failed) return LND_ERR_IO;
    if (s->bytes > UINT32_MAX - s->header_size - s->block) return LND_ERR_UNSUPPORTED;
    lnd_adpcm_encode_block(s->tag, s->channels, s->pcm, s->samples, s->encoded);
    if (LND_IoWrite(s->io, s->encoded, s->block) != s->block) {
        s->failed = true;
        return LND_ERR_IO;
    }
    s->bytes += s->block;
    s->filled = 0;
    return LND_OK;
}

static int32_t lnd_adpcm_enc_close(void *state) {
    lnd_adpcm_encoder *s = state;
    int32_t r = s->failed ? LND_ERR_IO : LND_OK;
    if (s->header_size && !s->failed) {
        if (s->filled) {
            for (uint32_t f = s->filled; f < s->samples; f++)
                memcpy(s->pcm + f * s->channels, s->pcm + (s->filled - 1) * s->channels, s->channels * sizeof(int16_t));
            r = lnd_adpcm_emit(s);
        }
        uint64_t end = LND_IoGetPositionBytes(s->io);
        if (r == LND_OK && (s->bytes & 1)) {
            uint8_t pad = 0;
            if (LND_IoWrite(s->io, &pad, 1) != 1) r = LND_ERR_IO;
            end++;
        }
        uint8_t field[4];
        uint64_t offsets[3] = {4, s->header_size - 12, s->header_size - 4};
        uint32_t values[3] = {(uint32_t)(end - 8), (uint32_t)s->frames, (uint32_t)s->bytes};
        for (uint32_t i = 0; r == LND_OK && i < 3; i++) {
            lnd_adpcm_put32(field, values[i]);
            if (LND_IoSeekBytes(s->io, offsets[i]) != LND_OK || LND_IoWrite(s->io, field, 4) != 4) r = LND_ERR_IO;
        }
        if (LND_IoSeekBytes(s->io, end) != LND_OK) r = LND_ERR_IO;
    }
    lnd_free(s->pcm);
    lnd_free(s->encoded);
    lnd_free(s);
    return r;
}

static int32_t lnd_adpcm_enc_open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state) {
    if (!LND_IoCanSeek(io) || params->channels > 2 || params->bitrate_kbps || (params->flags & LND_OUTPUT_STREAMING)) return LND_ERR_UNSUPPORTED;
    uint32_t ch = params->channels, tag = 17;
    char variant[16];
    if (lnd_encoder_option(params->options, "variant", variant, sizeof variant)) {
        if (!strcmp(variant, "ms")) tag = 2;
        else if (strcmp(variant, "ima")) return LND_ERR_INVALID_ARG;
    }
    uint32_t samples = params->frame_size_frames;
    uint64_t block = 256 * ch;
    if (samples) {
        if (tag == 17) {
            if (samples < 9 || (samples - 1) % 8) return LND_ERR_INVALID_ARG;
            block = (uint64_t)(samples - 1) / 2 * ch + 4 * ch;
        } else {
            if (samples < 2 || ((samples - 2) * (uint64_t)ch) % 2) return LND_ERR_INVALID_ARG;
            block = (uint64_t)(samples - 2) * ch / 2 + 7 * ch;
        }
    } else samples = lnd_adpcm_block_frames(tag, ch, (uint32_t)block);
    if (block > UINT16_MAX || samples > UINT16_MAX || (uint64_t)params->sample_rate_hz * block / samples > UINT32_MAX) return LND_ERR_INVALID_ARG;
    lnd_adpcm_encoder *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->tag = tag;
    s->channels = ch;
    s->block = (uint32_t)block;
    s->samples = samples;
    s->pcm = lnd_alloc((size_t)samples * ch * sizeof(int16_t));
    s->encoded = lnd_alloc((size_t)block);
    if (!s->pcm || !s->encoded) {
        lnd_adpcm_enc_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    uint8_t header[90] = {0};
    uint32_t fmt_size = tag == 17 ? 20 : 50;
    memcpy(header, "RIFF", 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    lnd_adpcm_put32(header + 16, fmt_size);
    lnd_adpcm_put16(header + 20, (uint16_t)tag);
    lnd_adpcm_put16(header + 22, (uint16_t)ch);
    lnd_adpcm_put32(header + 24, params->sample_rate_hz);
    lnd_adpcm_put32(header + 28, (uint32_t)((uint64_t)params->sample_rate_hz * block / samples));
    lnd_adpcm_put16(header + 32, (uint16_t)block);
    lnd_adpcm_put16(header + 34, 4);
    lnd_adpcm_put16(header + 36, (uint16_t)(fmt_size - 18));
    lnd_adpcm_put16(header + 38, (uint16_t)samples);
    if (tag == 2) {
        lnd_adpcm_put16(header + 40, 7);
        for (uint32_t i = 0; i < 7; i++) {
            lnd_adpcm_put16(header + 42 + 4 * i, (uint16_t)lnd_adpcm_coefficients[i][0]);
            lnd_adpcm_put16(header + 44 + 4 * i, (uint16_t)lnd_adpcm_coefficients[i][1]);
        }
    }
    uint32_t at = 20 + fmt_size;
    memcpy(header + at, "fact", 4);
    lnd_adpcm_put32(header + at + 4, 4);
    memcpy(header + at + 12, "data", 4);
    uint32_t size = at + 20;
    if (LND_IoWrite(io, header, size) != size) {
        lnd_adpcm_enc_close(s);
        return LND_ERR_IO;
    }
    s->header_size = size;
    *state = s;
    return LND_OK;
}

static int32_t lnd_adpcm_enc_write(void *state, const float *pcm, uint64_t frames) {
    lnd_adpcm_encoder *s = state;
    if (frames > UINT32_MAX - s->frames) return LND_ERR_UNSUPPORTED;
    for (uint64_t done = 0; done < frames;) {
        uint32_t n = (uint32_t)LND_MIN(frames - done, s->samples - s->filled);
        lnd_pcm_from_f32(LND_FORMAT_S16, pcm + done * s->channels, s->pcm + s->filled * s->channels, (size_t)n * s->channels);
        s->filled += n;
        s->frames += n;
        done += n;
        if (s->filled == s->samples) {
            int32_t r = lnd_adpcm_emit(s);
            if (r != LND_OK) return r;
        }
    }
    return LND_OK;
}

const LND_ENCODER lnd_encoder_adpcm = {.name = "adpcm",
                                       .extensions = "wav;wave",
                                       .flags = LND_ENCODER_FLAG_FALLBACK | LND_ENCODER_FLAG_SEEK | LND_ENCODER_FLAG_FRAME_SIZE,
                                       .open = lnd_adpcm_enc_open,
                                       .write = lnd_adpcm_enc_write,
                                       .close = lnd_adpcm_enc_close};
