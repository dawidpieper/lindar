#include "lindar_analysis.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

static unsigned checks, failures;

#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)
#define NEAR(a, b, e) CHECK(fabs((double)(a) - (double)(b)) <= (e))

static void test_levels(void) {
    double values[] = {0.25, -0.5, 0.25, 0.5, 0.25, -0.5, 0.25, 0.5};
    LND_PCM pcm = {.data = values, .frames = 4, .channels = 2, .format = LND_FORMAT_F64};
    LND_LEVELS levels;
    CHECK(LND_PcmAnalyzeLevels(&pcm, 1, 2, 1, &levels) == LND_OK);
    NEAR(levels.peak, 0.5, 0.0);
    NEAR(levels.rms, 0.5, 0.0);
    NEAR(levels.rms_dbfs, -6.020599913279624, 1e-12);
    CHECK(levels.samples == 2);
    CHECK(LND_PcmAnalyzeLevels(&pcm, 0, 4, LND_CHANNEL_ALL, &levels) == LND_OK);
    NEAR(levels.rms, sqrt(0.15625), 1e-15);
    CHECK(levels.samples == 8);
    CHECK(LND_PcmAnalyzeLevels(&pcm, 4, 0, 0, &levels) == LND_OK);
    CHECK(levels.rms == 0.0 && levels.peak == 0.0 && levels.samples == 0);
    CHECK(isinf(levels.rms_dbfs) && levels.rms_dbfs < 0.0);
    CHECK(LND_PcmAnalyzeLevels(&pcm, 3, 2, 0, &levels) == LND_ERR_INVALID_ARG);
    CHECK(LND_PcmAnalyzeLevels(&pcm, 0, 1, 2, &levels) == LND_ERR_INVALID_ARG);
    values[0] = NAN;
    LND_LEVELS previous = levels;
    CHECK(LND_PcmAnalyzeLevels(&pcm, 0, 4, 0, &levels) == LND_ERR_FORMAT);
    CHECK(memcmp(&previous, &levels, sizeof levels) == 0);
    values[0] = DBL_MAX;
    CHECK(LND_PcmAnalyzeLevels(&pcm, 0, 1, 0, &levels) == LND_OK);
    CHECK(levels.rms == DBL_MAX);
    values[0] = 1e-300;
    CHECK(LND_PcmAnalyzeLevels(&pcm, 0, 1, 0, &levels) == LND_OK);
    CHECK(levels.rms == 1e-300);
    NEAR(LND_DbToAmplitude(LND_AmplitudeToDb(0.125)), 0.125, 1e-16);
}

static double window_at(int window, unsigned i, unsigned n) {
    double x = 2.0 * PI * i / n;
    if (window == LND_WINDOW_HANN) return 0.5 - 0.5 * cos(x);
    if (window == LND_WINDOW_HAMMING) return 0.54 - 0.46 * cos(x);
    if (window == LND_WINDOW_BLACKMAN) return 0.42 - 0.5 * cos(x) + 0.08 * cos(2.0 * x);
    return 1.0;
}

static void test_fft_reference(void) {
    for (unsigned n = 2; n <= 128; n *= 2) {
        for (int window = 0; window <= 3; window++) {
            float signal[130], unused[130] = {0};
            void *planes[] = {unused, signal};
            for (unsigned i = 0; i < n + 2; i++) signal[i] = (float)(0.4 * sin(0.31 * i) + 0.3 * cos(1.27 * i));
            LND_PCM pcm = {.planes = planes, .frames = n + 2, .channels = 2, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_PLANAR};
            size_t bytes = LND_FftGetMemoryBytes(n);
            void *memory = malloc(bytes);
            LND_FFT *fft = LND_FftInit(memory, bytes, n, window);
            CHECK(fft != nullptr && LND_FftGetFrames(fft) == n && LND_FftGetBinCount(fft) == n / 2 + 1);
            LND_COMPLEX actual[66];
            CHECK(LND_FftExecute(fft, &pcm, 1, 1, actual, 66) == LND_OK);
            for (unsigned k = 0; k <= n / 2; k++) {
                double real = 0.0, imag = 0.0;
                for (unsigned i = 0; i < n; i++) {
                    double x = signal[i + 1] * window_at(window, i, n);
                    real += x * cos(2.0 * PI * k * i / n);
                    imag -= x * sin(2.0 * PI * k * i / n);
                }
                NEAR(actual[k].real, real, n * 2e-7);
                NEAR(actual[k].imag, imag, n * 2e-7);
            }
            CHECK(LND_FftExecute(fft, &pcm, 3, 1, actual, 66) == LND_ERR_INVALID_ARG);
            CHECK(LND_FftExecute(fft, &pcm, 0, 2, actual, 66) == LND_ERR_INVALID_ARG);
            CHECK(LND_FftExecute(fft, &pcm, 0, 1, actual, 1) == LND_ERR_INVALID_ARG);
            LND_FftFree(fft);
            CHECK(LND_FftGetFrames(fft) == 0);
            free(memory);
        }
    }
}

static void test_spectrum(void) {
    float signal[256], spectrum[129];
    LND_COMPLEX bins[129];
    LND_PCM pcm = {.data = signal, .frames = 256, .channels = 1, .format = LND_FORMAT_F32};
    for (int window = 0; window <= 3; window++) {
        LND_FFT *fft = LND_FftCreate(256, window);
        CHECK(fft != nullptr);
        for (unsigned i = 0; i < 256; i++) signal[i] = (float)(0.5 * sin(2.0 * PI * 16 * i / 256));
        CHECK(LND_FftExecute(fft, &pcm, 0, 0, bins, 129) == LND_OK);
        CHECK(LND_FftSpectrum(fft, bins, spectrum, 129, false) == LND_OK);
        NEAR(spectrum[16], 0.5, 2e-7);
        CHECK(LND_FftSpectrum(fft, bins, spectrum, 129, true) == LND_OK);
        NEAR(spectrum[16], -6.020599913, 2e-6);
        for (unsigned i = 0; i < 256; i++) signal[i] = 0.25f;
        CHECK(LND_FftExecute(fft, &pcm, 0, 0, bins, 129) == LND_OK);
        CHECK(LND_FftSpectrum(fft, bins, spectrum, 129, false) == LND_OK);
        NEAR(spectrum[0], 0.25, 2e-7);
        for (unsigned i = 0; i < 256; i++) signal[i] = i % 2 ? -0.75f : 0.75f;
        CHECK(LND_FftExecute(fft, &pcm, 0, 0, bins, 129) == LND_OK);
        CHECK(LND_FftSpectrum(fft, bins, spectrum, 129, false) == LND_OK);
        NEAR(spectrum[128], 0.75, 2e-7);
        signal[0] = INFINITY;
        CHECK(LND_FftExecute(fft, &pcm, 0, 0, bins, 129) == LND_ERR_FORMAT);
        LND_FftFree(fft);
    }
    CHECK(LND_FftGetMemoryBytes(0) == 0 && LND_FftGetMemoryBytes(1) == 0 && LND_FftGetMemoryBytes(3) == 0);
    CHECK(LND_FftInit(nullptr, 0, 16, 0) == nullptr);
    CHECK(LND_FftCreate(16, 99) == nullptr);
    size_t bytes = LND_FftGetMemoryBytes(16);
    void *memory = malloc(bytes);
    CHECK(LND_FftInit(memory, bytes - 1, 16, 0) == nullptr);
    CHECK(LND_FftInit((char *)memory + 1, bytes - 1, 8, 0) == nullptr);
    free(memory);
}

int main(void) {
    test_levels();
    test_fft_reference();
    test_spectrum();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
