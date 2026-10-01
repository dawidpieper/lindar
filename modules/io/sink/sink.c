#include "lindar_sink.h"
#include "io/devices/context.h"
#include "src/error.h"
#include "playback/graph/context.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/context.h"
#include "src/platform.h"
#include "src/thread.h"
#include "io/devices/capture.h"
#include "io/devices/engine.h"
#include "playback/graph/node.h"
#include "io/output/output.h"

typedef struct lnd_sink_node {
    lnd_node base;
    lnd_atomic(lnd_output *) output;
    lnd_instance *clock;
    lnd_capture *ring;
    float *stage;
    float *chunk;
    uint64_t acc;
    lnd_thread thread;
    bool started;
    struct lnd_sink_node *next_sink;
} lnd_sink_node;

static void lnd_sink_thread(void *user) {
    lnd_sink_node *s = user;
    uint32_t block = s->base.block;
    for (;;) {
        uint64_t got = lnd_capture_read(s->ring, s->chunk, block, LND_CAPTURE_READ_WAIT);
        if (got) {
            LND_OutputWrite(s->output, s->chunk, LND_FORMAT_F32, got);
            continue;
        }
        if (lnd_load(&s->ring->ended)) break;
    }
}

static void lnd_sink_attach(lnd_sink_node *s) {
    lnd_instance *i = s->clock;
    lnd_spinlock_lock(&i->sinks_lock);
    s->next_sink = i->sinks;
    i->sinks = s;
    lnd_spinlock_unlock(&i->sinks_lock);
}

static void lnd_sink_unclock(lnd_sink_node *s) {
    lnd_instance *i = s->clock;
    if (!i) return;
    lnd_spinlock_lock(&i->sinks_lock);
    for (lnd_sink_node **p = &i->sinks; *p; p = &(*p)->next_sink) {
        if (*p == s) {
            *p = s->next_sink;
            break;
        }
    }
    s->clock = nullptr;
    lnd_spinlock_unlock(&i->sinks_lock);
}

static void lnd_sink_stop(lnd_sink_node *s) {
    lnd_sink_unclock(s);
    if (s->started) {
        lnd_capture_end(s->ring);
        lnd_thread_join(&s->thread);
        s->started = false;
    }
    if (s->output) LND_OutputFlush(s->output);
    s->output = nullptr;
}

static void lnd_sink_destroy(lnd_node *n) {
    lnd_sink_node *s = (lnd_sink_node *)n;
    lnd_sink_stop(s);
    lnd_capture_free(s->ring);
    lnd_free_aligned(s->stage);
    lnd_free_aligned(s->chunk);
}

static const lnd_node_vt lnd_sink_vt = {
    .render = lnd_bus_render,
    .destroy = lnd_sink_destroy,
};

lnd_node *lnd_node_create_sink(lnd_output *output, lnd_instance *clock) {
    lnd_sink_node *s = (lnd_sink_node *)lnd_node_alloc(sizeof *s, &lnd_sink_vt, LND_NODE_TERMINAL, output->channels, output->sample_rate_hz);
    if (!s) return nullptr;
    uint32_t block = s->base.block;
    uint64_t scaled = (uint64_t)clock->cfg.buffer_frames * output->sample_rate_hz / LND_MAX(clock->cfg.sample_rate_hz, 1u);
    uint32_t ring = (uint32_t)LND_MAX((uint64_t)lnd_cfg_u32(LND_CFG_GRAPH_BUFFER_FRAMES) * lnd_cfg_u32(LND_CFG_GRAPH_BUFFER_COUNT), scaled * 4);
    s->output = output;
    s->clock = clock;
    s->ring = lnd_capture_new(output->channels, output->sample_rate_hz, ring);
    s->stage = lnd_alloc_aligned((size_t)block * output->channels * sizeof(float), LND_CACHE_LINE);
    s->chunk = lnd_alloc_aligned((size_t)block * output->channels * sizeof(float), LND_CACHE_LINE);
    if (!s->ring || !s->stage || !s->chunk || lnd_thread_create(&s->thread, lnd_sink_thread, s) != LND_OK) {
        s->clock = nullptr;
        lnd_node_destroy(&s->base);
        return nullptr;
    }
    s->started = true;
    lnd_sink_attach(s);
    return &s->base;
}

lnd_output *lnd_sink_output(const lnd_node *n) { return ((const lnd_sink_node *)n)->output; }

static void lnd_sink_pump(lnd_sink_node *s, uint64_t frames, uint32_t clock_rate) {
    lnd_node *n = &s->base;
    s->acc += frames * n->sample_rate_hz;
    uint64_t want = s->acc / clock_rate;
    s->acc %= clock_rate;
    while (want) {
        uint32_t nb = (uint32_t)LND_MIN(want, (uint64_t)n->block);
        lnd_node_pull(n, nullptr, s->stage, nb);
        lnd_capture_push(s->ring, s->stage, LND_FORMAT_F32, nb);
        want -= nb;
    }
}

void lnd_sinks_pump(lnd_instance *i, uint64_t frames) {
    lnd_spinlock_lock(&i->sinks_lock);
    for (lnd_sink_node *s = i->sinks; s; s = s->next_sink) lnd_sink_pump(s, frames, i->cfg.sample_rate_hz);
    lnd_spinlock_unlock(&i->sinks_lock);
}

void lnd_sinks_detach_instance(lnd_instance *i) {
    lnd_spinlock_lock(&i->sinks_lock);
    lnd_sink_node *s = i->sinks;
    i->sinks = nullptr;
    lnd_spinlock_unlock(&i->sinks_lock);
    for (; s; s = s->next_sink) s->clock = nullptr;
}

void lnd_sinks_release_output(lnd_output *o) {
    for (lnd_node *n = lnd_graph_ctx.nodes; n; n = n->next) {
        if (n->vt == &lnd_sink_vt && ((lnd_sink_node *)n)->output == o) lnd_sink_stop((lnd_sink_node *)n);
    }
}

LND_NODE *LND_NodeCreateSink(LND_OUTPUT *output, LND_DEVICE_INSTANCE *instance) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!output) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_STATE);
    }
    if (instance && (!lnd_context_has_instance(instance) || !instance->master)) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
    if (!instance) instance = lnd_context_default_output();
    if (!instance) {
        lnd_context_unlock();
        return nullptr;
    }
    lnd_node *n = lnd_node_create_sink(output, instance);
    lnd_context_unlock();
    return n ? n : lnd_error_null(LND_ERR_OUT_OF_MEMORY);
}

LND_OUTPUT *LND_NodeGetSinkOutput(const LND_NODE *sink) { return sink && sink->vt == &lnd_sink_vt ? lnd_sink_output(sink) : nullptr; }
