#include "lindar_analysis.h"
#include <stdio.h>

int main(void) {
    int16_t samples[] = {-16384, 8192, 16384, -8192};
    LND_PCM pcm = {.data = samples, .frames = 2, .channels = 2, .format = LND_FORMAT_S16LE};
    for (uint32_t channel = 0; channel < 2; channel++) {
        LND_LEVELS levels;
        if (LND_PcmAnalyzeLevels(&pcm, 0, pcm.frames, channel, &levels) != LND_OK) return 1;
        printf("channel %u: peak=%.2f dBFS, RMS=%.2f dBFS\n", channel, levels.peak_dbfs, levels.rms_dbfs);
    }
    return 0;
}
