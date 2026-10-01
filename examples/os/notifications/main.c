#include "lindar_notify.h"
#include "lindar.h"
#include "lnd_modules.h"
#if LND_MODULE_GRAPH
#include "lindar_graph.h"
#endif

#include <stdio.h>

typedef struct app {
    unsigned ended, positions;
} app;

static void notified(void *user, const LND_NOTIFICATION *event) {
    app *a = user;
    if (event->type == LND_NOTIFY_END)
        a->ended++;
    if (event->type == LND_NOTIFY_POSITION)
        a->positions++;
    printf("event=%d frame=%llu rate=%u\n", event->type, (unsigned long long)event->position_frames, event->sample_rate_hz);
}

#if LND_MODULE_GRAPH
static void process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t rate) {
    (void)rate;
    float gain = *(float *)user;
    for (size_t i = 0; i < (size_t)frames * channels; i++)
        pcm[i] *= gain;
}
#endif

int main(void) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) != LND_OK ||
        LND_LibraryInit() != LND_OK)
        return 1;
    int16_t input[32], output[8];
    for (unsigned i = 0; i < 32; i++)
        input[i] = i & 1 ? 4096 : -4096;
    LND_PCM pcm = {.data = input, .frames = 32, .channels = 1, .format = LND_FORMAT_S16};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .channels = 1, .sample_rate_hz = 8000});
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    app a = {0};
    LND_SUBSCRIPTION_CONFIG config = {.type = LND_NOTIFY_END, .proc = notified, .user = &a};
    LND_SUBSCRIPTION *end = LND_SoundSubscribe(sound, &config);
    config.type = LND_NOTIFY_POSITION;
    config.position_frames = 17;
    config.flags = LND_NOTIFY_ONCE;
    LND_SUBSCRIPTION *position = LND_SoundSubscribe(sound, &config);
    LND_RENDERER *renderer = nullptr;
#if LND_MODULE_GRAPH
    float gain = 0.5f;
    LND_NODE *effect = LND_NodeCreateProcessorProc(process, &gain, 1, 8000, LND_PROCESSOR_BOUNDED);
    if (effect && LND_NodeConnect(LND_SoundEnsureNode(sound), effect) == LND_OK)
        renderer = LND_RendererCreateNode(effect);
#else
    renderer = LND_RendererCreateProc(&(LND_RENDERER_CONFIG){.render = LND_SoundRenderPcm, .user = sound, .channels = 1, .sample_rate_hz = 8000, .block_frames = 8});
#endif
    int result = 1;
    if (source && sound && end && position && renderer && LND_SoundPlay(sound) == LND_OK) {
        pcm = (LND_PCM){.data = output, .frames = 8, .channels = 1, .format = LND_FORMAT_S16};
        for (unsigned i = 0; i < 8 && !a.ended; i++) {
            if (LND_RendererFillPcm(renderer, &pcm, 0, 8) != LND_OK || LND_LibraryUpdate() != LND_OK)
                break;
        }
        result = a.ended != 1 || a.positions != 1;
    }
    LND_LibraryFree();
    return result;
}
