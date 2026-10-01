#include "src/config.h"
#include "simd.h"
#include "pcm/pcm_float/convert.h"
#include "lindar.h"

#include <math.h>
#include <string.h>

static void lnd_accumulate_c(float *restrict dst, const float *restrict src, float gain, size_t n) {
    for (size_t i = 0; i < n; i++)
        dst[i] += src[i] * gain;
}

static void lnd_scale_c(float *dst, float gain, size_t n) {
    for (size_t i = 0; i < n; i++)
        dst[i] *= gain;
}

static void lnd_clip_hard_c(float *dst, size_t n) {
    for (size_t i = 0; i < n; i++)
        dst[i] = dst[i] < -1.0f ? -1.0f : dst[i] > 1.0f ? 1.0f : dst[i];
}

static void lnd_clip_soft_c(float *dst, size_t n) {
    for (size_t i = 0; i < n; i++) {
        float x = dst[i], a = fabsf(x);
        if (a > 0.8f) dst[i] = copysignf(0.8f + 0.2f * tanhf((a - 0.8f) * 5.0f), x);
    }
}

static void lnd_s16_f32_c(const int16_t *src, float *dst, size_t n) {
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    const uint16_t endian = 1;
    for (size_t i = 0; i < n; i++) {
        int16_t v;
        memcpy(&v, s + i * 2, 2);
        if (!*(const uint8_t *)&endian) v = (int16_t)((uint16_t)s[i * 2] | (uint16_t)s[i * 2 + 1] << 8);
        float f = v * (1.0f / 32768.0f);
        memcpy(d + i * 4, &f, 4);
    }
}

static void lnd_f32_s16_c(const float *src, int16_t *dst, size_t n) {
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) {
        float f;
        memcpy(&f, s + i * 4, 4);
        int16_t out = lnd_pcm_s16(f);
        d[i * 2] = (uint8_t)out;
        d[i * 2 + 1] = (uint8_t)((uint16_t)out >> 8);
    }
}

static void lnd_stereo_split_c(const void *input, void *left, void *right, size_t frames, size_t bytes) {
    const uint8_t *src = input;
    for (size_t f = 0; f < frames; f++) {
        memcpy((uint8_t *)left + f * bytes, src + f * bytes * 2, bytes);
        memcpy((uint8_t *)right + f * bytes, src + (f * 2 + 1) * bytes, bytes);
    }
}

static void lnd_stereo_join_c(const void *left, const void *right, void *output, size_t frames, size_t bytes) {
    uint8_t *dst = output;
    for (size_t f = 0; f < frames; f++) {
        memcpy(dst + f * bytes * 2, (const uint8_t *)left + f * bytes, bytes);
        memcpy(dst + (f * 2 + 1) * bytes, (const uint8_t *)right + f * bytes, bytes);
    }
}

static void lnd_matrix_stereo_c(const float *restrict src, float *restrict dst, size_t frames, const float *m) {
    for (size_t f = 0; f < frames; f++) {
        float l = src[f * 2], r = src[f * 2 + 1];
        dst[f * 2] = l * m[0] + r * m[1];
        dst[f * 2 + 1] = l * m[2] + r * m[3];
    }
}

#define LND_C_OPS                                                                                                                                              \
    {.accumulate = lnd_accumulate_c,                                                                                                                           \
     .scale = lnd_scale_c,                                                                                                                                     \
     .clip_hard = lnd_clip_hard_c,                                                                                                                             \
     .clip_soft = lnd_clip_soft_c,                                                                                                                             \
     .s16_to_f32 = lnd_s16_f32_c,                                                                                                                              \
     .f32_to_s16 = lnd_f32_s16_c,                                                                                                                              \
     .split_stereo = lnd_stereo_split_c,                                                                                                                       \
     .join_stereo = lnd_stereo_join_c,                                                                                                                         \
     .matrix_stereo = lnd_matrix_stereo_c,                                                                                                                     \
     .sinc = lnd_sinc_dot_c,                                                                                                                                   \
     .sinc_select = lnd_sinc_select_c,                                                                                                                         \
     .sinc_frame_select = lnd_sinc_frame_select_c,                                                                                                             \
     .name = "c",                                                                                                                                              \
     .sinc_name = "c"}

lnd_simd_ops lnd_simd = LND_C_OPS;
const lnd_simd_ops lnd_simd_c = LND_C_OPS;

void lnd_simd_init(int32_t mode) {
    lnd_simd = lnd_simd_c;
#if LND_MODULE_SIMD
    if (mode != LND_SIMD_NONE) {
        lnd_simd_x86(mode);
        lnd_simd_neon(mode);
        lnd_simd_biquad_init();
    }
#else
    LND_UNUSED(mode);
#endif
}

int32_t lnd_pcm_init(void) {
    lnd_simd_init((int32_t)lnd_cfg_u32(LND_CFG_AUDIO_SIMD));
    return LND_OK;
}
