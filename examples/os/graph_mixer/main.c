#include "lindar_graph.h"
#include "lindar.h"
#include <stdio.h>

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16LE);
    LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0);
    if (LND_LibraryInit() != LND_OK) return 1;
    int16_t a[] = {1000, 2000, 3000, 4000}, b[] = {4000, 3000, 2000, 1000}, out[4];
    LND_PCM inputs[] = {{.data = a, .frames = 4, .channels = 1, .format = LND_FORMAT_S16LE},
                        {.data = b, .frames = 4, .channels = 1, .format = LND_FORMAT_S16LE}};
    LND_NODE *mixer = LND_NodeCreateMixer(1, 8000, LND_MIX_LOCKSTEP | LND_MIX_END);
    int result = 1;
    if (!mixer) goto done;
    for (unsigned i = 0; i < 2; i++) {
        LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &inputs[i], .channels = 1, .sample_rate_hz = 8000});
        LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
        if (!sound || LND_NodeConnect(LND_SourceEnsureNode(source), mixer) != LND_OK || LND_SoundPlay(sound) != LND_OK) goto done;
    }
    LND_RENDERER *renderer = LND_RendererCreateNode(mixer);
    LND_PCM pcm = {.data = out, .frames = 4, .channels = 1, .format = LND_FORMAT_S16LE};
    if (renderer && LND_RendererReadPcm(renderer, &pcm, 0, 4) == 4) {
        printf("mixed: %d %d %d %d\n", out[0], out[1], out[2], out[3]);
        result = 0;
    }
    LND_RendererFree(renderer);
done:
    LND_LibraryFree();
    return result;
}
