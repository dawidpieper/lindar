#include "lindar.h"
#include <stdio.h>

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16LE);
    if (LND_LibraryInit() != LND_OK) return 1;
    uint8_t samples[16] = {0};
    LND_PCM pcm = {.data = samples, .frames = 8, .channels = 1, .format = LND_FORMAT_S16LE};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 8000};
    size_t source_bytes = LND_SourceGetMemoryBytes(&config), sound_bytes = LND_SoundGetMemoryBytes();
    max_align_t source_memory[(source_bytes + sizeof(max_align_t) - 1) / sizeof(max_align_t)];
    max_align_t sound_memory[(sound_bytes + sizeof(max_align_t) - 1) / sizeof(max_align_t)];
    LND_SOURCE *source = LND_SourceInit(source_memory, sizeof source_memory, &config);
    LND_SOUND *sound = LND_SoundInit(sound_memory, sizeof sound_memory, source);
    int result = 1;
    if (!sound || LND_SoundSetGainQ16(sound, 32768) != LND_OK || LND_SoundPlay(sound) != LND_OK) goto done;
    uint8_t output[8];
    LND_PCM dst = {.data = output, .frames = 4, .channels = 1, .format = LND_FORMAT_S16LE};
    if (LND_SoundRenderPcm(sound, &dst, 0, 4) != 4 || LND_SoundSetPause(sound, true) != LND_OK) goto done;
    printf("paused at %llu\n", (unsigned long long)LND_SoundGetPositionFrames(sound));
    if (LND_SoundSeekFrames(sound, 6) != LND_OK || LND_SoundSetLoop(sound, true) != LND_OK || LND_SoundSetPause(sound, false) != LND_OK) goto done;
    if (LND_SoundRenderPcm(sound, &dst, 0, 4) != 4 || LND_SoundStop(sound) != LND_OK) goto done;
    printf("stopped at %llu\n", (unsigned long long)LND_SoundGetPositionFrames(sound));
    result = 0;
done:
    LND_SoundFree(sound);
    LND_SourceFree(source);
    LND_LibraryFree();
    return result;
}
