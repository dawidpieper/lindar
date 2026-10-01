#pragma once
#include "src/platform.h"

typedef float (*lnd_sinc_dot)(const float *src, const float *coefficients, uint32_t taps, uint32_t stride);
typedef void (*lnd_sinc_frame)(const float *src, const float *coefficients, float *dst, uint32_t taps, uint32_t channels);
typedef lnd_sinc_dot (*lnd_sinc_selector)(uint32_t taps, uint32_t stride);
typedef lnd_sinc_frame (*lnd_sinc_frame_selector)(uint32_t taps, uint32_t channels);

float lnd_sinc_dot_c(const float *src, const float *coefficients, uint32_t taps, uint32_t stride);
lnd_sinc_dot lnd_sinc_select_c(uint32_t taps, uint32_t stride);
lnd_sinc_frame lnd_sinc_frame_select_c(uint32_t taps, uint32_t channels);

#define LND_SINC_SHAPES(M)                                                                                                                                     \
    M(8, 1)                                                                                                                                                    \
    M(8, 2)                                                                                                                                                    \
    M(8, 4)                                                                                                                                                    \
    M(8, 8)                                                                                                                                                    \
    M(8, 0)                                                                                                                                                    \
    M(16, 1)                                                                                                                                                   \
    M(16, 2)                                                                                                                                                   \
    M(16, 4)                                                                                                                                                   \
    M(16, 8)                                                                                                                                                   \
    M(16, 0)                                                                                                                                                   \
    M(32, 1)                                                                                                                                                   \
    M(32, 2)                                                                                                                                                   \
    M(32, 4)                                                                                                                                                   \
    M(32, 8)                                                                                                                                                   \
    M(32, 0)
#define LND_SINC_FRAMES(M)                                                                                                                                     \
    M(8, 2)                                                                                                                                                    \
    M(8, 4)                                                                                                                                                    \
    M(8, 8)                                                                                                                                                    \
    M(8, 0)                                                                                                                                                    \
    M(16, 2)                                                                                                                                                   \
    M(16, 4)                                                                                                                                                   \
    M(16, 8)                                                                                                                                                   \
    M(16, 0)                                                                                                                                                   \
    M(32, 2)                                                                                                                                                   \
    M(32, 4)                                                                                                                                                   \
    M(32, 8)                                                                                                                                                   \
    M(32, 0)

LND_INLINE int lnd_sinc_row(uint32_t taps) { return taps == 8 ? 0 : taps == 16 ? 1 : taps == 32 ? 2 : -1; }
LND_INLINE unsigned lnd_sinc_column(uint32_t stride) { return stride == 1 ? 0 : stride == 2 ? 1 : stride == 4 ? 2 : stride == 8 ? 3 : 4; }
LND_INLINE unsigned lnd_sinc_frame_column(uint32_t channels) { return channels == 2 ? 0 : channels == 4 ? 1 : channels == 8 ? 2 : 3; }

#define LND_SINC_TABLE(P)                                                                                                                                      \
    {{P##8_1, P##8_2, P##8_4, P##8_8, P##8_0}, {P##16_1, P##16_2, P##16_4, P##16_8, P##16_0}, {P##32_1, P##32_2, P##32_4, P##32_8, P##32_0}}
#define LND_SINC_FRAME_TABLE(P) {{P##8_2, P##8_4, P##8_8, P##8_0}, {P##16_2, P##16_4, P##16_8, P##16_0}, {P##32_2, P##32_4, P##32_8, P##32_0}}
