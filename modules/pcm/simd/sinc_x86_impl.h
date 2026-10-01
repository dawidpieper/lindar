#include "pcm/audio/sinc_tree.h"
#include <immintrin.h>
#include <limits.h>

#define LND_JOIN_(A, B) A##B
#define LND_JOIN(A, B) LND_JOIN_(A, B)
#define LND_FN(N) LND_JOIN(LND_PREFIX, N)

#if LND_WIDTH != 16
LND_TARGET static __m128 load4(const float *src, size_t stride, bool odd, unsigned shape) {
    if (shape == 1) return _mm_loadu_ps(src);
    if (shape == 2) {
        const float *p = src - odd;
        __m128 a = _mm_loadu_ps(p), b = _mm_loadu_ps(p + 4);
        return odd ? _mm_shuffle_ps(a, b, 0xdd) : _mm_shuffle_ps(a, b, 0x88);
    }
    return _mm_set_ps(src[stride * 3], src[stride * 2], src[stride], src[0]);
}

#endif

LND_TARGET static LND_VECTOR load(const float *src, size_t stride, bool last, unsigned shape) {
#if LND_WIDTH == 4
    return load4(src, stride, last, shape);
#elif LND_WIDTH == 8
    if (shape == 1) return _mm256_loadu_ps(src);
    __m128 a = load4(src, stride, false, shape), b = load4(src + stride * 4, stride, last, shape);
    return _mm256_insertf128_ps(_mm256_castps128_ps256(a), b, 1);
#else
    if (shape == 1) return _mm512_loadu_ps(src);
    if (shape == 2) {
        __m512 a = _mm512_loadu_ps(src), b = _mm512_loadu_ps(src + 15);
        __m512i index = _mm512_set_epi32(31, 29, 27, 25, 23, 21, 19, 17, 14, 12, 10, 8, 6, 4, 2, 0);
        return _mm512_permutex2var_ps(a, index, b);
    }
    int s = (int)stride;
    __m512i index = _mm512_set_epi32(15 * s, 14 * s, 13 * s, 12 * s, 11 * s, 10 * s, 9 * s, 8 * s, 7 * s, 6 * s, 5 * s, 4 * s, 3 * s, 2 * s, s, 0);
    return _mm512_i32gather_ps(index, src, 4);
#endif
}

LND_TARGET static __m128 fold(LND_VECTOR p) {
#if LND_WIDTH == 16
    __m256 v = _mm256_add_ps(_mm512_castps512_ps256(p), _mm256_castpd_ps(_mm512_extractf64x4_pd(_mm512_castps_pd(p), 1)));
    return _mm_add_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1));
#elif LND_WIDTH == 8
    return _mm_add_ps(_mm256_castps256_ps128(p), _mm256_extractf128_ps(p, 1));
#else
    return p;
#endif
}

#define LND_DOT(T, S)                                                                                                                                          \
    LND_TARGET static float dot_##T##_##S(const float *src, const float *h, uint32_t taps, uint32_t stride) {                                                  \
        LND_UNUSED(taps);                                                                                                                                      \
        size_t step = S ? S : stride;                                                                                                                          \
        if (T < LND_WIDTH || step > INT_MAX / 32) return lnd_sinc_dot_c(src, h, T, stride);                                                                    \
        LND_VECTOR p[(T + LND_WIDTH - 1) / LND_WIDTH];                                                                                                         \
        for (uint32_t i = 0; i < T; i += LND_WIDTH)                                                                                                            \
            p[i / LND_WIDTH] = LND_MUL(load(src + i * step, step, i + LND_WIDTH == T, S), LND_LOAD(h + i));                                                    \
        for (uint32_t n = T / (LND_WIDTH * 2); n; n /= 2)                                                                                                      \
            for (uint32_t i = 0; i < n; i++)                                                                                                                   \
                p[i] = LND_ADD(p[i], p[i + n]);                                                                                                                \
        __m128 x = fold(p[0]);                                                                                                                                 \
        x = _mm_add_ps(x, _mm_movehl_ps(x, x));                                                                                                                \
        return _mm_cvtss_f32(_mm_add_ss(x, _mm_shuffle_ps(x, x, 0x55)));                                                                                       \
    }
LND_SINC_SHAPES(LND_DOT)
#undef LND_DOT

#if LND_WIDTH == 4
#define LND_STEREO_BODY(T)                                                                                                                                     \
    __m128 p[T / 2];                                                                                                                                           \
    for (uint32_t g = 0; g < T / 4; g++) {                                                                                                                     \
        __m128 x = _mm_loadu_ps(h + g * 4);                                                                                                                    \
        p[g * 2] = _mm_mul_ps(_mm_loadu_ps(src + g * 8), _mm_unpacklo_ps(x, x));                                                                               \
        p[g * 2 + 1] = _mm_mul_ps(_mm_loadu_ps(src + g * 8 + 4), _mm_unpackhi_ps(x, x));                                                                       \
    }                                                                                                                                                          \
    for (uint32_t n = T / 4; n; n /= 2)                                                                                                                        \
        for (uint32_t t = 0; t < n; t++)                                                                                                                       \
            p[t] = _mm_add_ps(p[t], p[t + n]);                                                                                                                 \
    __m128 x = p[0];
