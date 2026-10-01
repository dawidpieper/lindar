#include "pcm/audio/simd.h"
#include "sinc_x86.h"
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
#if defined(__GNUC__) || defined(__clang__)
#define LND_TARGET __attribute__((target("sse2")))
#else
#define LND_TARGET
#endif
#define LND_PREFIX lnd_sinc_sse2_
#define LND_NAME "sse2"
#define LND_WIDTH 4
#define LND_VECTOR __m128
#define LND_LOAD _mm_loadu_ps
#define LND_STORE _mm_storeu_ps
#define LND_MUL _mm_mul_ps
#define LND_ADD _mm_add_ps
#define LND_SPLAT _mm_set1_ps
#include "sinc_x86_impl.h"
#endif
