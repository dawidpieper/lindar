#include "lindar.h"
#include "pcm/audio/simd.h"
#if LND_MODULE_SINC_ASM
#include "lindar_sinc_asm.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static double now(void) {
#if defined(_WIN32)
    LARGE_INTEGER value, frequency;
    QueryPerformanceCounter(&value);
    QueryPerformanceFrequency(&frequency);
    return (double)value.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec value;
    timespec_get(&value, TIME_UTC);
    return (double)value.tv_sec + (double)value.tv_nsec * 1e-9;
#endif
}

static float serial_dot(const float *src, const float *h, uint32_t taps, uint32_t stride) {
    float result = 0;
    for (uint32_t t = 0; t < taps; t++)
        result += src[(size_t)t * stride] * h[t];
    return result;
}

static void dots(const char *name, lnd_sinc_dot proc, lnd_sinc_selector select, unsigned iterations) {
    float input[32 * 8], h[32];
    for (unsigned i = 0; i < 32 * 8; i++)
        input[i] = (float)((int)(i % 47) - 23) / 32;
    for (unsigned i = 0; i < 32; i++)
        h[i] = (float)((int)(i % 7) - 3) / 64;
    for (uint32_t stride = 1; stride <= 8; stride *= 2) {
        for (uint32_t taps = 8; taps <= 32; taps *= 2) {
            lnd_sinc_dot volatile dot = select ? select(taps, stride) : proc;
            double best = 1e30;
            float checksum = 0;
            for (unsigned trial = 0; trial < 3; trial++) {
                float sum = 0;
                double start = now();
                for (unsigned i = 0; i < iterations; i++)
                    sum += dot(input, h, taps, stride);
                double elapsed = now() - start;
                if (elapsed < best) best = elapsed;
                checksum = sum;
            }
            printf("%s,sinc%u_stride%u,1,%.3f,%.9g\n", name, taps, stride, best * 1e9 / iterations, (double)checksum);
        }
    }
}

static void buffers(unsigned iterations) {
    float input[2048], output[2048];
    int16_t shorts[2048];
    for (unsigned i = 0; i < 2048; i++)
        input[i] = (float)((int)(i % 127) - 63) / 64, shorts[i] = (int16_t)(i * 31 - 32768);
    static const size_t blocks[] = {32, 128, 480, 1024};
    for (size_t k = 0; k < sizeof blocks / sizeof *blocks; k++) {
        size_t n = blocks[k] * 2;
        for (unsigned operation = 0; operation < 3; operation++) {
            double best = 1e30;
            float checksum = 0;
            for (unsigned trial = 0; trial < 3; trial++) {
                double start = now();
                for (unsigned i = 0; i < iterations; i++) {
                    if (operation == 0)
                        lnd_simd.s16_to_f32(shorts, output, n);
                    else if (operation == 1)
                        lnd_simd.f32_to_s16(input, shorts, n);
                    else {
                        for (size_t s = 0; s < n; s++)
                            output[s] = 0;
                        lnd_simd.accumulate(output, input, 0.5f, n);
                    }
                }
                double elapsed = now() - start;
                if (elapsed < best) best = elapsed;
                checksum = operation == 1 ? (float)shorts[n / 2] : output[n / 2];
            }
            printf("%s,%s,%zu,%.3f,%.9g\n", lnd_simd.name,
                   operation == 0   ? "s16_f32"
                   : operation == 1 ? "f32_s16"
                                    : "mix",
                   blocks[k], best * 1e9 / ((double)iterations * n), (double)checksum);
        }
    }
}

int main(int argc, char **argv) {
    unsigned iterations = argc > 1 ? (unsigned)strtoul(argv[1], nullptr, 10) : 200000;
    if (!iterations || iterations > 10000000) return 1;
    printf("backend,operation,frames,ns_per_sample_or_dot,checksum\n");
    dots("serial-c", serial_dot, nullptr, iterations);
    for (int mode = 0; mode < 3; mode++) {
        if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK ||
            LND_ConfigSet(LND_CFG_AUDIO_SIMD, mode == 0 ? LND_SIMD_NONE : LND_SIMD_AUTO) != LND_OK ||
#if LND_MODULE_SINC_ASM
            LND_ConfigSet(LND_CFG_SINC_ASM_ENABLED, mode == 2) != LND_OK ||
#endif
            LND_LibraryInit() != LND_OK)
            return 1;
        dots(lnd_simd.sinc_name, lnd_simd.sinc, lnd_simd.sinc_select, iterations);
        if (mode != 2) buffers(iterations / 20 + 1);
        LND_LibraryFree();
    }
    return 0;
}
