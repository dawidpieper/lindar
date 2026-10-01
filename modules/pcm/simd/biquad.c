#include "pcm/audio/simd.h"
#include <string.h>

#if (defined(LND_COMPILER_CLANG) || defined(LND_COMPILER_GCC)) && (defined(LND_ARCH_ARM64) || defined(LND_ARCH_X64))
typedef float lnd_float2 __attribute__((vector_size(8)));
typedef float lnd_float4 __attribute__((vector_size(16)));
typedef double lnd_double2 __attribute__((vector_size(16)));

#define LND_BIQUAD_FLOAT(N) \
    for (; c + N <= channels; c += N) { \
        lnd_float##N z1, z2; \
        memcpy(&z1, state1 + c, sizeof z1); \
        memcpy(&z2, state2 + c, sizeof z2); \
        for (uint32_t f = 0; f < frames; f++) { \
            float *p = pcm + (size_t)f * channels + c; \
            lnd_float##N x; \
            memcpy(&x, p, sizeof x); \
            lnd_float##N y = k->b0 * x + z1; \
            z1 = k->b1 * x - k->a1 * y + z2; \
            z2 = k->b2 * x - k->a2 * y; \
            memcpy(p, &y, sizeof y); \
        } \
        memcpy(state1 + c, &z1, sizeof z1); \
        memcpy(state2 + c, &z2, sizeof z2); \
    }

static uint32_t biquad_f32(float *pcm, uint32_t frames, uint32_t channels, const lnd_biquad_coeff_f32 *k, float *state1, float *state2) {
    uint32_t c = 0;
    LND_BIQUAD_FLOAT(4);
    LND_BIQUAD_FLOAT(2);
    for (uint32_t i = 0; i < c; i++) {
        if (fabsf(state1[i]) < 1e-30f) state1[i] = 0;
        if (fabsf(state2[i]) < 1e-30f) state2[i] = 0;
    }
    return c;
}
#undef LND_BIQUAD_FLOAT

static void biquad_f64(float *pcm, uint32_t frames, uint32_t channels, const lnd_biquad_coeff *k, double z[2][2]) {
    lnd_double2 z1 = {z[0][0], z[1][0]}, z2 = {z[0][1], z[1][1]};
    for (uint32_t f = 0; f < frames; f++) {
        float *p = pcm + (size_t)f * channels;
        lnd_float2 xf;
        memcpy(&xf, p, sizeof xf);
        lnd_double2 x = __builtin_convertvector(xf, lnd_double2);
        lnd_double2 y = k->b0 * x + z1;
        z1 = k->b1 * x - k->a1 * y + z2;
        z2 = k->b2 * x - k->a2 * y;
        lnd_float2 yf = __builtin_convertvector(y, lnd_float2);
        memcpy(p, &yf, sizeof yf);
    }
    for (uint32_t i = 0; i < 2; i++) {
        z[i][0] = fabs(z1[i]) < 1e-30 ? 0 : z1[i];
        z[i][1] = fabs(z2[i]) < 1e-30 ? 0 : z2[i];
    }
}
#endif

void lnd_simd_biquad_init(void) {
#if (defined(LND_COMPILER_CLANG) || defined(LND_COMPILER_GCC)) && (defined(LND_ARCH_ARM64) || defined(LND_ARCH_X64))
    if (strcmp(lnd_simd.name, "c") == 0) return;
    lnd_simd.biquad_f32 = biquad_f32;
    lnd_simd.biquad_f64 = biquad_f64;
#endif
}
