#pragma once

#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Key "audio.resample_quality": Default LND_RESAMPLE quality for rate conversion. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_AUDIO_RESAMPLE_QUALITY;

/** Key "audio.channel_mix": Default LND_CHANNEL_MIX conversion policy. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_AUDIO_CHANNEL_MIX;

/** Key "audio.simd": Requested LND_SIMD implementation; set before LibraryInit. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_AUDIO_SIMD;

enum {
    LND_SIMD_AUTO = 0, /**< Select the best supported PCM implementation. */
    LND_SIMD_NONE = 1, /**< Use scalar PCM processing. */
    LND_SIMD_SSE2 = 2, /**< Request x86 SSE2 processing where available. */
    LND_SIMD_AVX = 3, /**< Request x86 AVX processing where available. */
    LND_SIMD_AVX2 = 4, /**< Request x86 AVX2 processing where available. */
    LND_SIMD_AVX512 = 5, /**< Request x86 AVX-512 processing where available. */
    LND_SIMD_NEON = 6, /**< Request Arm NEON processing where available. */
};

enum {
    LND_RESAMPLE_LINEAR = 0, /**< Linear interpolation with minimal history/work. */
    LND_RESAMPLE_SINC8 = 1, /**< 8-tap sinc interpolation. */
    LND_RESAMPLE_SINC16 = 2, /**< 16-tap sinc interpolation. */
    LND_RESAMPLE_SINC32 = 3, /**< 32-tap sinc interpolation. */
};

enum {
    LND_CHANNEL_MIX_MATRIX = 0, /**< Use the standard channel gain matrix. */
    LND_CHANNEL_MIX_SIMPLE = 1, /**< Use simplified channel mapping. */
};

#ifdef __cplusplus
}
#endif
