#include "lindar_graph.h"
#include "lindar.h"

#include <stdio.h>

static int64_t generate(void *user, void *dst, uint64_t frames) {
    uint32_t *phase = user;
    float *out = dst;
    for (uint64_t i = 0; i < frames; i++) {
        float value = (float)(*phase % 100) * 0.01f - 0.5f;
        out[i * 2] = value;
        out[i * 2 + 1] = -value;
        (*phase)++;
    }
    return frames;
}

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0);
    if (LND_LibraryInit() != LND_OK) return 1;
    uint32_t phase = 0;
    LND_SOURCE_PROCS procs = {.read = generate};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &phase, LND_FORMAT_F32, 2, 48000, 0);
    LND_SOUND *sound = source ? LND_SourceEnsureSound(source, nullptr) : nullptr;
    LND_RENDERER *renderer = sound ? LND_RendererCreateNode(LND_SoundEnsureNode(sound)) : nullptr;
    if (!renderer || LND_SoundPlay(sound) != LND_OK) {
        LND_LibraryFree();
        return 1;
    }
    int16_t samples[512 * 2];
    LND_PCM output = {.data = samples, .frames = 512, .channels = 2, .format = LND_FORMAT_S16LE};
    int32_t result = LND_RendererFillPcm(renderer, &output, 0, output.frames);
    if (result == LND_OK) printf("%zu frames, %u Hz, %u channels\n", output.frames, LND_RendererGetSampleRateHz(renderer), output.channels);
    LND_RendererFree(renderer);
    LND_SourceFree(source);
    LND_LibraryFree();
    return result == LND_OK ? 0 : 1;
}
