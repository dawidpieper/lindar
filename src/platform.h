#pragma once

#include "lindar.h"
#include "lnd_modules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if LND_OS_MODE && defined(_WIN32)
#define LND_OS_WINDOWS 1
#elif LND_OS_MODE && defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define LND_OS_IOS 1
#else
#define LND_OS_MACOS 1
#endif
#elif LND_OS_MODE && defined(__ANDROID__)
#define LND_OS_ANDROID 1
#elif LND_OS_MODE && defined(__linux__)
#define LND_OS_LINUX 1
#endif

#ifndef LND_OS_WINDOWS
#define LND_OS_WINDOWS 0
#endif
#ifndef LND_OS_MACOS
#define LND_OS_MACOS 0
#endif
#ifndef LND_OS_IOS
#define LND_OS_IOS 0
#endif
#ifndef LND_OS_ANDROID
#define LND_OS_ANDROID 0
#endif
#ifndef LND_OS_LINUX
#define LND_OS_LINUX 0
#endif
#define LND_OS_POSIX (LND_OS_MACOS || LND_OS_IOS || LND_OS_ANDROID || LND_OS_LINUX)

#if defined(__aarch64__) || defined(_M_ARM64)
#define LND_ARCH_ARM64 1
#elif defined(__x86_64__) || defined(_M_X64)
#define LND_ARCH_X64 1
#elif defined(__arm__) || defined(_M_ARM)
#define LND_ARCH_ARM 1
#elif defined(__i386__) || defined(_M_IX86)
#define LND_ARCH_X86 1
#endif

#if defined(__clang__)
#define LND_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define LND_COMPILER_GCC 1
#elif defined(_MSC_VER)
#define LND_COMPILER_MSVC 1
#endif

#if defined(LND_COMPILER_MSVC)
#define LND_INLINE static __forceinline
#define LND_NOINLINE __declspec(noinline)
#define LND_LIKELY(x) (x)
#define LND_UNLIKELY(x) (x)
#define LND_RESTRICT __restrict
#else
#define LND_INLINE static inline __attribute__((always_inline))
#define LND_NOINLINE __attribute__((noinline))
#define LND_LIKELY(x) __builtin_expect(!!(x), 1)
#define LND_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define LND_RESTRICT restrict
#endif

#define LND_COUNTOF(a) (sizeof(a) / sizeof((a)[0]))
#define LND_MIN(a, b) ((a) < (b) ? (a) : (b))
#define LND_MAX(a, b) ((a) > (b) ? (a) : (b))
#define LND_CLAMP(x, lo, hi) LND_MIN(LND_MAX(x, lo), hi)
#define LND_UNUSED(x) ((void)(x))

#define LND_MAX_CHANNELS 32
#define LND_CACHE_LINE 64

#if defined(LND_DEBUG)
#include <assert.h>
#define LND_ASSERT(x) assert(x)
#else
#define LND_ASSERT(x) ((void)0)
#endif

LND_INLINE uint32_t lnd_next_pow2_u32(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

LND_INLINE size_t lnd_next_pow2_size(size_t v) {
    if (v <= 1) return 1;
    if (v > SIZE_MAX / 2 + 1) return 0;
    size_t r = 1;
    while (r < v) r <<= 1;
    return r;
}

LND_INLINE uint32_t lnd_log2_size(size_t v) {
    uint32_t r = 0;
    while (v >>= 1) r++;
    return r;
}
