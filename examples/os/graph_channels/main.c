#include "lindar_graph.h"
#include "lindar.h"
#include <stdio.h>

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16LE);
    LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0);
    if (LND_LibraryInit() != LND_OK) return 1;
    int16_t input[] = {100, 900, 200, 800}, output[4];
    LND_PCM pcm = {.data = input, .frames = 2, .channels = 2, .format = LND_FORMAT_S16LE};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .channels = 2, .sample_rate_hz = 8000});
    LND_NODE *split = LND_NodeCreateChannelSplitter(2, 8000), *merge = LND_NodeCreateChannelMerger(2, 8000);
    int result = 1;
    if (!source || !split || !merge || LND_NodeConnect(LND_SourceEnsureNode(source), split) != LND_OK) goto done;
    if (LND_SoundPlay(LND_SourceGetSound(source)) != LND_OK) goto done;
    for (uint32_t i = 0; i < 2; i++) {
        LND_NODE *channel = LND_NodeGetSplitterOutput(split, i);
        if (LND_NodeConnect(channel, merge) != LND_OK || LND_NodeSetChannelMergerInput(merge, 1 - i, channel) != LND_OK) goto done;
        if (LND_SoundPlay(LND_NodeEnsureSound(channel, nullptr)) != LND_OK) goto done;
    }
    LND_RENDERER *renderer = LND_RendererCreateNode(merge);
    pcm.data = output;
    if (renderer && LND_RendererReadPcm(renderer, &pcm, 0, 2) == 2) {
        printf("swapped: %d %d | %d %d\n", output[0], output[1], output[2], output[3]);
        result = 0;
    }
    LND_RendererFree(renderer);
done:
    LND_LibraryFree();
    return result;
}
