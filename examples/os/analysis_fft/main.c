#include "lindar_analysis.h"
#include <math.h>
#include <stdio.h>

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (LND_LibraryInit() != LND_OK) return 1;
    float samples[256], amplitudes[129];
    LND_COMPLEX bins[129];
    for (size_t i = 0; i < 256; i++) samples[i] = (float)(0.5 * sin(6.283185307179586 * 8 * i / 256));
    LND_PCM pcm = {.data = samples, .frames = 256, .channels = 1, .format = LND_FORMAT_F32};
    LND_FFT *fft = LND_FftCreate(256, LND_WINDOW_HANN);
    int result = 1;
    if (!fft || LND_FftExecute(fft, &pcm, 0, 0, bins, 129) != LND_OK || LND_FftSpectrum(fft, bins, amplitudes, 129, false) != LND_OK) goto done;
    size_t peak = 1;
    for (size_t i = 2; i < 129; i++) if (amplitudes[i] > amplitudes[peak]) peak = i;
    printf("peak: %.1f Hz, %.2f dBFS\n", peak * 8192.0 / 256, LND_AmplitudeToDb(amplitudes[peak]));
    result = peak == 8 ? 0 : 1;
done:
    LND_FftFree(fft);
    LND_LibraryFree();
    return result;
}
