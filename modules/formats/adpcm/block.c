#include "block.h"
#include "io/reader.h"

#include <string.h>

static const int16_t lnd_ima_steps[89] = {
    7,    8,    9,    10,   11,   12,   13,   14,    16,    17,    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,   50,   55,   60,
    66,   73,   80,   88,   97,   107,  118,  130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,  449,  494,  544,
    598,  658,  724,  796,  876,  963,  1060, 1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,  3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t lnd_ima_indices[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const uint16_t lnd_ms_adaptation[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
const int16_t lnd_adpcm_coefficients[7][2] = {{256, 0}, {512, -256}, {0, 0}, {192, 64}, {240, 0}, {460, -208}, {392, -232}};

typedef struct lnd_adpcm_channel {
    int32_t a, b, step;
    int16_t c1, c2;
} lnd_adpcm_channel;

static int16_t lnd_adpcm_clip(int32_t sample) { return (int16_t)(sample < -32768 ? -32768 : sample > 32767 ? 32767 : sample); }

static int16_t lnd_ima_sample(lnd_adpcm_channel *s, uint32_t code) {
    int32_t step = lnd_ima_steps[s->step];
    int32_t delta = (step >> 3) + ((code & 1) ? step >> 2 : 0) + ((code & 2) ? step >> 1 : 0) + ((code & 4) ? step : 0);
    s->a = lnd_adpcm_clip(s->a + ((code & 8) ? -delta : delta));
    s->step += lnd_ima_indices[code & 7];
    s->step = s->step < 0 ? 0 : s->step > 88 ? 88 : s->step;
    return (int16_t)s->a;
}

static int32_t lnd_ms_predict(const lnd_adpcm_channel *s) { return (int32_t)(((int64_t)(s->a * s->c1) + (int64_t)(s->b * s->c2)) / 256); }

static int16_t lnd_ms_sample(lnd_adpcm_channel *s, uint32_t code) {
    int32_t nibble = code & 8 ? (int32_t)code - 16 : (int32_t)code;
    int16_t value = lnd_adpcm_clip(lnd_ms_predict(s) + nibble * s->step);
    s->b = s->a;
    s->a = value;
    s->step = s->step * lnd_ms_adaptation[code] / 256;
    s->step = s->step < 16 ? 16 : s->step > INT32_MAX / 768 ? INT32_MAX / 768 : s->step;
    return value;
}

uint32_t lnd_adpcm_block_frames(uint32_t tag, uint32_t channels, uint32_t bytes) {
    uint32_t header = (tag == 17 ? 4 : 7) * channels;
    if (!channels || bytes < header) return 0;
    if (tag == 17) return 1 + (bytes - header) / (4 * channels) * 8;
    return 2 + (bytes - header) * 2 / channels;
}

bool lnd_adpcm_decode_block(uint32_t tag, uint32_t channels, const uint8_t *block, uint32_t frames, const int16_t (*coef)[2], uint32_t coefficients,
                            int16_t *pcm) {
    lnd_adpcm_channel s[2] = {0};
    if (tag == 17) {
        for (uint32_t c = 0; c < channels; c++) {
            s[c].a = (int16_t)lnd_rd_u16le(block + 4 * c);
            s[c].step = block[4 * c + 2];
            if (s[c].step > 88 || block[4 * c + 3]) return false;
            pcm[c] = (int16_t)s[c].a;
        }
        block += channels * 4;
        for (uint32_t f = 1; f < frames; f += 8) {
            for (uint32_t c = 0; c < channels; c++) {
                for (uint32_t j = 0; j < 8; j++) {
                    uint32_t code = (block[j / 2] >> ((j & 1) * 4)) & 15;
                    if (f + j < frames) pcm[(f + j) * channels + c] = lnd_ima_sample(&s[c], code);
                }
                block += 4;
            }
        }
    } else {
        for (uint32_t c = 0; c < channels; c++) {
            uint32_t predictor = block[c];
            if (predictor >= coefficients) return false;
            s[c].c1 = coef[predictor][0];
            s[c].c2 = coef[predictor][1];
            s[c].step = LND_MAX(16, lnd_rd_u16le(block + channels + 2 * c));
            s[c].a = (int16_t)lnd_rd_u16le(block + 3 * channels + 2 * c);
            s[c].b = (int16_t)lnd_rd_u16le(block + 5 * channels + 2 * c);
            pcm[c] = (int16_t)s[c].b;
            if (frames > 1) pcm[channels + c] = (int16_t)s[c].a;
        }
        block += channels * 7;
        uint32_t count = (frames - 2) * channels;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t code = (block[i / 2] >> ((i & 1) ? 0 : 4)) & 15;
            pcm[2 * channels + i] = lnd_ms_sample(&s[i & (channels - 1)], code);
        }
    }
    return true;
}

#if LND_MODULE_ADPCM_ENCODER
static void lnd_adpcm_put16(uint8_t *p, int32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)((uint32_t)value >> 8);
}

static uint32_t lnd_ima_code(lnd_adpcm_channel *s, int32_t value) {
    int32_t delta = value - s->a;
    uint32_t code = delta < 0 ? 8 : 0;
    if (delta < 0) delta = -delta;
    int32_t step = lnd_ima_steps[s->step];
    for (uint32_t bit = 4; bit; bit >>= 1, step >>= 1) {
        if (delta >= step) {
            code |= bit;
            delta -= step;
        }
    }
    lnd_ima_sample(s, code);
    return code;
}

static uint32_t lnd_ms_code(lnd_adpcm_channel *s, int32_t value) {
    int32_t delta = value - lnd_ms_predict(s);
    int32_t code = (delta + (delta < 0 ? -s->step / 2 : s->step / 2)) / s->step;
    code = code < -8 ? -8 : code > 7 ? 7 : code;
    lnd_ms_sample(s, (uint32_t)code & 15);
    return (uint32_t)code & 15;
}

void lnd_adpcm_encode_block(uint32_t tag, uint32_t channels, const int16_t *pcm, uint32_t frames, uint8_t *block) {
    lnd_adpcm_channel s[2] = {0};
    if (tag == 17) {
        for (uint32_t c = 0; c < channels; c++) {
            int32_t delta = frames > 1 ? pcm[channels + c] - pcm[c] : 0;
            if (delta < 0) delta = -delta;
            while (s[c].step < 88 && lnd_ima_steps[s[c].step] < delta) s[c].step++;
            s[c].a = pcm[c];
            lnd_adpcm_put16(block + 4 * c, s[c].a);
            block[4 * c + 2] = (uint8_t)s[c].step;
            block[4 * c + 3] = 0;
        }
        block += channels * 4;
        for (uint32_t f = 1; f < frames; f += 8) {
            for (uint32_t c = 0; c < channels; c++) {
                for (uint32_t j = 0; j < 8; j += 2) {
                    uint32_t a = lnd_ima_code(&s[c], pcm[(f + j) * channels + c]);
                    uint32_t b = lnd_ima_code(&s[c], pcm[(f + j + 1) * channels + c]);
                    *block++ = (uint8_t)(a | (b << 4));
                }
            }
        }
    } else {
        for (uint32_t c = 0; c < channels; c++) {
            uint64_t best = UINT64_MAX;
            uint32_t predictor = 0;
            int32_t delta = pcm[channels + c] - pcm[c];
            if (delta < 0) delta = -delta;
            delta = LND_MAX(delta, 16);
            for (uint32_t p = 0; p < 7; p++) {
                lnd_adpcm_channel trial = {
                    .a = pcm[channels + c], .b = pcm[c], .step = delta, .c1 = lnd_adpcm_coefficients[p][0], .c2 = lnd_adpcm_coefficients[p][1]};
                uint64_t error = 0;
                for (uint32_t f = 2; f < frames; f++) {
                    lnd_ms_code(&trial, pcm[f * channels + c]);
                    int64_t d = pcm[f * channels + c] - trial.a;
                    error += (uint64_t)(d * d);
                }
                if (error < best) {
                    best = error;
                    predictor = p;
                }
            }
            s[c] = (lnd_adpcm_channel){
                .a = pcm[channels + c], .b = pcm[c], .step = delta, .c1 = lnd_adpcm_coefficients[predictor][0], .c2 = lnd_adpcm_coefficients[predictor][1]};
            block[c] = (uint8_t)predictor;
            lnd_adpcm_put16(block + channels + 2 * c, delta);
            lnd_adpcm_put16(block + 3 * channels + 2 * c, s[c].a);
            lnd_adpcm_put16(block + 5 * channels + 2 * c, s[c].b);
        }
        block += channels * 7;
        uint32_t count = (frames - 2) * channels;
        for (uint32_t i = 0; i < count; i += 2) {
            uint32_t a = lnd_ms_code(&s[i & (channels - 1)], pcm[2 * channels + i]);
            uint32_t b = lnd_ms_code(&s[(i + 1) & (channels - 1)], pcm[2 * channels + i + 1]);
            *block++ = (uint8_t)((a << 4) | b);
        }
    }
}
#endif
