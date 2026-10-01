#include "pcm/audio/simd.h"
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
#include <immintrin.h>
#include <string.h>
#if defined(__GNUC__) || defined(__clang__)
#define LND_TARGET __attribute__((target("avx2")))
#else
#define LND_TARGET
#endif
LND_TARGET static void s16_f32(const int16_t *src, float *dst, size_t n) {
    size_t i = 0;
    for (; n - i >= 8; i += 8) {
        __m256i q = _mm256_cvtepi16_epi32(_mm_loadu_si128((const __m128i *)((const uint8_t *)src + i * 2)));
        _mm256_storeu_ps((float *)((uint8_t *)dst + i * 4), _mm256_mul_ps(_mm256_cvtepi32_ps(q), _mm256_set1_ps(1.0f / 32768)));
    }
    lnd_simd_c.s16_to_f32((const int16_t *)((const uint8_t *)src + i * 2), (float *)((uint8_t *)dst + i * 4), n - i);
}
LND_TARGET static void f32_s16(const float *src, int16_t *dst, size_t n) {
    __m256 half = _mm256_set1_ps(0.5f);
    size_t i = 0;
    for (; n - i >= 8; i += 8) {
        __m256 x = _mm256_loadu_ps((const float *)((const uint8_t *)src + i * 4));
        x = _mm256_and_ps(x, _mm256_cmp_ps(x, x, _CMP_ORD_Q));
        x = _mm256_min_ps(_mm256_max_ps(x, _mm256_set1_ps(-1)), _mm256_set1_ps(32767.0f / 32768));
        x = _mm256_mul_ps(x, _mm256_set1_ps(32768));
        __m256i q = _mm256_cvttps_epi32(x);
        __m256 frac = _mm256_sub_ps(x, _mm256_cvtepi32_ps(q));
        q = _mm256_sub_epi32(q, _mm256_castps_si256(_mm256_cmp_ps(frac, half, _CMP_GE_OQ)));
        q = _mm256_add_epi32(q, _mm256_castps_si256(_mm256_cmp_ps(frac, _mm256_sub_ps(_mm256_setzero_ps(), half), _CMP_LE_OQ)));
        __m128i packed = _mm_packs_epi32(_mm256_castsi256_si128(q), _mm256_extracti128_si256(q, 1));
        _mm_storeu_si128((__m128i *)((uint8_t *)dst + i * 2), packed);
    }
    lnd_simd_c.f32_to_s16((const float *)((const uint8_t *)src + i * 4), (int16_t *)((uint8_t *)dst + i * 2), n - i);
}
void lnd_simd_use_avx2(void) {
    lnd_simd.s16_to_f32 = s16_f32;
    lnd_simd.f32_to_s16 = f32_s16;
    lnd_simd.name = "avx2";
}
#endif
