#include "lindar_slide.h"
#include "lindar.h"
#include "lindar_soundtouch.h"
#include "playback/graph/context.h"
#include "playback/graph/node.h"
#include "src/thread.h"
#include <stdio.h>
#include <string.h>

typedef struct measurement {
    LND_RENDERER *renderer;
    lnd_atomic_u32 done;
    uint64_t calls, errors, busy, nonzero, total_ns, maximum_ns;
} measurement;
static int64_t generate(void *user, void *data, uint64_t frames) {
    float *pcm = data;
    for (uint64_t i = 0; i < frames * 2; i++) pcm[i] = i & 1 ? -0.1f : 0.1f;
    return (int64_t)frames;
}
static void render(void *user) {
    measurement *m = user;
    float data[256 * 2];
    LND_PCM pcm = {.data = data, .frames = 256, .channels = 2, .format = LND_FORMAT_F32};
    while (!lnd_load(&m->done)) {
        uint64_t start = lnd_time_ns();
        int32_t result = LND_RendererFillPcm(m->renderer, &pcm, 0, 256);
        uint64_t elapsed = lnd_time_ns() - start;
        m->calls++;
        m->nonzero += result == LND_OK && data[0] != 0;
        m->total_ns += elapsed;
        if (elapsed > m->maximum_ns) m->maximum_ns = elapsed;
        if (result == LND_ERR_BUSY) m->busy++;
        else if (result != LND_OK) m->errors++;
    }
}
static int measure(const char *name, LND_NODE *root, LND_NODE *control, unsigned mode) {
    measurement m = {.renderer = LND_RendererCreateNode(root)};
    if (!m.renderer) return 1;
    lnd_thread thread = {0};
    if (lnd_thread_create(&thread, render, &m) != LND_OK) return 1;
    uint64_t start = lnd_time_ns(), maximum = 0, calls = 0, busy = 0, errors = 0;
    while (lnd_time_ns() - start < 250000000) {
        uint64_t before = lnd_time_ns();
        int32_t result = LND_OK;
        if (mode == 1) {
            LND_SLIDE_CONFIG slide = {.duration_frames = 48000, .curve = LND_SLIDE_LOGARITHMIC};
            result = LND_NodeSlideParam(control, LND_PARAM_GAIN, calls & 1 ? 0.5f : 1.0f, &slide);
        } else if (mode == 2) {
            lnd_context_lock();
            result = lnd_node_reconfigure(control, calls & 1 ? 1 : 2, calls & 1 ? 44100 : 48000);
            lnd_context_unlock();
        }
#if LND_MODULE_SOUNDTOUCH
        else if (mode == 3)
            result = LND_NodeSetSoundTouchSetting(control, LND_SOUNDTOUCH_QUICK_SEEK, (int32_t)(calls & 1));
#endif
        else lnd_sleep_ms(1);
        uint64_t elapsed = lnd_time_ns() - before;
        if (mode && elapsed > maximum) maximum = elapsed;
        calls++;
        if (result == LND_ERR_BUSY) busy++;
        else if (result != LND_OK) errors++;
    }
    lnd_store(&m.done, 1);
    lnd_thread_join(&thread);
    printf("%s,%llu,%.2f,%.2f,%llu,%.2f,%llu,%llu,%llu,%llu\n", name, (unsigned long long)m.calls, m.calls ? (double)m.total_ns / m.calls / 1000 : 0,
           (double)m.maximum_ns / 1000, (unsigned long long)calls, (double)maximum / 1000, (unsigned long long)busy, (unsigned long long)m.busy,
           (unsigned long long)(errors + m.errors), (unsigned long long)m.nonzero);
    LND_RendererFree(m.renderer);
    return !m.calls || errors || m.errors || (mode < 2 && !m.nonzero);
}
int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_MANUAL);
    LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_SOURCE_PROCS procs = {.read = generate};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, nullptr, LND_FORMAT_F32, 2, 48000, LND_GRAPH_SOURCE_DIRECT);
    LND_NODE *input = LND_SourceEnsureNode(source), *control = LND_NodeCreateBus(2, 48000), *root = LND_NodeCreateBus(2, 48000);
    if (!input || !control || !root || LND_NodeConnect(input, control) != LND_OK || LND_NodeConnect(control, root) != LND_OK) return 1;
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    if (!sound || LND_SoundPlay(sound) != LND_OK) return 1;
    puts("mode,render_calls,render_mean_us,render_max_us,control_calls,control_max_us,control_busy,render_busy,errors,nonzero_blocks");
    int result = measure("baseline", root, control, 0);
    result |= measure("slide", root, control, 1);
    result |= measure("reconfigure", root, control, 2);
#if LND_MODULE_SOUNDTOUCH
    LND_NODE *touch = LND_NodeCreateSoundTouch(2, 48000, nullptr);
    if (touch) {
        result |= measure("soundtouch_empty", touch, touch, 3);
        LND_NodeFree(touch);
    } else result = 1;
#endif
    LND_SourceFree(source);
    LND_NodeFree(control);
    LND_NodeFree(root);
    LND_LibraryFree();
    return result;
}
