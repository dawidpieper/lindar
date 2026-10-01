#include "pcm/audio/simd.h"
#include "sinc_x86.h"
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
#if defined(__GNUC__) || defined(__clang__)
#define LND_TARGET __attribute__((target("avx512f,avx512bw")))
#else
#define LND_TARGET
#endif
#define LND_PREFIX lnd_sinc_avx512_
#define LND_NAME "avx512"
#define LND_WIDTH 16
#define LND_VECTOR __m512
#define LND_LOAD _mm512_loadu_ps
#define LND_STORE _mm512_storeu_ps
#define LND_MUL _mm512_mul_ps
#define LND_ADD _mm512_add_ps
#define LND_SPLAT _mm512_set1_ps
#include "sinc_x86_impl.h"
#endif
