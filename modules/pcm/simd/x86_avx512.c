#include "pcm/audio/simd.h"
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
#include <immintrin.h>
#include <string.h>
#if defined(__GNUC__) || defined(__clang__)
#define LND_TARGET __attribute__((target("avx512f,avx512bw")))
#else
#define LND_TARGET
#endif
LND_TARGET static void accumulate(float *restrict dst, const float *restrict src, float gain, size_t n) {
    __m512 g = _mm512_set1_ps(gain);
    size_t i = 0;
    for (; n - i >= 16; i += 16)
        _mm512_storeu_ps(dst + i, _mm512_add_ps(_mm512_loadu_ps(dst + i), _mm512_mul_ps(_mm512_loadu_ps(src + i), g)));
    lnd_simd_c.accumulate(dst + i, src + i, gain, n - i);
}
LND_TARGET static void scale(float *dst, float gain, size_t n) {
    __m512 g = _mm512_set1_ps(gain);
    size_t i = 0;
    for (; n - i >= 16; i += 16)
        _mm512_storeu_ps(dst + i, _mm512_mul_ps(_mm512_loadu_ps(dst + i), g));
    lnd_simd_c.scale(dst + i, gain, n - i);
}
LND_TARGET static void clip(float *dst, size_t n) {
    __m512 lo = _mm512_set1_ps(-1), hi = _mm512_set1_ps(1);
    size_t i = 0;
    for (; n - i >= 16; i += 16) {
        __m512 x = _mm512_loadu_ps(dst + i);
        __m512 y = _mm512_min_ps(hi, _mm512_max_ps(lo, x));
        _mm512_storeu_ps(dst + i, y);
    }
    lnd_simd_c.clip_hard(dst + i, n - i);
}
LND_TARGET static void matrix(const float *restrict src, float *restrict dst, size_t frames, const float *m) {
    __m512 a = _mm512_set_ps(m[2], m[0], m[2], m[0], m[2], m[0], m[2], m[0], m[2], m[0], m[2], m[0], m[2], m[0], m[2], m[0]);
    __m512 b = _mm512_set_ps(m[3], m[1], m[3], m[1], m[3], m[1], m[3], m[1], m[3], m[1], m[3], m[1], m[3], m[1], m[3], m[1]);
    size_t f = 0;
    for (; frames - f >= 8; f += 8) {
        __m512 x = _mm512_loadu_ps(src + f * 2);
        __m512 l = _mm512_shuffle_ps(x, x, 0xa0), r = _mm512_shuffle_ps(x, x, 0xf5);
        _mm512_storeu_ps(dst + f * 2, _mm512_add_ps(_mm512_mul_ps(l, a), _mm512_mul_ps(r, b)));
    }
    lnd_simd_c.matrix_stereo(src + f * 2, dst + f * 2, frames - f, m);
}
LND_TARGET static void s16_f32(const int16_t *src, float *dst, size_t n) {
    size_t i = 0;
    for (; n - i >= 16; i += 16) {
        __m512i q = _mm512_cvtepi16_epi32(_mm256_loadu_si256((const __m256i *)((const uint8_t *)src + i * 2)));
        _mm512_storeu_ps((float *)((uint8_t *)dst + i * 4), _mm512_mul_ps(_mm512_cvtepi32_ps(q), _mm512_set1_ps(1.0f / 32768)));
    }
    lnd_simd_c.s16_to_f32((const int16_t *)((const uint8_t *)src + i * 2), (float *)((uint8_t *)dst + i * 4), n - i);
}
LND_TARGET static void f32_s16(const float *src, int16_t *dst, size_t n) {
    __m512 half = _mm512_set1_ps(0.5f);
    size_t i = 0;
    for (; n - i >= 16; i += 16) {
        __m512 x = _mm512_loadu_ps((const float *)((const uint8_t *)src + i * 4));
        x = _mm512_maskz_mov_ps(_mm512_cmp_ps_mask(x, x, _CMP_ORD_Q), x);
        x = _mm512_min_ps(_mm512_max_ps(x, _mm512_set1_ps(-1)), _mm512_set1_ps(32767.0f / 32768));
        x = _mm512_mul_ps(x, _mm512_set1_ps(32768));
        __m512i q = _mm512_cvttps_epi32(x);
        __m512 frac = _mm512_sub_ps(x, _mm512_cvtepi32_ps(q));
        q = _mm512_mask_add_epi32(q, _mm512_cmp_ps_mask(frac, half, _CMP_GE_OQ), q, _mm512_set1_epi32(1));
        q = _mm512_mask_sub_epi32(q, _mm512_cmp_ps_mask(frac, _mm512_sub_ps(_mm512_setzero_ps(), half), _CMP_LE_OQ), q, _mm512_set1_epi32(1));
        _mm256_storeu_si256((__m256i *)((uint8_t *)dst + i * 2), _mm512_cvtsepi32_epi16(q));
    }
    lnd_simd_c.f32_to_s16((const float *)((const uint8_t *)src + i * 4), (int16_t *)((uint8_t *)dst + i * 2), n - i);
}
void lnd_simd_use_avx512(void) {
    lnd_simd.accumulate = accumulate;
    lnd_simd.scale = scale;
    lnd_simd.clip_hard = clip;
    lnd_simd.matrix_stereo = matrix;
    lnd_simd.s16_to_f32 = s16_f32;
    lnd_simd.f32_to_s16 = f32_s16;
    lnd_simd.name = "avx512";
}
#endif
