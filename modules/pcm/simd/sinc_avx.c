#include "pcm/audio/simd.h"
#include "sinc_x86.h"
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
#if defined(__GNUC__) || defined(__clang__)
#define LND_TARGET __attribute__((target("avx")))
#else
#define LND_TARGET
#endif
#define LND_PREFIX lnd_sinc_avx_
#define LND_NAME "avx"
#define LND_WIDTH 8
#define LND_VECTOR __m256
#define LND_LOAD _mm256_loadu_ps
#define LND_STORE _mm256_storeu_ps
#define LND_MUL _mm256_mul_ps
#define LND_ADD _mm256_add_ps
#define LND_SPLAT _mm256_set1_ps
#include "sinc_x86_impl.h"
#endif
