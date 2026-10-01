#include "render.h"
#include "src/alloc.h"
#include "src/context.h"
#include "src/error.h"
#include "src/pcm.h"
#include "src/render.h"

#if LND_THREADS
thread_local uint32_t lnd_render_depth;
thread_local uint64_t lnd_render_timestamp;
#endif

struct lnd_graph_renderer {
    lnd_node *node;
    LND_RENDERER *renderer;
    float *scratch;
    uint64_t cursor;
    struct lnd_graph_renderer *next;
};

typedef struct lnd_graph_renderer lnd_graph_renderer;

static lnd_graph_renderer *lnd_graph_renderers;

static void lnd_renderer_detach(lnd_graph_renderer *r) {
    if (!r->node) return;
    lnd_spinlock_lock(&r->node->lock);
    r->node->extra_consumers--;
    lnd_spinlock_unlock(&r->node->lock);
    r->node = nullptr;
}

void lnd_renderers_detach_node(lnd_node *node) {
    for (lnd_graph_renderer *r = lnd_graph_renderers; r; r = r->next) {
        lnd_spinlock_lock(&r->renderer->lock);
        if (r->node == node) lnd_renderer_detach(r);
        lnd_spinlock_unlock(&r->renderer->lock);
    }
}

static void lnd_graph_renderer_close(void *user) {
    lnd_graph_renderer *r = user;
    for (lnd_graph_renderer **p = &lnd_graph_renderers; *p; p = &(*p)->next) {
        if (*p != r) continue;
        *p = r->next;
        break;
    }
    lnd_renderer_detach(r);
    lnd_free_aligned(r->scratch);
    lnd_free(r);
}

static int64_t lnd_graph_renderer_read(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_graph_renderer *r = user;
    if (!r->node) return LND_ERR_STATE;
    lnd_node *node = r->node;
    if (node->native) {
        uint32_t got = lnd_node_pull_pcm_valid(node, &r->cursor, pcm, offset, (uint32_t)frames);
        int32_t status = lnd_node_status(node, &r->cursor);
        return !got && status < 0 ? status : (int64_t)got;
    }
    uint32_t planar_got;
    if (pcm->layout == LND_LAYOUT_PLANAR && node->vt->render_planar && lnd_node_pull_planar(node, &r->cursor, pcm, offset, (uint32_t)frames, &planar_got)) {
        int32_t status = lnd_node_status(node, &r->cursor);
        return !planar_got && status < 0 ? status : (int64_t)planar_got;
    }
    bool direct = pcm->layout == LND_LAYOUT_INTERLEAVED && pcm->format == LND_FORMAT_F32 && lnd_pcm_stride(pcm) == node->channels * sizeof(float) &&
                  (uintptr_t)lnd_pcm_at(pcm, 0, offset) % alignof(float) == 0;
    float *dst = direct ? (float *)lnd_pcm_at(pcm, 0, offset) : r->scratch;
    uint32_t got = lnd_node_pull(node, &r->cursor, dst, (uint32_t)frames);
    if (!direct) {
        LND_PCM stage = {.data = r->scratch, .frames = frames, .channels = node->channels, .format = LND_FORMAT_F32};
        int32_t result = lnd_pcm_convert(pcm, offset, &stage, 0, frames);
        if (result != LND_OK) return result;
    }
    int32_t status = lnd_node_status(node, &r->cursor);
    return !got && status < 0 ? status : (int64_t)got;
}

LND_RENDERER *LND_RendererCreateNode(LND_NODE *node) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_ctx.initialized || !node || !lnd_context_has_node(node)) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
    lnd_graph_renderer *r = lnd_alloc_zero(sizeof *r);
    if (!r) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    if (!node->native) r->scratch = lnd_alloc_aligned((size_t)node->block * node->channels * sizeof(float), LND_CACHE_LINE);
    int32_t result = node->native || r->scratch ? LND_OK : LND_ERR_OUT_OF_MEMORY;
    if (result == LND_OK && node->outputs_count + node->extra_consumers + (node->instance != nullptr)) result = lnd_node_ensure_ring(node);
    if (result == LND_OK) {
        LND_RENDERER_CONFIG config = {.render = lnd_graph_renderer_read,
                                    .close = lnd_graph_renderer_close,
                                    .user = r,
                                    .channels = node->channels,
                                    .sample_rate_hz = node->sample_rate_hz,
                                    .block_frames = node->block};
        r->renderer = lnd_renderer_create(&config);
        if (!r->renderer) result = LND_ErrorGetLast();
    }
    if (result != LND_OK) {
        lnd_free_aligned(r->scratch);
        lnd_free(r);
        lnd_context_unlock();
        return lnd_error_null(result);
    }
    lnd_spinlock_lock(&node->lock);
    node->extra_consumers++;
    r->cursor = node->produced;
    r->node = node;
    lnd_spinlock_unlock(&node->lock);
    r->next = lnd_graph_renderers;
    lnd_graph_renderers = r;
    lnd_context_unlock();
    return r->renderer;
}
