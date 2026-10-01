#include "context.h"
#include "node.h"
#include "walk.h"
#include "native.h"
#include "playback/slide/slide.h"
#include "src/pcm.h"
#include "render.h"
#include "src/native.h"
#if LND_MODULE_QUEUE
#include "lindar_queue.h"
#endif
#include "src/alloc.h"
#include "src/config.h"
#include "src/error.h"
#include "src/thread.h"
#if LND_MODULE_DEVICES
#include "io/devices/engine.h"
#include "io/devices/context.h"
#endif
#include "pcm/audio/channels.h"
#include "pcm/audio/convert.h"
#include "src/format.h"
#include "pcm/audio/simd.h"

#include <math.h>
#include <string.h>

typedef struct lnd_edge_source {
    lnd_source base;
    lnd_edge *edge;
} lnd_edge_source;

static uint64_t lnd_edge_source_read(lnd_source *s, float *dst, uint64_t frames) {
    lnd_edge *e = ((lnd_edge_source *)s)->edge;
    uint32_t got = lnd_node_pull(e->src, &e->cursor, dst, (uint32_t)frames);
    lnd_store_relaxed(&s->pos, lnd_load_relaxed(&s->pos) + got);
    lnd_store(&s->status, lnd_node_status(e->src, &e->cursor));
    return got;
}

static int64_t lnd_edge_source_read_pcm(lnd_source *s, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_edge *e = ((lnd_edge_source *)s)->edge;
    uint32_t got = lnd_node_pull_pcm(e->src, &e->cursor, pcm, offset, (uint32_t)frames);
    lnd_store_relaxed(&s->pos, lnd_load_relaxed(&s->pos) + got);
    int32_t status = lnd_node_status(e->src, &e->cursor);
    lnd_store(&s->status, status);
    return !got && status < 0 ? status : (int64_t)got;
}

static void lnd_edge_source_free(lnd_source *s) { lnd_free(s); }

static const lnd_source_vt lnd_edge_source_vt = {
    .read = lnd_edge_source_read,
    .read_pcm = lnd_edge_source_read_pcm,
    .free = lnd_edge_source_free,
};

const float *lnd_node_matrix(lnd_node *n, uint32_t sc) {
    if (sc == 0 || sc > LND_MAX_CHANNELS) return nullptr;
    float *existing = lnd_load(&n->matrices[sc]);
    if (existing) return existing;
    float *matrix = lnd_alloc((size_t)sc * n->channels * sizeof(float));
    if (!matrix) return nullptr;
    lnd_channels_matrix(sc, n->channels, n->channel_mix, matrix);
    lnd_store(&n->matrices[sc], matrix);
    return matrix;
}

bool lnd_node_reaches(lnd_node *from, lnd_node *target) {
    lnd_walk walk = lnd_walk_begin(from, false, true);
    for (lnd_node *n = lnd_walk_next(&walk); n; n = lnd_walk_next(&walk))
        if (n == target) return true;
    return false;
}

void lnd_node_activate(lnd_node *node) {
    lnd_walk walk = lnd_walk_begin(node, false, false);
    for (lnd_node *n = lnd_walk_next(&walk); n; n = lnd_walk_next(&walk)) {
        lnd_store(&n->status, LND_SOURCE_READY);
        lnd_store(&n->active, 1);
    }
}

typedef struct lnd_edge_plan {
    lnd_edge *edge;
    lnd_source *resampler;
    float *mono;
    float *matrix;
} lnd_edge_plan;

static void lnd_edge_plan_free(lnd_edge_plan *p) {
    lnd_source_free(p->resampler);
    lnd_free(p->mono);
    lnd_free(p->matrix);
}

static int32_t lnd_edge_prepare(lnd_edge_plan *p, lnd_edge *e, uint32_t sc, uint32_t sr, uint32_t dc, uint32_t dr) {
    p->edge = e;
    if (sr != dr) {
        lnd_edge_source *adapter = lnd_alloc_zero(sizeof *adapter);
        if (!adapter) return LND_ERR_OUT_OF_MEMORY;
        adapter->base.vt = &lnd_edge_source_vt;
        adapter->base.live = true;
        adapter->base.channels = sc;
        adapter->base.sample_rate_hz = sr;
        adapter->edge = e;
        p->resampler = lnd_resample_source_create(&adapter->base, true, dr, e->dst->block, lnd_cfg_u32(LND_CFG_AUDIO_RESAMPLE_QUALITY));
        if (!p->resampler) {
            lnd_free(adapter);
            return LND_ERR_OUT_OF_MEMORY;
        }
    }
    if (e->dst->type == LND_NODE_CHANNEL_MERGER) {
        if (sc == 1) return LND_OK;
        p->mono = lnd_alloc((size_t)sc * sizeof(float));
        if (!p->mono) return LND_ERR_OUT_OF_MEMORY;
        lnd_channels_matrix(sc, 1, e->dst->channel_mix, p->mono);
    } else {
        bool custom = e->matrix && sc == e->src->channels && dc == e->dst->channels;
        if (sc == dc && !custom) return LND_OK;
        p->matrix = lnd_alloc((size_t)sc * dc * sizeof(float));
        if (!p->matrix) return LND_ERR_OUT_OF_MEMORY;
        if (custom)
            memcpy(p->matrix, e->matrix, (size_t)sc * dc * sizeof(float));
        else
            lnd_channels_matrix(sc, dc, e->dst->channel_mix, p->matrix);
    }
    return LND_OK;
}

