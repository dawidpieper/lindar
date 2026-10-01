#include "convert.h"
#include "simd.h"
#include "lindar.h"

#include <math.h>
#include <string.h>

LND_INLINE float lnd_clip1(float x) { return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x); }

#define LND_KERNEL_TO_F32(NAME, T, EXPR)                                                                                                                       \
    static void NAME(const void *LND_RESTRICT src, float *LND_RESTRICT dst, size_t n) {                                                                        \
        const T *s = src;                                                                                                                                      \
        for (size_t i = 0; i < n; i++) {                                                                                                                       \
            T x = s[i];                                                                                                                                        \
            dst[i] = (EXPR);                                                                                                                                   \
        }                                                                                                                                                      \
    }

#define LND_KERNEL_FROM_F32(NAME, T, EXPR)                                                                                                                     \
    static void NAME(const float *LND_RESTRICT src, void *LND_RESTRICT dst, size_t n) {                                                                        \
        T *d = dst;                                                                                                                                            \
        for (size_t i = 0; i < n; i++) {                                                                                                                       \
            float x = lnd_clip1(src[i]);                                                                                                                       \
            d[i] = (EXPR);                                                                                                                                     \
        }                                                                                                                                                      \
    }

LND_KERNEL_TO_F32(lnd_u8_to_f32, uint8_t, ((float)x - 128.0f) * (1.0f / 128.0f))
LND_KERNEL_TO_F32(lnd_s32_to_f32, int32_t, (float)x * (1.0f / 2147483648.0f))
LND_KERNEL_TO_F32(lnd_f64_to_f32, double, (float)x)

LND_KERNEL_FROM_F32(lnd_f32_to_u8, uint8_t, (uint8_t)LND_MIN(lrintf(x * 128.0f) + 128, 255))
LND_KERNEL_FROM_F32(lnd_f32_to_s32, int32_t, (int32_t)LND_MIN(llrint((double)x * 2147483648.0), 2147483647))

static void lnd_f32_to_f64(const float *LND_RESTRICT src, void *LND_RESTRICT dst, size_t n) {
    double *d = dst;
    for (size_t i = 0; i < n; i++)
        d[i] = (double)src[i];
}

static void lnd_s24_to_f32(const void *LND_RESTRICT src, float *LND_RESTRICT dst, size_t n) {
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++, s += 3) {
        int32_t v = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8;
        dst[i] = (float)v * (1.0f / 8388608.0f);
    }
}

static void lnd_f32_to_s24(const float *LND_RESTRICT src, void *LND_RESTRICT dst, size_t n) {
    uint8_t *d = dst;
    for (size_t i = 0; i < n; i++, d += 3) {
        int32_t v = (int32_t)LND_MIN(lrintf(lnd_clip1(src[i]) * 8388608.0f), 8388607);
        d[0] = (uint8_t)v;
        d[1] = (uint8_t)(v >> 8);
        d[2] = (uint8_t)(v >> 16);
    }
}

void lnd_pcm_to_f32(int32_t format, const void *src, float *dst, size_t samples) {
    switch (format) {
    case LND_FORMAT_U8:
        lnd_u8_to_f32(src, dst, samples);
        break;
    case LND_FORMAT_S16:
        lnd_simd.s16_to_f32(src, dst, samples);
        break;
    case LND_FORMAT_S24:
        lnd_s24_to_f32(src, dst, samples);
        break;
    case LND_FORMAT_S32:
        lnd_s32_to_f32(src, dst, samples);
        break;
    case LND_FORMAT_F32:
        memcpy(dst, src, samples * sizeof(float));
        break;
    case LND_FORMAT_F64:
        lnd_f64_to_f32(src, dst, samples);
        break;
    default:
        memset(dst, 0, samples * sizeof(float));
        break;
    }
}

void lnd_pcm_from_f32(int32_t format, const float *src, void *dst, size_t samples) {
    switch (format) {
    case LND_FORMAT_U8:
        lnd_f32_to_u8(src, dst, samples);
        break;
    case LND_FORMAT_S16:
        lnd_simd.f32_to_s16(src, dst, samples);
        break;
    case LND_FORMAT_S24:
        lnd_f32_to_s24(src, dst, samples);
        break;
    case LND_FORMAT_S32:
        lnd_f32_to_s32(src, dst, samples);
        break;
    case LND_FORMAT_F32:
        memcpy(dst, src, samples * sizeof(float));
        break;
    case LND_FORMAT_F64:
        lnd_f32_to_f64(src, dst, samples);
        break;
    default:
        break;
    }
}