#else
#define LND_STEREO_BODY(T)                                                                                                                                     \
    __m256 p[T / 4];                                                                                                                                           \
    for (uint32_t g = 0; g < T / 4; g++) {                                                                                                                     \
        __m128 x = _mm_loadu_ps(h + g * 4);                                                                                                                    \
        __m256 weights = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_unpacklo_ps(x, x)), _mm_unpackhi_ps(x, x), 1);                                        \
        p[g] = _mm256_mul_ps(_mm256_loadu_ps(src + g * 8), weights);                                                                                           \
    }                                                                                                                                                          \
    for (uint32_t n = T / 8; n; n /= 2)                                                                                                                        \
        for (uint32_t t = 0; t < n; t++)                                                                                                                       \
            p[t] = _mm256_add_ps(p[t], p[t + n]);                                                                                                              \
    __m128 x = _mm_add_ps(_mm256_castps256_ps128(p[0]), _mm256_extractf128_ps(p[0], 1));
#endif
#define LND_STEREO(T)                                                                                                                                          \
    LND_TARGET static void stereo_##T(const float *src, const float *h, float *dst) {                                                                          \
        LND_STEREO_BODY(T)                                                                                                                                     \
        _mm_storel_epi64((__m128i *)dst, _mm_castps_si128(_mm_add_ps(x, _mm_movehl_ps(x, x))));                                                                \
    }
LND_STEREO(8)
LND_STEREO(16)
LND_STEREO(32)
#undef LND_STEREO
#undef LND_STEREO_BODY

#define LND_X86_LEAF(R, I) p[R] = LND_MUL(LND_LOAD(input + (size_t)(I) * count), LND_SPLAT(h[I]));
#define LND_X86_ADD(R, S) p[R] = LND_ADD(p[R], p[S]);
#define LND_FRAME(T, C)                                                                                                                                        \
    LND_TARGET static void frame_##T##_##C(const float *restrict src, const float *restrict h, float *restrict dst, uint32_t taps, uint32_t channels) {        \
        LND_UNUSED(taps);                                                                                                                                      \
        uint32_t count = C ? C : channels;                                                                                                                     \
        if (count == 2) {                                                                                                                                      \
            stereo_##T(src, h, dst);                                                                                                                           \
            return;                                                                                                                                            \
        }                                                                                                                                                      \
        uint32_t c = 0;                                                                                                                                        \
        for (; count - c >= LND_WIDTH; c += LND_WIDTH) {                                                                                                       \
            const float *input = src + c;                                                                                                                      \
            LND_VECTOR p[6];                                                                                                                                   \
            LND_SINC_TREE(T, LND_X86_LEAF, LND_X86_ADD)                                                                                                        \
            LND_STORE(dst + c, p[0]);                                                                                                                          \
        }                                                                                                                                                      \
        for (; c < count; c++)                                                                                                                                 \
            dst[c] = lnd_sinc_dot_c(src + c, h, T, count);                                                                                                     \
    }
LND_SINC_FRAMES(LND_FRAME)
#undef LND_FRAME
#undef LND_X86_LEAF
#undef LND_X86_ADD

static const lnd_sinc_dot dots[3][5] = LND_SINC_TABLE(dot_);
static const lnd_sinc_frame frames[3][4] = LND_SINC_FRAME_TABLE(frame_);

lnd_sinc_dot LND_FN(select)(uint32_t taps, uint32_t stride) {
#if LND_WIDTH == 16
    if (taps == 8) return lnd_sinc_avx_select(taps, stride);
#endif
    int row = lnd_sinc_row(taps);
    return row < 0 ? lnd_sinc_select_c(taps, stride) : dots[row][lnd_sinc_column(stride)];
}
lnd_sinc_frame LND_FN(frame_select)(uint32_t taps, uint32_t channels) {
#if LND_WIDTH == 16
    if (channels < 16) return lnd_sinc_avx_frame_select(taps, channels);
#elif LND_WIDTH == 8
    if (channels == 4) return lnd_sinc_sse2_frame_select(taps, channels);
#endif
    int row = lnd_sinc_row(taps);
    return row < 0 ? nullptr : frames[row][lnd_sinc_frame_column(channels)];
}
static float dot(const float *src, const float *h, uint32_t taps, uint32_t stride) { return LND_FN(select)(taps, stride)(src, h, taps, stride); }
void LND_FN(init)(void) {
    lnd_simd.sinc = dot;
    lnd_simd.sinc_select = LND_FN(select);
    lnd_simd.sinc_frame_select = LND_FN(frame_select);
    lnd_simd.sinc_name = LND_NAME;
}
