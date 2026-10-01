#include "pcm/audio/simd.h"
#include "pcm/audio/cpu.h"
#include "sinc_x86.h"

#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
#include <immintrin.h>
#if defined(__GNUC__) || defined(__clang__)
#define LND_SSE2 __attribute__((target("sse2")))
#else
#define LND_SSE2
#endif
void lnd_simd_use_sse2(void);
void lnd_simd_use_avx(void);
void lnd_simd_use_avx2(void);
void lnd_simd_use_avx512(void);

LND_SSE2 static void split(const void *input, void *left, void *right, size_t frames, size_t bytes) {
    const uint8_t *src = input;
    uint8_t *l = left, *r = right;
    size_t f = 0;
    if (bytes == 4) {
        for (; frames - f >= 4; f += 4) {
            __m128 a = _mm_loadu_ps((const float *)(src + f * 8)), b = _mm_loadu_ps((const float *)(src + f * 8 + 16));
            _mm_storeu_ps((float *)(l + f * 4), _mm_shuffle_ps(a, b, 0x88));
            _mm_storeu_ps((float *)(r + f * 4), _mm_shuffle_ps(a, b, 0xdd));
        }
    } else if (bytes == 2) {
        for (; frames - f >= 4; f += 4) {
            __m128i x = _mm_loadu_si128((const __m128i *)(src + f * 4));
            __m128i a = _mm_srai_epi32(_mm_slli_epi32(x, 16), 16), b = _mm_srai_epi32(x, 16);
            _mm_storel_epi64((__m128i *)(l + f * 2), _mm_packs_epi32(a, a));
            _mm_storel_epi64((__m128i *)(r + f * 2), _mm_packs_epi32(b, b));
        }
    }
    lnd_simd_c.split_stereo(src + f * bytes * 2, l + f * bytes, r + f * bytes, frames - f, bytes);
}

LND_SSE2 static void join(const void *left, const void *right, void *output, size_t frames, size_t bytes) {
    const uint8_t *l = left, *r = right;
    uint8_t *dst = output;
    size_t f = 0;
    if (bytes == 4) {
        for (; frames - f >= 4; f += 4) {
            __m128 a = _mm_loadu_ps((const float *)(l + f * 4)), b = _mm_loadu_ps((const float *)(r + f * 4));
            _mm_storeu_ps((float *)(dst + f * 8), _mm_unpacklo_ps(a, b));
            _mm_storeu_ps((float *)(dst + f * 8 + 16), _mm_unpackhi_ps(a, b));
        }
    } else if (bytes == 2) {
        for (; frames - f >= 4; f += 4) {
            __m128i a = _mm_loadl_epi64((const __m128i *)(l + f * 2)), b = _mm_loadl_epi64((const __m128i *)(r + f * 2));
            _mm_storeu_si128((__m128i *)(dst + f * 4), _mm_unpacklo_epi16(a, b));
        }
    }
    lnd_simd_c.join_stereo(l + f * bytes, r + f * bytes, dst + f * bytes * 2, frames - f, bytes);
}
#endif

void lnd_simd_x86(int32_t mode) {
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
    int32_t level = lnd_x86_level(mode);
    if (level == LND_SIMD_NONE) return;
    lnd_simd_use_sse2();
    lnd_sinc_sse2_init();
    lnd_simd.split_stereo = split;
    lnd_simd.join_stereo = join;
    if (level >= LND_SIMD_AVX) {
        lnd_simd_use_avx();
        lnd_sinc_avx_init();
    }
    if (level >= LND_SIMD_AVX2) lnd_simd_use_avx2();
    if (level >= LND_SIMD_AVX512) {
        lnd_simd_use_avx512();
        lnd_sinc_avx512_init();
    }
#else
    LND_UNUSED(mode);
#endif
}
