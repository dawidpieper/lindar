#pragma once
#include "pcm/audio/sinc.h"
#define LND_DECLARE(T, S)                                                                                                                                      \
    extern float lnd_sinc_x64_sse2_dot_##T##_##S(const float *, const float *, uint32_t, uint32_t);                                                            \
    extern float lnd_sinc_x64_avx_dot_##T##_##S(const float *, const float *, uint32_t, uint32_t);                                                             \
    extern float lnd_sinc_x64_avx512_dot_##T##_##S(const float *, const float *, uint32_t, uint32_t);
LND_SINC_SHAPES(LND_DECLARE)
#undef LND_DECLARE
#define LND_DECLARE(T, C)                                                                                                                                      \
    extern void lnd_sinc_x64_sse2_frame_##T##_##C(const float *, const float *, float *, uint32_t, uint32_t);                                                  \
    extern void lnd_sinc_x64_avx_frame_##T##_##C(const float *, const float *, float *, uint32_t, uint32_t);
LND_SINC_FRAMES(LND_DECLARE)
#undef LND_DECLARE
extern void lnd_sinc_x64_avx512_frame_8_0(const float *, const float *, float *, uint32_t, uint32_t);
extern void lnd_sinc_x64_avx512_frame_16_0(const float *, const float *, float *, uint32_t, uint32_t);
extern void lnd_sinc_x64_avx512_frame_32_0(const float *, const float *, float *, uint32_t, uint32_t);
