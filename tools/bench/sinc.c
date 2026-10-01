#include "lindar.h"
#include "pcm/audio/simd.h"
#if LND_MODULE_SINC_ASM
#include "lindar_sinc_asm.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

typedef struct {
    lnd_simd_ops ops;
    const char *name;
} backend;

static double now(void) {
#if defined(_WIN32)
    LARGE_INTEGER value, frequency;
    QueryPerformanceCounter(&value);
    QueryPerformanceFrequency(&frequency);
    return (double)value.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
#endif
}

#if defined(LND_BENCH_RESAMPLE)
#include "pcm/audio/source.h"

typedef struct {
    lnd_source base;
    const float *data;
} input_source;

static uint64_t input_read(lnd_source *source, float *dst, uint64_t frames) {
    input_source *input = (input_source *)source;
    uint32_t channels = source->channels;
    uint64_t done = 0;
    while (done < frames) {
        uint32_t offset = (uint32_t)(source->pos & 255);
        uint64_t n = frames - done < 256 - offset ? frames - done : 256 - offset;
        memcpy(dst + done * channels, input->data + (size_t)offset * channels, (size_t)n * channels * sizeof(float));
        done += n;
        source->pos += n;
    }
    return done;
}

static double measure_resample(backend *b, const float *src, unsigned iterations, uint32_t taps, uint32_t channels, float *checksum) {
    static const lnd_source_vt vt = {.read = input_read};
    input_source input = {.base = {.vt = &vt, .sample_rate_hz = 48000, .channels = channels}, .data = src};
    lnd_simd_ops saved = lnd_simd;
    lnd_simd = b->ops;
    lnd_source *resampler = lnd_resample_source_create(&input.base, false, 44100, 256, taps == 8 ? 1 : taps == 16 ? 2 : 3);
    lnd_simd = saved;
    if (!resampler) exit(2);
    float out[256 * 32], sum = 0;
    if (lnd_source_read(resampler, out, 256) != 256) exit(2);
    unsigned blocks = iterations / 256 + 1;
    double start = now();
    for (unsigned i = 0; i < blocks; i++) {
        if (lnd_source_read(resampler, out, 256) != 256) exit(2);
        sum += out[(i & 255) * channels];
    }
    double elapsed = now() - start;
    *checksum = sum;
    lnd_source_free(resampler);
    return elapsed * 1e9 / ((double)blocks * 256);
}
#endif

static double measure(backend *b, const float *src, const float *h, unsigned iterations, uint32_t taps, uint32_t channels, bool frame, float *checksum) {
    lnd_sinc_dot dot = b->ops.sinc_select(taps, channels);
    lnd_sinc_frame block = frame && channels > 1 ? b->ops.sinc_frame_select(taps, channels) : nullptr;
    float out[32], a = 0, c = 0, d = 0, e = 0;
    double start = now();
    for (unsigned i = 0; i < iterations; i += 4) {
        const float *s = src + (size_t)(i & 15) * 32 * channels;
        const float *coeff = h + (size_t)(i & 7) * 32;
        if (block) {
            block(s, coeff, out, taps, channels);
            a += out[0];
            block(s + 32 * channels, coeff + 32, out, taps, channels);
            c += out[channels - 1];
            block(s + 64 * channels, coeff + 64, out, taps, channels);
            d += out[0];
            block(s + 96 * channels, coeff + 96, out, taps, channels);
            e += out[channels - 1];
        } else if (frame && channels > 1) {
            for (uint32_t j = 0; j < channels; j++)
                out[j] = dot(s + j, coeff, taps, channels);
            a += out[0];
            for (uint32_t j = 0; j < channels; j++)
                out[j] = dot(s + 32 * channels + j, coeff + 32, taps, channels);
            c += out[channels - 1];
            for (uint32_t j = 0; j < channels; j++)
                out[j] = dot(s + 64 * channels + j, coeff + 64, taps, channels);
            d += out[0];
            for (uint32_t j = 0; j < channels; j++)
                out[j] = dot(s + 96 * channels + j, coeff + 96, taps, channels);
            e += out[channels - 1];
        } else {
            a += dot(s, coeff, taps, channels);
            c += dot(s + 32 * channels, coeff + 32, taps, channels);
            d += dot(s + 64 * channels, coeff + 64, taps, channels);
            e += dot(s + 96 * channels, coeff + 96, taps, channels);
        }
    }
    double elapsed = now() - start;
    *checksum = (a + c) + (d + e);
    return elapsed * 1e9 / iterations;
}

