#include "lindar_queue.h"
#include "lindar.h"

#include <stdio.h>

enum { RATE = 16000, BLOCK = 32, CAPACITY = 128 };

static alignas(max_align_t) unsigned char queue_memory[4096], sound_memory[512], renderer_memory[4096];
static int16_t input[BLOCK], output[BLOCK];

int main(void) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) != LND_OK ||
        LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, LND_LAYOUT_INTERLEAVED) != LND_OK || LND_LibraryInit() != LND_OK)
        return 1;
    LND_SOURCE *source = LND_QueueInit(queue_memory, sizeof queue_memory, 1, RATE, CAPACITY);
    LND_SOUND *sound = source ? LND_SoundInit(sound_memory, sizeof sound_memory, source) : nullptr;
    LND_RENDERER_CONFIG config = {.render = LND_SoundRenderPcm, .user = sound, .channels = 1, .sample_rate_hz = RATE, .block_frames = BLOCK};
    LND_RENDERER *renderer = sound ? LND_RendererInit(renderer_memory, sizeof renderer_memory, &config) : nullptr;
    LND_PCM in = {.data = input, .frames = BLOCK, .channels = 1, .format = LND_FORMAT_S16};
    LND_PCM out = {.data = output, .frames = BLOCK, .channels = 1, .format = LND_FORMAT_S16};
    int result = 1;
    uint32_t phase = 0, stalls = 0;
    uint64_t energy = 0;
    if (!renderer || LND_SoundPlay(sound) != LND_OK) goto done;
    for (uint32_t block = 0; block < 100; block++) {
        if (block % 8) {
            for (uint32_t i = 0; i < BLOCK; i++) {
                phase = (phase + 1802) & 65535u;
                input[i] = (int16_t)((phase < 32768 ? phase : 65535 - phase) - 16384);
            }
            if (LND_QueueWritePcm(source, &in, 0, BLOCK) != BLOCK) goto done;
        }
        if (LND_RendererFillPcm(renderer, &out, 0, BLOCK) != LND_OK) goto done;
        stalls += LND_SoundGetState(sound) == LND_SOUND_STALLED;
        for (uint32_t i = 0; i < BLOCK; i++)
            energy += (uint64_t)((int32_t)output[i] * output[i]);
    }
    if (LND_SourceEnd(source) != LND_OK) goto done;
    do {
        if (LND_RendererFillPcm(renderer, &out, 0, BLOCK) != LND_OK) goto done;
    } while (LND_SoundGetState(sound) != LND_SOUND_STOPPED);
    printf("frames=%llu stalls=%u energy=%llu EOF=%d\n", (unsigned long long)LND_SourceGetPositionFrames(source), stalls, (unsigned long long)energy,
           LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    result = 0;
done:
    LND_LibraryFree();
    return result;
}
