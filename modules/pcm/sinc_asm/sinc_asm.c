#include "lindar_sinc_asm.h"
#include "pcm/audio/simd.h"
#include "src/config.h"

#if defined(LND_SINC_ASM_ARM64)
#define LND_DOT(T, S) extern float lnd_sinc_arm64_dot_##T##_##S(const float *, const float *, uint32_t, uint32_t);
LND_SINC_SHAPES(LND_DOT)
#undef LND_DOT
#define LND_FRAME(T, C) extern void lnd_sinc_arm64_frame_##T##_##C(const float *, const float *, float *, uint32_t, uint32_t);
LND_SINC_FRAMES(LND_FRAME)
LND_FRAME(8, 16)
LND_FRAME(8, 32)
#undef LND_FRAME
static const lnd_sinc_dot dots[3][5] = LND_SINC_TABLE(lnd_sinc_arm64_dot_);
static const lnd_sinc_frame frames[3][4] = LND_SINC_FRAME_TABLE(lnd_sinc_arm64_frame_);
static lnd_sinc_dot select_dot(uint32_t taps, uint32_t stride) {
    int row = lnd_sinc_row(taps);
    return row < 0 ? lnd_sinc_select_c(taps, stride) : dots[row][lnd_sinc_column(stride)];
}
static lnd_sinc_frame select_frame(uint32_t taps, uint32_t channels) {
    if (taps == 8 && channels == 16) return lnd_sinc_arm64_frame_8_16;
    if (taps == 8 && channels == 32) return lnd_sinc_arm64_frame_8_32;
    int row = lnd_sinc_row(taps);
    unsigned col = lnd_sinc_frame_column(channels);
    return row < 0 ? lnd_sinc_frame_select_c(taps, channels) : frames[row][col];
}
static float dot(const float *src, const float *h, uint32_t taps, uint32_t stride) { return select_dot(taps, stride)(src, h, taps, stride); }
#endif

#if defined(LND_SINC_ASM_X64)
#include "pcm/audio/cpu.h"
#include "x64_kernels.h"
#include <limits.h>
#define LND_BACKEND(LEVEL)                                                                                                                                     \
    static const lnd_sinc_dot LEVEL##_dots[3][5] = LND_SINC_TABLE(lnd_sinc_x64_##LEVEL##_dot_);                                                                \
    static const lnd_sinc_frame LEVEL##_frames[3][4] = LND_SINC_FRAME_TABLE(lnd_sinc_x64_##LEVEL##_frame_);                                                    \
    static lnd_sinc_dot LEVEL##_select(uint32_t taps, uint32_t stride) {                                                                                       \
        int row = lnd_sinc_row(taps);                                                                                                                          \
        return row < 0 ? lnd_sinc_select_c(taps, stride) : LEVEL##_dots[row][lnd_sinc_column(stride)];                                                         \
    }                                                                                                                                                          \
    static lnd_sinc_frame LEVEL##_frame_select(uint32_t taps, uint32_t channels) {                                                                             \
        int row = lnd_sinc_row(taps);                                                                                                                          \
        return row < 0 ? lnd_sinc_frame_select_c(taps, channels) : LEVEL##_frames[row][lnd_sinc_frame_column(channels)];                                       \
    }                                                                                                                                                          \
    static float LEVEL##_dot(const float *src, const float *h, uint32_t taps, uint32_t stride) { return LEVEL##_select(taps, stride)(src, h, taps, stride); }
LND_BACKEND(sse2)
LND_BACKEND(avx)
#undef LND_BACKEND
#define LND_ROW(T)                                                                                                                                             \
    {lnd_sinc_x64_avx512_dot_##T##_1, lnd_sinc_x64_avx512_dot_##T##_2, lnd_sinc_x64_avx512_dot_##T##_4, lnd_sinc_x64_avx512_dot_##T##_8,                       \
     lnd_sinc_x64_avx512_dot_##T##_0}
static const lnd_sinc_dot avx512_dots[2][5] = {LND_ROW(16), LND_ROW(32)};
#undef LND_ROW
static const lnd_sinc_frame avx512_frames[3] = {lnd_sinc_x64_avx512_frame_8_0, lnd_sinc_x64_avx512_frame_16_0, lnd_sinc_x64_avx512_frame_32_0};
static lnd_sinc_dot avx512_select(uint32_t taps, uint32_t stride) {
    int row = lnd_sinc_row(taps);
    if (row < 1) return avx_select(taps, stride);
    return stride > INT_MAX / 32 ? lnd_sinc_select_c(taps, stride) : avx512_dots[row - 1][lnd_sinc_column(stride)];
}
static lnd_sinc_frame avx512_frame_select(uint32_t taps, uint32_t channels) {
    int row = lnd_sinc_row(taps);
    return row < 0 || channels < 16 ? avx_frame_select(taps, channels) : avx512_frames[row];
}
static float avx512_dot(const float *src, const float *h, uint32_t taps, uint32_t stride) { return avx512_select(taps, stride)(src, h, taps, stride); }
#endif

bool LND_SincAsmIsAvailable(void) {
#if defined(LND_SINC_ASM_X64) || defined(LND_SINC_ASM_ARM64)
    return true;
#else
    return false;
#endif
}
const char *LND_SincAsmGetName(void) { return lnd_simd.sinc_name; }

int32_t lnd_sinc_asm_init(void) {
#if defined(LND_SINC_ASM_X64)
    int32_t level = lnd_x86_level((int32_t)lnd_cfg_u32(LND_CFG_AUDIO_SIMD));
    if (lnd_cfg_u32(LND_CFG_SINC_ASM_ENABLED) && level >= LND_SIMD_SSE2) {
        if (level >= LND_SIMD_AVX512) {
            lnd_simd.sinc = avx512_dot;
            lnd_simd.sinc_select = avx512_select;
            lnd_simd.sinc_frame_select = avx512_frame_select;
            lnd_simd.sinc_name = "asm-avx512";
        } else if (level >= LND_SIMD_AVX) {
            lnd_simd.sinc = avx_dot;
            lnd_simd.sinc_select = avx_select;
            lnd_simd.sinc_frame_select = avx_frame_select;
            lnd_simd.sinc_name = "asm-avx";
        } else {
            lnd_simd.sinc = sse2_dot;
            lnd_simd.sinc_select = sse2_select;
            lnd_simd.sinc_frame_select = sse2_frame_select;
            lnd_simd.sinc_name = "asm-sse2";
        }
    }
#elif defined(LND_SINC_ASM_ARM64)
    uint32_t mode = lnd_cfg_u32(LND_CFG_AUDIO_SIMD);
    if (lnd_cfg_u32(LND_CFG_SINC_ASM_ENABLED) && (mode == LND_SIMD_AUTO || mode == LND_SIMD_NEON)) {
        lnd_simd.sinc = dot;
        lnd_simd.sinc_select = select_dot;
        lnd_simd.sinc_frame_select = select_frame;
        lnd_simd.sinc_name = "asm-arm64";
    }
#endif
    return LND_OK;
}