int main(int argc, char **argv) {
    unsigned iterations = argc > 1 ? (unsigned)strtoul(argv[1], nullptr, 10) : 1000000;
    if (iterations < 4 || iterations > 100000000) return 1;
    iterations &= ~3u;
    bool render = false, wide = false, all = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "resample") == 0)
            render = true;
        else if (strcmp(argv[i], "wide") == 0)
            wide = true;
        else if (strcmp(argv[i], "all") == 0)
            all = wide = true;
        else
            return 1;
    }
#if !defined(LND_BENCH_RESAMPLE)
    if (render) {
        fputs("Resampler component is not built.\n", stderr);
        return 2;
    }
#endif
    static float src[32 * 32 * 16], h[32 * 16];
    for (unsigned i = 0; i < sizeof src / sizeof *src; i++)
        src[i] = (float)((int)(i % 47) - 23) / 32;
    for (unsigned i = 0; i < sizeof h / sizeof *h; i++)
        h[i] = (float)((int)(i % 19) - 9) / 128;
    backend backends[16];
    unsigned count = 0;
    const int modes[] = {LND_SIMD_NONE, LND_SIMD_SSE2, LND_SIMD_AVX, LND_SIMD_AVX2, LND_SIMD_AVX512, LND_SIMD_AUTO};
    for (unsigned assembly = 0; assembly < 2; assembly++) {
        for (unsigned mode = 0; mode < sizeof modes / sizeof *modes; mode++) {
            if (assembly && mode == 0) continue;
            if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_AUDIO_SIMD, modes[mode]) != LND_OK ||
#if LND_MODULE_SINC_ASM
                LND_ConfigSet(LND_CFG_SINC_ASM_ENABLED, assembly) != LND_OK ||
#endif
                LND_LibraryInit() != LND_OK)
                return 1;
            bool found = false;
            for (unsigned i = 0; i < count; i++)
                if (backends[i].ops.sinc_select == lnd_simd.sinc_select && strcmp(backends[i].name, lnd_simd.sinc_name) == 0) found = true;
            if (!found) backends[count++] = (backend){.ops = lnd_simd, .name = lnd_simd.sinc_name};
            LND_LibraryFree();
        }
    }
    if (render && LND_LibraryInit() != LND_OK) return 2;
    puts("backend,kind,taps,channels,median_ns,min_ns,max_ns,checksum");
    for (unsigned frame = 0; frame < (render ? 1u : 2u); frame++) {
        for (uint32_t channels = 1; channels <= (wide ? 32u : 8u); channels = all ? channels + 1 : channels * 2) {
            for (uint32_t taps = 8; taps <= 32; taps *= 2) {
                double timings[16][7];
                float sums[16];
                for (unsigned trial = 0; trial < 7; trial++) {
                    for (unsigned j = 0; j < count; j++) {
                        unsigned k = (j + trial) % count;
#if defined(LND_BENCH_RESAMPLE)
                        if (render)
                            timings[k][trial] = measure_resample(&backends[k], src, iterations, taps, channels, &sums[k]);
                        else
#endif
                            timings[k][trial] = measure(&backends[k], src, h, iterations, taps, channels, frame != 0, &sums[k]);
                    }
                }
                for (unsigned k = 0; k < count; k++) {
                    for (unsigned i = 1; i < 7; i++) {
                        double x = timings[k][i];
                        unsigned j = i;
                        while (j && timings[k][j - 1] > x)
                            timings[k][j] = timings[k][j - 1], j--;
                        timings[k][j] = x;
                    }
                    printf("%s,%s,%u,%u,%.3f,%.3f,%.3f,%.9g\n", backends[k].name,
                           render  ? "resample"
                           : frame ? "frame"
                                   : "dot",
                           taps, channels, timings[k][3], timings[k][0], timings[k][6], (double)sums[k]);
                }
            }
        }
    }
    if (render) LND_LibraryFree();
    return 0;
}
