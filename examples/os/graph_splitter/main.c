#include "lindar_graph.h"
#include "lindar.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16LE);
    LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0);
    if (LND_LibraryInit() != LND_OK) return 1;
    int16_t input[] = {100, 200, 300, 400}, outputs[2][4];
    LND_PCM pcm = {.data = input, .frames = 4, .channels = 1, .format = LND_FORMAT_S16LE};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .channels = 1, .sample_rate_hz = 8000});
    LND_NODE *split = LND_NodeCreateSplitter(1, 8000, 2);
    int result = 1;
    if (!source || !split || LND_NodeConnect(LND_SourceEnsureNode(source), split) != LND_OK || LND_SoundPlay(LND_SourceGetSound(source)) != LND_OK) goto done;
    for (unsigned i = 0; i < 2; i++) {
        LND_NODE *branch = LND_NodeGetSplitterOutput(split, i);
        if (LND_SoundPlay(LND_NodeEnsureSound(branch, nullptr)) != LND_OK) goto done;
        LND_RENDERER *renderer = LND_RendererCreateNode(branch);
        pcm.data = outputs[i];
        int64_t got = renderer ? LND_RendererReadPcm(renderer, &pcm, 0, 4) : -1;
        LND_RendererFree(renderer);
        if (got != 4) goto done;
        printf("branch %u: %lld frames, dropped=%llu\n", i, (long long)got, (unsigned long long)LND_NodeGetSplitDroppedFrames(branch));
    }
    result = memcmp(outputs[0], outputs[1], sizeof outputs[0]) != 0;
done:
    LND_LibraryFree();
    return result;
}