static void lnd_edge_commit(lnd_edge_plan *p) {
    lnd_edge *e = p->edge;
    lnd_source *old_resampler = e->resampler;
    float *old_mono = e->mono, *old_matrix = e->matrix;
    e->resampler = p->resampler;
    e->mono = p->mono;
    e->matrix = p->matrix;
    p->resampler = old_resampler;
    p->mono = old_mono;
    p->matrix = old_matrix;
    if (e->dst->type == LND_NODE_CHANNEL_MERGER && e->channel >= e->dst->channels) e->channel = e->dst->channels - 1;
}

int32_t lnd_node_connect(lnd_node *src, lnd_node *dst) {
    if (src == dst || lnd_node_reaches(dst, src)) return LND_ERR_CYCLE;
    for (lnd_edge *e = src->outputs; e; e = e->next_out)
        if (e->dst == dst) return LND_OK;
    lnd_edge *e = lnd_alloc_zero(sizeof *e);
    if (!e) return LND_ERR_OUT_OF_MEMORY;
    e->src = src;
    e->dst = dst;
    e->channel = dst->inputs_count % dst->channels;
    lnd_edge_plan plan = {0};
    int32_t result = lnd_edge_prepare(&plan, e, src->channels, src->sample_rate_hz, dst->channels, dst->sample_rate_hz);
    if (result == LND_OK) result = lnd_node_reserve_input(dst, src->channels, src->channels != dst->channels || dst->type == LND_NODE_CHANNEL_MERGER);
    if (result == LND_OK && src->outputs_count + 1 + (src->instance != nullptr) + src->extra_consumers >= 2) result = lnd_node_ensure_ring(src);
    if (result != LND_OK) {
        lnd_edge_plan_free(&plan);
        lnd_free(e);
        return result;
    }
    lnd_spinlock_lock(&dst->lock);
    lnd_spinlock_lock(&src->lock);
    lnd_edge_commit(&plan);
    e->cursor = src->produced;
    e->next_in = dst->inputs;
    dst->inputs = e;
    dst->inputs_count++;
    e->next_out = src->outputs;
    src->outputs = e;
    src->outputs_count++;
    lnd_spinlock_unlock(&src->lock);
    lnd_spinlock_unlock(&dst->lock);
    lnd_edge_plan_free(&plan);
    if (lnd_load(&src->active)) lnd_node_activate(dst);
    return LND_OK;
}

static void lnd_edge_remove(lnd_edge *e) {
    lnd_node *src = e->src;
    lnd_node *dst = e->dst;
    lnd_spinlock_lock(&dst->lock);
    lnd_spinlock_lock(&src->lock);
    if (dst->clock == e) dst->clock = nullptr;
    for (lnd_edge **p = &dst->inputs; *p; p = &(*p)->next_in) {
        if (*p == e) {
            *p = e->next_in;
            dst->inputs_count--;
            break;
        }
    }
    for (lnd_edge **p = &src->outputs; *p; p = &(*p)->next_out) {
        if (*p == e) {
            *p = e->next_out;
            src->outputs_count--;
            break;
        }
    }
    lnd_spinlock_unlock(&src->lock);
    lnd_spinlock_unlock(&dst->lock);
    lnd_source_free(e->resampler);
    lnd_free(e->mono);
    lnd_free(e->matrix);
    lnd_free(e);
}

int32_t lnd_node_disconnect(lnd_node *src, lnd_node *dst) {
    if (!dst) {
        while (src->outputs)
            lnd_edge_remove(src->outputs);
        return LND_OK;
    }
    for (lnd_edge *e = src->outputs; e; e = e->next_out) {
        if (e->dst == dst) {
            lnd_edge_remove(e);
            return LND_OK;
        }
    }
    return LND_ERR_INVALID_ARG;
}

void lnd_node_disconnect_all(lnd_node *n) {
    while (n->inputs)
        lnd_edge_remove(n->inputs);
    while (n->outputs)
        lnd_edge_remove(n->outputs);
}

int32_t lnd_node_set_output(lnd_node *src, lnd_node *dst) {
    int32_t result = dst ? lnd_node_connect(src, dst) : LND_OK;
    if (result != LND_OK) return result;
    for (lnd_edge *e = src->outputs, *next; e; e = next) {
        next = e->next_out;
        if (e->dst != dst) lnd_edge_remove(e);
    }
    return LND_OK;
}

