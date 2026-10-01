#include "pcm/audio/simd.h"
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
#include <immintrin.h>
#include <string.h>
#if defined(__GNUC__) || defined(__clang__)
#define LND_TARGET __attribute__((target("avx")))
#else
#define LND_TARGET
#endif
LND_TARGET static void accumulate(float *restrict dst, const float *restrict src, float gain, size_t n) {
    __m256 g = _mm256_set1_ps(gain);
    size_t i = 0;
    for (; n - i >= 8; i += 8)
        _mm256_storeu_ps(dst + i, _mm256_add_ps(_mm256_loadu_ps(dst + i), _mm256_mul_ps(_mm256_loadu_ps(src + i), g)));
    lnd_simd_c.accumulate(dst + i, src + i, gain, n - i);
}
LND_TARGET static void scale(float *dst, float gain, size_t n) {
    __m256 g = _mm256_set1_ps(gain);
    size_t i = 0;
    for (; n - i >= 8; i += 8)
        _mm256_storeu_ps(dst + i, _mm256_mul_ps(_mm256_loadu_ps(dst + i), g));
    lnd_simd_c.scale(dst + i, gain, n - i);
}
LND_TARGET static void clip(float *dst, size_t n) {
    __m256 lo = _mm256_set1_ps(-1), hi = _mm256_set1_ps(1);
    size_t i = 0;
    for (; n - i >= 8; i += 8) {
        __m256 x = _mm256_loadu_ps(dst + i);
        __m256 y = _mm256_min_ps(hi, _mm256_max_ps(lo, x));
        _mm256_storeu_ps(dst + i, y);
    }
    lnd_simd_c.clip_hard(dst + i, n - i);
}
LND_TARGET static void matrix(const float *restrict src, float *restrict dst, size_t frames, const float *m) {
    __m256 a = _mm256_set_ps(m[2], m[0], m[2], m[0], m[2], m[0], m[2], m[0]);
    __m256 b = _mm256_set_ps(m[3], m[1], m[3], m[1], m[3], m[1], m[3], m[1]);
    size_t f = 0;
    for (; frames - f >= 4; f += 4) {
        __m256 x = _mm256_loadu_ps(src + f * 2);
        __m256 l = _mm256_shuffle_ps(x, x, 0xa0), r = _mm256_shuffle_ps(x, x, 0xf5);
        _mm256_storeu_ps(dst + f * 2, _mm256_add_ps(_mm256_mul_ps(l, a), _mm256_mul_ps(r, b)));
    }
    lnd_simd_c.matrix_stereo(src + f * 2, dst + f * 2, frames - f, m);
}
void lnd_simd_use_avx(void) {
    lnd_simd.accumulate = accumulate;
    lnd_simd.scale = scale;
    lnd_simd.clip_hard = clip;
    lnd_simd.matrix_stereo = matrix;
    lnd_simd.name = "avx";
}
#endif
