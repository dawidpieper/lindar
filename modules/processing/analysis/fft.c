#include "lindar_analysis.h"
#include "src/alloc.h"
#include "src/error.h"
#include "src/pcm.h"

#include <math.h>
#include <string.h>

#define LND_FFT_MAGIC 0x4c4e4446u
#define LND_PI 3.14159265358979323846

struct LND_FFT {
    uint32_t magic;
    uint32_t frames;
    bool owned;
    float scale;
    uint32_t *reverse;
    float *window;
    LND_COMPLEX *twiddle;
    LND_COMPLEX *work;
};

size_t LND_FftGetMemoryBytes(uint32_t frames) {
    if (frames < 2 || (frames & (frames - 1))) return 0;
    size_t unit = sizeof(uint32_t) + sizeof(float) + sizeof(LND_COMPLEX) + sizeof(LND_COMPLEX) / 2;
    if (frames > (SIZE_MAX - sizeof(LND_FFT)) / unit) return 0;
    return sizeof(LND_FFT) + (size_t)frames * unit;
}

LND_FFT *LND_FftInit(void *memory, size_t bytes, uint32_t frames, int32_t window) {
    size_t need = LND_FftGetMemoryBytes(frames);
    if (!need || !memory || bytes < need || (uintptr_t)memory % alignof(LND_FFT) || window < LND_WINDOW_RECTANGULAR || window > LND_WINDOW_BLACKMAN)
        return lnd_error_null(LND_ERR_INVALID_ARG);
    LND_FFT *fft = memory;
    *fft = (LND_FFT){.magic = LND_FFT_MAGIC, .frames = frames};
    fft->reverse = (uint32_t *)(fft + 1);
    fft->window = (float *)(fft->reverse + frames);
    fft->twiddle = (LND_COMPLEX *)(fft->window + frames);
    fft->work = fft->twiddle + frames / 2;
    uint32_t bits = 0;
    for (uint32_t n = frames; n > 1; n >>= 1) bits++;
    double sum = 0.0;
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t r = 0, v = i;
        for (uint32_t b = 0; b < bits; b++, v >>= 1) r = (r << 1) | (v & 1);
        fft->reverse[i] = r;
        double phase = 2.0 * LND_PI * i / frames;
        double w = 1.0;
        if (window == LND_WINDOW_HANN) w = 0.5 - 0.5 * cos(phase);
        else if (window == LND_WINDOW_HAMMING) w = 0.54 - 0.46 * cos(phase);
        else if (window == LND_WINDOW_BLACKMAN) w = 0.42 - 0.5 * cos(phase) + 0.08 * cos(2.0 * phase);
        fft->window[i] = (float)w;
        sum += fft->window[i];
        if (i < frames / 2) fft->twiddle[i] = (LND_COMPLEX){(float)cos(phase), (float)-sin(phase)};
    }
    fft->scale = (float)(1.0 / sum);
    return fft;
}

LND_FFT *LND_FftCreate(uint32_t frames, int32_t window) {
    size_t bytes = LND_FftGetMemoryBytes(frames);
    if (!bytes || window < LND_WINDOW_RECTANGULAR || window > LND_WINDOW_BLACKMAN) return lnd_error_null(LND_ERR_INVALID_ARG);
    void *memory = lnd_alloc(bytes);
    if (!memory) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    LND_FFT *fft = LND_FftInit(memory, bytes, frames, window);
    if (!fft) lnd_free(memory);
    else fft->owned = true;
    return fft;
}

void LND_FftFree(LND_FFT *fft) {
    if (!fft || fft->magic != LND_FFT_MAGIC) return;
    bool owned = fft->owned;
    fft->magic = 0;
    if (owned) lnd_free(fft);
}

uint32_t LND_FftGetFrames(const LND_FFT *fft) { return fft && fft->magic == LND_FFT_MAGIC ? fft->frames : 0; }

uint32_t LND_FftGetBinCount(const LND_FFT *fft) {
    uint32_t frames = LND_FftGetFrames(fft);
    return frames ? frames / 2 + 1 : 0;
}

int32_t LND_FftExecute(LND_FFT *fft, const LND_PCM *pcm, size_t offset, uint32_t channel, LND_COMPLEX *bins, size_t bins_count) {
    uint32_t frames = LND_FftGetFrames(fft);
    if (!frames || !bins || bins_count < (size_t)frames / 2 + 1 || !lnd_pcm_range(pcm, offset, frames) || channel >= pcm->channels)
        return lnd_error(LND_ERR_INVALID_ARG);
    const uint8_t *p = lnd_pcm_at(pcm, channel, offset);
    size_t stride = lnd_pcm_stride(pcm);
    for (uint32_t i = 0; i < frames; i++) {
        double sample = lnd_pcm_load_sample(p + (size_t)i * stride, pcm->format);
        float value = (float)sample;
        if (!isfinite(value)) return lnd_error(LND_ERR_FORMAT);
        fft->work[fft->reverse[i]] = (LND_COMPLEX){value * fft->window[i], 0.0f};
    }
    for (uint32_t width = 2;; width <<= 1) {
        uint32_t half = width / 2, step = frames / width;
        for (uint32_t base = 0; base < frames; base += width) {
            for (uint32_t j = 0; j < half; j++) {
                LND_COMPLEX w = fft->twiddle[j * step];
                LND_COMPLEX a = fft->work[base + j], b = fft->work[base + j + half];
                float real = w.real * b.real - w.imag * b.imag;
                float imag = w.real * b.imag + w.imag * b.real;
                fft->work[base + j] = (LND_COMPLEX){a.real + real, a.imag + imag};
                fft->work[base + j + half] = (LND_COMPLEX){a.real - real, a.imag - imag};
            }
        }
        if (width == frames) break;
    }
    memcpy(bins, fft->work, ((size_t)frames / 2 + 1) * sizeof *bins);
    return LND_OK;
}

int32_t LND_FftSpectrum(const LND_FFT *fft, const LND_COMPLEX *bins, float *amplitudes, size_t count, bool dbfs) {
    uint32_t n = LND_FftGetBinCount(fft);
    if (!n || !bins || !amplitudes || count != n) return lnd_error(LND_ERR_INVALID_ARG);
    for (uint32_t i = 0; i < n; i++) {
        float factor = i == 0 || i == n - 1 ? fft->scale : 2.0f * fft->scale;
        float value = hypotf(bins[i].real, bins[i].imag) * factor;
        amplitudes[i] = dbfs ? 20.0f * log10f(value) : value;
    }
    return LND_OK;
}
