#include "sinc.h"
#include "sinc_tree.h"

#define LND_DOT(T, S)                                                                                                                                          \
    static float dot_##T##_##S(const float *src, const float *h, uint32_t taps, uint32_t stride) {                                                             \
        LND_UNUSED(taps);                                                                                                                                      \
        size_t step = S ? S : stride;                                                                                                                          \
        float p[T];                                                                                                                                            \
        for (uint32_t i = 0; i < T; i++)                                                                                                                       \
            p[i] = src[i * step] * h[i];                                                                                                                       \
        for (uint32_t n = T / 2; n; n /= 2)                                                                                                                    \
            for (uint32_t i = 0; i < n; i++)                                                                                                                   \
                p[i] += p[i + n];                                                                                                                              \
        return p[0];                                                                                                                                           \
    }
LND_SINC_SHAPES(LND_DOT)
#undef LND_DOT

#define LND_STEREO_TREE_8(LEAF, ADD) LND_SINC_TREE_4(LEAF, ADD)
#define LND_STEREO_TREE_16(LEAF, ADD) LND_SINC_TREE_8(LEAF, ADD)
#define LND_STEREO_TREE_32(LEAF, ADD) LND_SINC_TREE_16(LEAF, ADD)
#define LND_STEREO_LEAF(R, I)                                                                                                                                  \
    for (uint32_t c = 0; c < 4; c++)                                                                                                                           \
        stereo[R][c] = src[(I) * 4 + c] * h[(I) * 2 + c / 2];
#define LND_STEREO_ADD(R, S)                                                                                                                                   \
    for (uint32_t c = 0; c < 4; c++)                                                                                                                           \
        stereo[R][c] += stereo[S][c];
#define LND_C_LEAF(R, I) p[R] = src[(size_t)(I) * count + c] * h[I];
#define LND_C_ADD(R, S) p[R] += p[S];
static LND_NOINLINE void frame_32_block(const float *restrict src, const float *restrict h, float *restrict dst) {
    enum { count = 8 };
    for (uint32_t c = 0; c < 4; c++) {
        float p[6];
        LND_SINC_TREE(32, LND_C_LEAF, LND_C_ADD)
        dst[c] = p[0];
    }
}

#define LND_FRAME(T, CHANNELS)                                                                                                                                 \
    static void frame_##T##_##CHANNELS(const float *restrict src, const float *restrict h, float *restrict dst, uint32_t taps, uint32_t channels) {            \
        LND_UNUSED(taps);                                                                                                                                      \
        enum { C = CHANNELS };                                                                                                                                 \
        uint32_t count = C ? C : channels;                                                                                                                     \
        if (C == 2) {                                                                                                                                          \
            float stereo[5][4];                                                                                                                                \
            LND_STEREO_TREE_##T(LND_STEREO_LEAF, LND_STEREO_ADD);                                                                                              \
            dst[0] = stereo[0][0] + stereo[0][2];                                                                                                              \
            dst[1] = stereo[0][1] + stereo[0][3];                                                                                                              \
        } else if (T == 32 && C == 8) {                                                                                                                        \
            frame_32_block(src, h, dst);                                                                                                                       \
            frame_32_block(src + 4, h, dst + 4);                                                                                                               \
        } else if (C) {                                                                                                                                        \
            for (uint32_t c = 0; c < C; c++) {                                                                                                                 \
                float p[6];                                                                                                                                    \
                LND_SINC_TREE(T, LND_C_LEAF, LND_C_ADD)                                                                                                        \
                dst[c] = p[0];                                                                                                                                 \
            }                                                                                                                                                  \
        } else {                                                                                                                                               \
            for (uint32_t c = 0; c < count; c++) {                                                                                                             \
                float p[T];                                                                                                                                    \
                for (uint32_t i = 0; i < T; i++)                                                                                                               \
                    p[i] = src[(size_t)i * count + c] * h[i];                                                                                                  \
                for (uint32_t n = T / 2; n; n /= 2)                                                                                                            \
                    for (uint32_t i = 0; i < n; i++)                                                                                                           \
                        p[i] += p[i + n];                                                                                                                      \
                dst[c] = p[0];                                                                                                                                 \
            }                                                                                                                                                  \
        }                                                                                                                                                      \
    }
LND_SINC_FRAMES(LND_FRAME)
#undef LND_FRAME
#undef LND_C_LEAF
#undef LND_C_ADD

static const lnd_sinc_dot dots[3][5] = LND_SINC_TABLE(dot_);
static const lnd_sinc_frame frames[3][4] = LND_SINC_FRAME_TABLE(frame_);

lnd_sinc_dot lnd_sinc_select_c(uint32_t taps, uint32_t stride) {
    int row = lnd_sinc_row(taps);
    return row < 0 ? lnd_sinc_dot_c : dots[row][lnd_sinc_column(stride)];
}

lnd_sinc_frame lnd_sinc_frame_select_c(uint32_t taps, uint32_t channels) {
    int row = lnd_sinc_row(taps);
    return row < 0 ? nullptr : frames[row][lnd_sinc_frame_column(channels)];
}

float lnd_sinc_dot_c(const float *src, const float *h, uint32_t taps, uint32_t stride) {
    int row = lnd_sinc_row(taps);
    if (row >= 0) return dots[row][lnd_sinc_column(stride)](src, h, taps, stride);
    float a = 0, b = 0, c = 0, d = 0;
    uint32_t i = 0;
    for (; taps - i >= 4; i += 4) {
        a += src[(size_t)i * stride] * h[i];
        b += src[(size_t)(i + 1) * stride] * h[i + 1];
        c += src[(size_t)(i + 2) * stride] * h[i + 2];
        d += src[(size_t)(i + 3) * stride] * h[i + 3];
    }
    float sum = ((a + b) + c) + d;
    for (; i < taps; i++)
        sum += src[(size_t)i * stride] * h[i];
    return sum;
}
