#pragma once
#include "pcm/audio/sinc.h"
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
#define LND_DECLARE_X86(NAME)                                                                                                                                  \
    lnd_sinc_dot lnd_sinc_##NAME##_select(uint32_t taps, uint32_t stride);                                                                                     \
    lnd_sinc_frame lnd_sinc_##NAME##_frame_select(uint32_t taps, uint32_t channels);                                                                           \
    void lnd_sinc_##NAME##_init(void);
LND_DECLARE_X86(sse2)
LND_DECLARE_X86(avx)
LND_DECLARE_X86(avx512)
#undef LND_DECLARE_X86
#endif
