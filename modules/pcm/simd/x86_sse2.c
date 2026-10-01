#include "pcm/audio/simd.h"
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
#include <immintrin.h>
#include <string.h>
#if defined(__GNUC__) || defined(__clang__)
#define LND_TARGET __attribute__((target("sse2")))
#else
#define LND_TARGET
#endif
LND_TARGET static void accumulate(float *restrict dst, const float *restrict src, float gain, size_t n) {
    __m128 g = _mm_set1_ps(gain);
    size_t i = 0;
    for (; n - i >= 4; i += 4)
        _mm_storeu_ps(dst + i, _mm_add_ps(_mm_loadu_ps(dst + i), _mm_mul_ps(_mm_loadu_ps(src + i), g)));
    lnd_simd_c.accumulate(dst + i, src + i, gain, n - i);
}
LND_TARGET static void scale(float *dst, float gain, size_t n) {
    __m128 g = _mm_set1_ps(gain);
    size_t i = 0;
    for (; n - i >= 4; i += 4)
        _mm_storeu_ps(dst + i, _mm_mul_ps(_mm_loadu_ps(dst + i), g));
    lnd_simd_c.scale(dst + i, gain, n - i);
}
LND_TARGET static void clip(float *dst, size_t n) {
    __m128 lo = _mm_set1_ps(-1), hi = _mm_set1_ps(1);
    size_t i = 0;
    for (; n - i >= 4; i += 4) {
        __m128 x = _mm_loadu_ps(dst + i);
        __m128 y = _mm_min_ps(hi, _mm_max_ps(lo, x));
        _mm_storeu_ps(dst + i, y);
    }
    lnd_simd_c.clip_hard(dst + i, n - i);
}
LND_TARGET static void matrix(const float *restrict src, float *restrict dst, size_t frames, const float *m) {
    __m128 a = _mm_set_ps(m[2], m[0], m[2], m[0]);
    __m128 b = _mm_set_ps(m[3], m[1], m[3], m[1]);
    size_t f = 0;
    for (; frames - f >= 2; f += 2) {
        __m128 x = _mm_loadu_ps(src + f * 2);
        __m128 l = _mm_shuffle_ps(x, x, 0xa0), r = _mm_shuffle_ps(x, x, 0xf5);
        _mm_storeu_ps(dst + f * 2, _mm_add_ps(_mm_mul_ps(l, a), _mm_mul_ps(r, b)));
    }
    lnd_simd_c.matrix_stereo(src + f * 2, dst + f * 2, frames - f, m);
}
LND_TARGET static void s16_f32(const int16_t *src, float *dst, size_t n) {
    size_t i = 0;
    for (; n - i >= 4; i += 4) {
        __m128i x = _mm_loadl_epi64((const __m128i *)((const uint8_t *)src + i * 2));
        __m128i q = _mm_unpacklo_epi16(x, _mm_srai_epi16(x, 15));
        _mm_storeu_ps((float *)((uint8_t *)dst + i * 4), _mm_mul_ps(_mm_cvtepi32_ps(q), _mm_set1_ps(1.0f / 32768)));
    }
    lnd_simd_c.s16_to_f32((const int16_t *)((const uint8_t *)src + i * 2), (float *)((uint8_t *)dst + i * 4), n - i);
}
LND_TARGET static void f32_s16(const float *src, int16_t *dst, size_t n) {
    __m128 half = _mm_set1_ps(0.5f);
    size_t i = 0;
    for (; n - i >= 4; i += 4) {
        __m128 x = _mm_loadu_ps((const float *)((const uint8_t *)src + i * 4));
        x = _mm_and_ps(x, _mm_cmpord_ps(x, x));
        x = _mm_min_ps(_mm_max_ps(x, _mm_set1_ps(-1)), _mm_set1_ps(32767.0f / 32768));
        x = _mm_mul_ps(x, _mm_set1_ps(32768));
        __m128i q = _mm_cvttps_epi32(x);
        __m128 frac = _mm_sub_ps(x, _mm_cvtepi32_ps(q));
        q = _mm_sub_epi32(q, _mm_castps_si128(_mm_cmpge_ps(frac, half)));
        q = _mm_add_epi32(q, _mm_castps_si128(_mm_cmple_ps(frac, _mm_sub_ps(_mm_setzero_ps(), half))));
        _mm_storel_epi64((__m128i *)((uint8_t *)dst + i * 2), _mm_packs_epi32(q, q));
    }
    lnd_simd_c.f32_to_s16((const float *)((const uint8_t *)src + i * 4), (int16_t *)((uint8_t *)dst + i * 2), n - i);
}
void lnd_simd_use_sse2(void) {
    lnd_simd.accumulate = accumulate;
    lnd_simd.scale = scale;
    lnd_simd.clip_hard = clip;
    lnd_simd.matrix_stereo = matrix;
    lnd_simd.s16_to_f32 = s16_f32;
    lnd_simd.f32_to_s16 = f32_s16;
    lnd_simd.name = "sse2";
}
#endif
