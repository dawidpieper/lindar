#include "cpu.h"
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
static void cpuid(int info[4], int leaf) {
#if defined(_MSC_VER)
    __cpuidex(info, leaf, 0);
#else
    __cpuid_count(leaf, 0, info[0], info[1], info[2], info[3]);
#endif
}
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("xsave")))
#endif
static uint64_t xgetbv(void) {
    return _xgetbv(0);
}
#endif

int32_t lnd_x86_level(int32_t mode) {
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
    if (mode == LND_SIMD_NONE || mode == LND_SIMD_NEON) return LND_SIMD_NONE;
    int info[4];
    cpuid(info, 0);
    int maximum = info[0];
    if (maximum < 1) return LND_SIMD_NONE;
    cpuid(info, 1);
    if (!(info[3] & (1 << 26))) return LND_SIMD_NONE;
    int level = LND_SIMD_SSE2;
    uint64_t xcr = info[2] & (1 << 27) ? xgetbv() : 0;
    if ((info[2] & (1 << 28)) && (xcr & 6) == 6) {
        level = LND_SIMD_AVX;
        if (maximum >= 7) {
            cpuid(info, 7);
            if (info[1] & (1 << 5)) {
                level = LND_SIMD_AVX2;
                if ((xcr & 0xe6) == 0xe6 && (info[1] & (1 << 16)) && (info[1] & (1 << 30))) level = LND_SIMD_AVX512;
            }
        }
    }
    return mode == LND_SIMD_AUTO ? level : LND_MIN(mode, level);
#else
    LND_UNUSED(mode);
    return LND_SIMD_NONE;
#endif
}
