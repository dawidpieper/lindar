#include "pcm/audio/simd.h"
#include "pcm/audio/sinc_tree.h"
#if defined(LND_ARCH_ARM64) && (defined(__ARM_NEON) || defined(_M_ARM64))
#if defined(LND_COMPILER_MSVC)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif

#define LND_DOT(T, S)                                                                                                                                          \
    static float dot_##T##_##S(const float *src, const float *h, uint32_t taps, uint32_t stride) {                                                             \
        LND_UNUSED(taps);                                                                                                                                      \
        size_t step = S ? S : stride;                                                                                                                          \
        float32x4_t p[T / 4];                                                                                                                                  \
        for (uint32_t i = 0; i < T; i += 4) {                                                                                                                  \
            const float *s = src + i * step;                                                                                                                   \
            float32x4_t x;                                                                                                                                     \
            if (S == 1)                                                                                                                                        \
                x = vld1q_f32(s);                                                                                                                              \
            else if (S == 2) {                                                                                                                                 \
                float32x4x2_t pair = vld2q_f32(s - (i == T - 4));                                                                                              \
                x = pair.val[i == T - 4];                                                                                                                      \
            } else {                                                                                                                                           \
                float lanes[] = {s[0], s[step], s[step * 2], s[step * 3]};                                                                                     \
                x = vld1q_f32(lanes);                                                                                                                          \
            }                                                                                                                                                  \
            p[i / 4] = vmulq_f32(x, vld1q_f32(h + i));                                                                                                         \
        }                                                                                                                                                      \
        for (uint32_t n = T / 8; n; n /= 2)                                                                                                                    \
            for (uint32_t i = 0; i < n; i++)                                                                                                                   \
                p[i] = vaddq_f32(p[i], p[i + n]);                                                                                                              \
        return vpadds_f32(vadd_f32(vget_low_f32(p[0]), vget_high_f32(p[0])));                                                                                  \
    }
LND_SINC_SHAPES(LND_DOT)
#undef LND_DOT

static void stereo16(const float *src, const float *h, float *dst) {
    float32x2_t p[16];
    for (uint32_t i = 0; i < 16; i += 4) {
        float32x4_t coeff = vld1q_f32(h + i);
        p[i] = vmul_laneq_f32(vld1_f32(src + i * 2), coeff, 0);
        p[i + 1] = vmul_laneq_f32(vld1_f32(src + i * 2 + 2), coeff, 1);
        p[i + 2] = vmul_laneq_f32(vld1_f32(src + i * 2 + 4), coeff, 2);
        p[i + 3] = vmul_laneq_f32(vld1_f32(src + i * 2 + 6), coeff, 3);
    }
    for (uint32_t n = 8; n; n /= 2)
        for (uint32_t i = 0; i < n; i++)
            p[i] = vadd_f32(p[i], p[i + n]);
    vst1_f32(dst, p[0]);
}

#define LND_NEON_LEAF(R, I)                                                                                                                                    \
    for (uint32_t b = 0; b < blocks; b++)                                                                                                                      \
        p[R][b] = vmulq_laneq_f32(vld1q_f32(input + (size_t)(I) * count + b * 4), coeff[(I) / 4], (I) % 4);
#define LND_NEON_ADD(R, S)                                                                                                                                     \
    for (uint32_t b = 0; b < blocks; b++)                                                                                                                      \
        p[R][b] = vaddq_f32(p[R][b], p[S][b]);
#define LND_FRAME(T, C)                                                                                                                                        \
    static void frame_##T##_##C(const float *restrict src, const float *restrict h, float *restrict dst, uint32_t taps, uint32_t channels) {                   \
        LND_UNUSED(taps);                                                                                                                                      \
        uint32_t count = C ? C : channels;                                                                                                                     \
        if (T == 16 && count == 2) {                                                                                                                           \
            stereo16(src, h, dst);                                                                                                                             \
            return;                                                                                                                                            \
        }                                                                                                                                                      \
        if (count == 2) {                                                                                                                                      \
            float32x4_t p[T / 2];                                                                                                                              \
            for (uint32_t g = 0; g < T / 4; g++) {                                                                                                             \
                float32x4_t coeff = vld1q_f32(h + g * 4);                                                                                                      \
                p[g * 2] = vmulq_f32(vld1q_f32(src + g * 8), vzip1q_f32(coeff, coeff));                                                                        \
                p[g * 2 + 1] = vmulq_f32(vld1q_f32(src + g * 8 + 4), vzip2q_f32(coeff, coeff));                                                                \
            }                                                                                                                                                  \
            for (uint32_t n = T / 4; n; n /= 2)                                                                                                                \
                for (uint32_t t = 0; t < n; t++)                                                                                                               \
                    p[t] = vaddq_f32(p[t], p[t + n]);                                                                                                          \
            vst1_f32(dst, vadd_f32(vget_low_f32(p[0]), vget_high_f32(p[0])));                                                                                  \
            return;                                                                                                                                            \
        }                                                                                                                                                      \
        enum { blocks = C == 8 ? 2 : 1 };                                                                                                                      \
        float32x4_t coeff[T / 4];                                                                                                                              \
        for (uint32_t i = 0; i < T / 4; i++)                                                                                                                   \
            coeff[i] = vld1q_f32(h + i * 4);                                                                                                                   \
        uint32_t c = 0;                                                                                                                                        \
        for (; count - c >= blocks * 4; c += blocks * 4) {                                                                                                     \
            const float *input = src + c;                                                                                                                      \
            float32x4_t p[6][blocks];                                                                                                                          \
            LND_SINC_TREE(T, LND_NEON_LEAF, LND_NEON_ADD)                                                                                                      \
            for (uint32_t b = 0; b < blocks; b++)                                                                                                              \
                vst1q_f32(dst + c + b * 4, p[0][b]);                                                                                                           \
        }                                                                                                                                                      \
        for (; c < count; c++)                                                                                                                                 \
            dst[c] = lnd_sinc_dot_c(src + c, h, T, count);                                                                                                     \
    }
LND_SINC_FRAMES(LND_FRAME)
#undef LND_FRAME
#undef LND_NEON_LEAF
#undef LND_NEON_ADD

static const lnd_sinc_dot dots[3][5] = LND_SINC_TABLE(dot_);
static const lnd_sinc_frame frames[3][4] = LND_SINC_FRAME_TABLE(frame_);
static lnd_sinc_dot select_dot(uint32_t taps, uint32_t stride) {
    int row = lnd_sinc_row(taps);
    return row < 0 ? lnd_sinc_dot_c : dots[row][lnd_sinc_column(stride)];
}
static lnd_sinc_frame select_frame(uint32_t taps, uint32_t channels) {
    int row = lnd_sinc_row(taps);
    return row < 0 ? nullptr : frames[row][lnd_sinc_frame_column(channels)];
}
static float dot(const float *src, const float *h, uint32_t taps, uint32_t stride) { return select_dot(taps, stride)(src, h, taps, stride); }
#endif

void lnd_sinc_neon_init(void) {
#if defined(LND_ARCH_ARM64) && (defined(__ARM_NEON) || defined(_M_ARM64))
    lnd_simd.sinc = dot;
    lnd_simd.sinc_select = select_dot;
    lnd_simd.sinc_frame_select = select_frame;
    lnd_simd.sinc_name = "neon";
#endif
}