static void lnd_node_buffers_free(lnd_node *n) {
    lnd_free_aligned(n->scratch);
    lnd_free_aligned(n->scratch_map);
    if (!n->native) lnd_free_aligned(n->out);
    lnd_node_native_free(n);
}

int32_t lnd_node_reconfigure(lnd_node *n, uint32_t channels, uint32_t sample_rate_hz) {
    if (!channels || channels > LND_MAX_CHANNELS || !sample_rate_hz) return LND_ERR_INVALID_ARG;
    if (n->channels == channels && n->sample_rate_hz == sample_rate_hz) return LND_OK;
    lnd_node fresh = {.vt = n->vt, .type = n->type, .channels = channels, .input_channels = channels,
                      .sample_rate_hz = sample_rate_hz, .block = n->block, .out_cap = n->out_cap};
    int32_t result = n->native ? lnd_node_native_init(&fresh) : LND_OK;
    if (result == LND_OK && n->native && n->out) result = lnd_node_native_ring(&fresh);
    if (result == LND_OK) {
        if (n->scratch) fresh.scratch = lnd_alloc_aligned((size_t)n->block * channels * sizeof(float), LND_CACHE_LINE);
        if (n->scratch_map) fresh.scratch_map = lnd_alloc_aligned((size_t)n->block * channels * sizeof(float), LND_CACHE_LINE);
        if (n->out && !n->native) {
            fresh.out = lnd_alloc_aligned(((size_t)n->out_cap + n->block) * channels * sizeof(float), LND_CACHE_LINE);
            if (fresh.out) memset(fresh.out, 0, (size_t)n->out_cap * channels * sizeof(float));
        }
        if ((n->scratch && !fresh.scratch) || (n->scratch_map && !fresh.scratch_map) || (n->out && !fresh.out)) result = LND_ERR_OUT_OF_MEMORY;
    }
    size_t count = (size_t)n->inputs_count + n->outputs_count, prepared = 0;
    lnd_edge_plan *plans = nullptr;
    if (result == LND_OK && count) {
        plans = count <= SIZE_MAX / sizeof *plans ? lnd_alloc_zero(count * sizeof *plans) : nullptr;
        if (!plans) result = LND_ERR_OUT_OF_MEMORY;
    }
    for (lnd_edge *e = n->inputs; result == LND_OK && e; e = e->next_in) {
        result = lnd_node_reserve_input(&fresh, e->src->channels, e->matrix || e->src->channels != channels || n->type == LND_NODE_CHANNEL_MERGER);
        if (result == LND_OK) result = lnd_edge_prepare(&plans[prepared++], e, e->src->channels, e->src->sample_rate_hz, channels, sample_rate_hz);
    }
    for (lnd_edge *e = n->outputs; result == LND_OK && e; e = e->next_out) {
        result = lnd_node_reserve_input(e->dst, channels, e->matrix || channels != e->dst->channels || e->dst->type == LND_NODE_CHANNEL_MERGER);
        if (result == LND_OK) result = lnd_edge_prepare(&plans[prepared++], e, channels, sample_rate_hz, e->dst->channels, e->dst->sample_rate_hz);
    }
    if (result == LND_OK) {
        lnd_renderers_detach_node(n);
        lnd_walk walk = lnd_walk_begin(n, false, true);
        lnd_node *locked = nullptr;
        for (lnd_node *p = lnd_walk_post(&walk); p; p = lnd_walk_post(&walk)) {
            lnd_spinlock_lock(&p->lock);
            p->transaction_next = locked;
            locked = p;
        }
        lnd_node old = {.native = n->native, .out = n->out, .scratch = n->scratch, .scratch_map = n->scratch_map};
        n->native = fresh.native;
        n->out = fresh.out;
        n->scratch = fresh.scratch;
        n->scratch_map = fresh.scratch_map;
        n->input_channels = fresh.input_channels;
        fresh.native = old.native;
        fresh.out = old.out;
        fresh.scratch = old.scratch;
        fresh.scratch_map = old.scratch_map;
        n->channels = channels;
        n->sample_rate_hz = sample_rate_hz;
        n->revision++;
        float *matrices[LND_MAX_CHANNELS + 1];
        for (uint32_t i = 0; i <= LND_MAX_CHANNELS; i++) {
            matrices[i] = lnd_load_relaxed(&n->matrices[i]);
            lnd_store_relaxed(&n->matrices[i], nullptr);
        }
        for (size_t i = 0; i < prepared; i++) {
            lnd_edge_commit(&plans[i]);
            if (plans[i].edge->src == n) plans[i].edge->cursor = n->produced;
        }
        while (locked) {
            lnd_node *next = locked->transaction_next;
            locked->transaction_next = nullptr;
            lnd_spinlock_unlock(&locked->lock);
            locked = next;
        }
        for (uint32_t i = 0; i <= LND_MAX_CHANNELS; i++)
            lnd_free(matrices[i]);
    }
    for (size_t i = 0; i < prepared; i++)
        lnd_edge_plan_free(&plans[i]);
    lnd_free(plans);
    lnd_node_buffers_free(&fresh);
    return result;
}
