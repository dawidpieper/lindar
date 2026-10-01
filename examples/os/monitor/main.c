#include "lindar_monitor.h"
#include <stdio.h>

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_NODE *bus = LND_NodeCreateBus(2, 48000);
    LND_RENDERER *renderer = bus ? LND_RendererCreateNode(bus) : nullptr;
    int result = 1;
    if (!renderer || LND_NodeSetMonitoring(bus, true) != LND_OK) goto done;
    int16_t samples[512];
    LND_PCM pcm = {.data = samples, .frames = 256, .channels = 2, .format = LND_FORMAT_S16LE};
    for (unsigned i = 0; i < 100; i++) if (LND_RendererFillPcm(renderer, &pcm, 0, 256) != LND_OK) goto done;
    LND_NODE_STATS stats;
    if (LND_NodeGetStats(bus, &stats) != LND_OK) goto done;
    printf("calls=%llu frames=%llu self=%llu ns peak=%llu ns\n", (unsigned long long)stats.calls,
           (unsigned long long)stats.frames, (unsigned long long)stats.self_ns, (unsigned long long)stats.peak_ns);
    result = 0;
done:
    LND_RendererFree(renderer);
    LND_LibraryFree();
    return result;
}
