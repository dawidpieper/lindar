#include "lindar_effects.h"
#include "lindar.h"
#include "lindar_slide.h"
#include "src/atomic.h"
#include "src/thread.h"

#include <math.h>
#include <stdio.h>

typedef struct state {
    LND_NODE *node;
    LND_RENDERER *renderer;
    lnd_atomic_u32 stop, blocks, errors, probed;
    lnd_event started;
    uint64_t position;
    int32_t layout;
} state;

static void check(state *s, bool ok) {
    if (!ok) lnd_add(&s->errors, 1);
}

static int64_t input(void *user, void *dst, uint64_t frames) {
    state *s = user;
    if (!lnd_exchange(&s->probed, 1)) {
        LND_EFFECT_INFO info;
        check(s, LND_NodeResetEffect(s->node) == LND_ERR_BUSY);
        check(s, LND_NodeSetEffectChannelMask(s->node, 1) == LND_ERR_BUSY);
        check(s, LND_NodeGetEffectInfo(s->node, &info) == LND_ERR_BUSY);
        check(s, LND_NodeSetParam(s->node, LND_EFFECT_PARAM_WET, 0.5f) == LND_ERR_BUSY);
        check(s, !LND_NodeCreateFlanger(2, 48000, nullptr) && LND_ErrorGetLast() == LND_ERR_BUSY);
    }
    float *out = dst;
    for (uint64_t i = 0; i < frames; i++) {
        out[i * 2] = (float)(0.1 * sin(0.07 * s->position++));
        out[i * 2 + 1] = -out[i * 2];
    }
    return (int64_t)frames;
}

static void render(void *user) {
    state *s = user;
    float data[256];
    void *planes[] = {data, data + 128};
    LND_PCM pcm = {.data = data, .planes = planes, .frames = 128, .channels = 2, .format = LND_FORMAT_F32, .layout = s->layout};
    do {
        check(s, LND_RendererReadPcm(s->renderer, &pcm, 0, 128) == 128);
        for (size_t i = 0; i < 256; i++)
            check(s, isfinite(data[i]));
        if (!lnd_add(&s->blocks, 1)) lnd_event_signal(&s->started);
    } while (!lnd_load(&s->stop));
}

#define REQUIRE(x)                                                                                                                                             \
    do {                                                                                                                                                       \
        if (!(x)) {                                                                                                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                                                                         \
            return 1;                                                                                                                                          \
        }                                                                                                                                                      \
    } while (0)

static int run(int32_t format, int32_t layout, int32_t external) {
    state s = {.layout = external};
    REQUIRE(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, format) == LND_OK);
    REQUIRE(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, layout) == LND_OK);
    REQUIRE(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_MANUAL) == LND_OK && LND_LibraryInit() == LND_OK);
    REQUIRE(lnd_event_init(&s.started) == LND_OK);
    LND_SOURCE_PROCS procs = {.read = input};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &s, LND_FORMAT_F32, 2, 48000, LND_GRAPH_SOURCE_DIRECT | LND_SOURCE_LIVE);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    s.node = LND_NodeCreateFlanger(2, 48000, nullptr);
    REQUIRE(source && sound && s.node && LND_NodeConnect(LND_SourceEnsureNode(source), s.node) == LND_OK);
    s.renderer = LND_RendererCreateNode(s.node);
    REQUIRE(s.renderer && LND_SoundPlay(sound) == LND_OK);
    lnd_thread worker = {0};
    REQUIRE(lnd_thread_create(&worker, render, &s) == LND_OK);
    REQUIRE(lnd_event_wait(&s.started, 3000));
    for (uint32_t i = 0; i < 1000; i++) {
        check(&s, LND_NodeSetParam(s.node, LND_EFFECT_PARAM_FEEDBACK, i & 1 ? 0.7f : -0.7f) == LND_OK);
        check(&s, LND_NodeSetEffectChannelMask(s.node, i % 4) == LND_OK);
        LND_EFFECT_INFO info;
        check(&s, LND_NodeGetEffectInfo(s.node, &info) == LND_OK && !info.error);
        if (!(i % 7)) check(&s, LND_NodeResetEffect(s.node) == LND_OK);
        LND_SLIDE_CONFIG slide = {.duration_frames = 1000, .step_frames = 32, .curve = LND_SLIDE_LINEAR};
        int32_t result = LND_NodeSlideParam(s.node, LND_EFFECT_PARAM_WET, i & 1 ? 0.2f : 0.8f, &slide);
        check(&s, result == LND_OK || result == LND_ERR_BUSY);
    }
    lnd_store(&s.stop, 1);
    lnd_thread_join(&worker);
    unsigned errors = lnd_load(&s.errors);
    printf("Effects concurrent controls: %u blocks, %u errors\n", lnd_load(&s.blocks), errors);
    REQUIRE(LND_RendererFree(s.renderer) == LND_OK && LND_NodeFree(s.node) == LND_OK && LND_SourceFree(source) == LND_OK);
    LND_LibraryFree();
    lnd_event_free(&s.started);
    return errors != 0;
}

int main(void) {
    int errors = 0;
    for (int format = 0; format < 2; format++)
        for (int layout = 0; layout < 2; layout++)
            for (int external = 0; external < 2; external++)
                errors |= run(format ? LND_FORMAT_S16 : LND_FORMAT_F32, layout, external);
    return errors;
}
