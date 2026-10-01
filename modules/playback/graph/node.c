#include "lnd_modules.h"
#if LND_MODULE_MONITOR
#include "processing/monitor/monitor.h"
#endif
#include "context.h"
#include "src/notify.h"
#include "node.h"
#include "sound.h"
#include "split.h"
#include "walk.h"
#include "native.h"
#include "gain.h"
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

static void lnd_node_pending_remove(lnd_node *n) {
    if (!n->pending) return;
    if (n->pending_prev)
        n->pending_prev->pending_next = n->pending_next;
    else
        lnd_graph_ctx.pending_head = n->pending_next;
    if (n->pending_next)
        n->pending_next->pending_prev = n->pending_prev;
    else
        lnd_graph_ctx.pending_tail = n->pending_prev;
    n->pending = false;
    n->pending_prev = n->pending_next = nullptr;
    lnd_graph_ctx.pending_count--;
}

static void lnd_node_pending_add(lnd_node *n) {
    if (n->pending) return;
    n->pending = true;
    n->pending_prev = lnd_graph_ctx.pending_tail;
    if (n->pending_prev)
        n->pending_prev->pending_next = n;
    else
        lnd_graph_ctx.pending_head = n;
    lnd_graph_ctx.pending_tail = n;
    lnd_graph_ctx.pending_count++;
}

static void lnd_node_link(lnd_node *n) {
    n->prev = nullptr;
    n->next = lnd_graph_ctx.nodes;
    if (n->next) n->next->prev = n;
    lnd_graph_ctx.nodes = n;
}

static void lnd_node_unlink(lnd_node *n) {
    lnd_node_pending_remove(n);
    if (n->prev)
        n->prev->next = n->next;
    else
        lnd_graph_ctx.nodes = n->next;
    if (n->next) n->next->prev = n->prev;
    n->prev = n->next = nullptr;
}

lnd_node *lnd_node_alloc(size_t size, const lnd_node_vt *vt, int32_t type, uint32_t channels, uint32_t sample_rate_hz) {
    lnd_node *n = lnd_alloc_aligned(size, alignof(lnd_node));
    if (!n) return nullptr;
    memset(n, 0, size);
    n->vt = vt;
    n->type = type;
    n->channels = channels;
    n->input_channels = channels;
    n->sample_rate_hz = sample_rate_hz;
    n->block = lnd_cfg_u32(LND_CFG_GRAPH_MIX_BLOCK_FRAMES);
    n->ramp = lnd_cfg_u32(LND_CFG_GRAPH_GAIN_RAMP_FRAMES);
    n->clip = LND_CLIP_NONE;
    n->channel_mix = (int32_t)lnd_cfg_u32(LND_CFG_AUDIO_CHANNEL_MIX);
    n->out_cap = lnd_next_pow2_u32(lnd_cfg_u32(LND_CFG_GRAPH_MIXER_BUFFER_FRAMES));
    n->gain_param = n->gain = n->gain_target = 1.0f;
    lnd_store_relaxed(&n->active, type == LND_NODE_SOURCE ? 0 : 1);
    lnd_store_relaxed(&n->last_pull, lnd_time_ns());
    bool ok = lnd_ring_init(&n->ring, lnd_cfg_u32(LND_CFG_GRAPH_COMMAND_QUEUE_CAPACITY)) == LND_OK;
    if (ok) ok = lnd_node_native_init(n) == LND_OK;
    if (ok && (!n->native || !vt->render_pcm) && type != LND_NODE_SOURCE && type != LND_NODE_PCM_INPUT) {
        n->scratch = lnd_alloc_aligned((size_t)n->block * channels * sizeof(float), LND_CACHE_LINE);
        ok = n->scratch != nullptr;
    }
    if (!ok) {
        lnd_node_native_free(n);
        lnd_ring_free(&n->ring);
        lnd_free_aligned(n->scratch);
        lnd_free_aligned(n->scratch_map);
        lnd_free_aligned(n);
        return nullptr;
    }
    lnd_node_link(n);
    return n;
}

static void lnd_node_command(lnd_node *n, const lnd_cmd *c, bool immediate) {
    uint32_t ramp = immediate ? 0 : n->ramp;
#if LND_MODULE_SLIDE
    if (c->op == LND_OP_GAIN)
        lnd_slide_cancel(n, LND_PARAM_GAIN);
    else if (c->op == LND_OP_PARAM)
        lnd_slide_cancel(n, LND_PARAM_USER + (int32_t)c->u32);
#endif
    switch (c->op) {
    case LND_OP_GAIN:
        n->gain_target = c->f32;
        if (ramp == 0) {
            n->gain = c->f32;
            n->gain_ramp = 0;
        } else {
            n->gain_ramp = ramp;
            n->gain_step = (c->f32 - n->gain) / (float)ramp;
        }
        break;
    case LND_OP_CLIP:
        n->clip = (int32_t)c->u32;
        break;
    default:
        if (n->vt->command) n->vt->command(n, c, immediate);
        break;
    }
}

void lnd_node_process(lnd_node *n, bool immediate) {
    lnd_cmd c;
    while (lnd_ring_pop(&n->ring, &c))
        lnd_node_command(n, &c, immediate);
    if (!immediate) return;
    n->gain = n->gain_target;
    n->gain_ramp = 0;
    if (n->vt->finish) n->vt->finish(n);
}

static void lnd_node_render_plain(lnd_node *n, float *dst, uint32_t frames) {
    n->rendered = frames;
#if LND_MODULE_MONITOR
    lnd_monitor_scope scope;
    lnd_monitor_begin(n, &scope);
#endif
    n->vt->render(n, dst, frames);
#if LND_MODULE_MONITOR
    lnd_monitor_end(n, &scope, frames);
#endif
    uint32_t ch = n->channels;
    size_t total = (size_t)frames * ch;
    LND_PCM pcm = {.data = dst, .frames = frames, .channels = ch, .format = LND_FORMAT_F32};
    bool automated = false;
#if LND_MODULE_SLIDE
    if (n->slides) automated = lnd_slide_gain(n, &pcm, 0, frames);
#endif
    if (!automated) {
        if (n->gain_ramp)
            lnd_graph_pcm_ramp_active(&pcm, 0, frames, &n->gain, n->gain_step, n->gain_target, &n->gain_ramp);
        else if (n->gain != 1.0f)
            lnd_simd.scale(dst, n->gain, total);
    }
    lnd_notify_node(n, n->rendered);
    if (n->clip == LND_CLIP_HARD)
        lnd_simd.clip_hard(dst, total);
    else if (n->clip == LND_CLIP_SOFT)
        lnd_simd.clip_soft(dst, total);
}

static void lnd_node_render(lnd_node *n, float *dst, uint32_t frames) {
#if LND_MODULE_SLIDE
    if (n->slides) {
        uint32_t done = 0;
        while (done < frames) {
            uint32_t count = lnd_slide_before(n, frames - done);
            lnd_node_render_plain(n, dst + (size_t)done * n->channels, count);
            lnd_slide_after(n, count);
            done += n->rendered;
            if (n->rendered < count) break;
        }
        n->rendered = done;
        return;
    }
#endif
    lnd_node_render_plain(n, dst, frames);
}

static uint32_t lnd_node_render_ring(lnd_node *n, uint64_t at, uint32_t frames) {
    uint32_t ch = n->channels, idx = (uint32_t)(at & (n->out_cap - 1));
    float *stage = n->out + (size_t)n->out_cap * ch;
    lnd_node_render(n, stage, frames);
    uint32_t got = n->rendered, first = LND_MIN(got, n->out_cap - idx);
    memcpy(n->out + (size_t)idx * ch, stage, (size_t)first * ch * sizeof(float));
    if (got > first) memcpy(n->out, stage + (size_t)first * ch, (size_t)(got - first) * ch * sizeof(float));
    return got;
}

static void lnd_node_copy_ring(const lnd_node *n, uint64_t from, float *dst, uint32_t frames) {
    uint32_t ch = n->channels;
    uint32_t idx = (uint32_t)(from & (n->out_cap - 1));
    uint32_t first = LND_MIN(frames, n->out_cap - idx);
    memcpy(dst, n->out + (size_t)idx * ch, (size_t)first * ch * sizeof(float));
    if (frames > first) memcpy(dst + (size_t)first * ch, n->out, (size_t)(frames - first) * ch * sizeof(float));
}

uint32_t lnd_node_pull(lnd_node *n, uint64_t *cursor, float *dst, uint32_t frames) {
    if (n->native) {
        LND_PCM pcm = {.data = dst, .frames = frames, .channels = n->channels, .format = LND_FORMAT_F32};
        return lnd_node_pull_pcm(n, cursor, &pcm, 0, frames);
    }
    uint32_t ch = n->channels, done = 0;
    lnd_spinlock_lock(&n->lock);
    lnd_store_relaxed(&n->last_pull, lnd_render_begin());
    lnd_node_process(n, false);
    uint64_t at = cursor ? *cursor : n->produced;
    while (done < frames) {
        uint32_t count = LND_MIN(frames - done, n->block), got;
        if (!n->out) {
            lnd_node_render(n, dst + (size_t)done * ch, count);
            got = n->rendered;
            n->produced += got;
            at = n->produced;
        } else {
            count = LND_MIN(count, n->out_cap / 2);
            if (n->produced - at > n->out_cap) at = n->produced - n->out_cap;
            if (at + count > n->produced) n->produced += lnd_node_render_ring(n, n->produced, (uint32_t)(at + count - n->produced));
            got = (uint32_t)LND_MIN(count, n->produced - at);
            lnd_node_copy_ring(n, at, dst + (size_t)done * ch, got);
            at += got;
        }
        done += got;
        if (got < count) break;
    }
    if (done < frames) memset(dst + (size_t)done * ch, 0, (size_t)(frames - done) * ch * sizeof(float));
    if (cursor) *cursor = at;
    lnd_render_end();
    lnd_spinlock_unlock(&n->lock);
    return done;
}

bool lnd_node_pull_planar(lnd_node *n, uint64_t *cursor, const LND_PCM *pcm, size_t offset, uint32_t frames, uint32_t *got) {
    if (!n->vt->render_planar || n->native || pcm->format != LND_FORMAT_F32 || pcm->layout != LND_LAYOUT_PLANAR ||
        lnd_pcm_stride(pcm) != sizeof(float) || frames > n->block) return false;
    for (uint32_t c = 0; c < pcm->channels; c++)
        if ((uintptr_t)lnd_pcm_at(pcm, c, offset) % alignof(float)) return false;
    lnd_spinlock_lock(&n->lock);
    lnd_node_process(n, false);
    lnd_edge *e = n->inputs;
    bool ready = !n->out && !n->gain_ramp && e && !e->next_in && !e->paused && !e->matrix && !e->resampler && e->src->channels == n->channels &&
                 e->src->native && e->src->native->stage.pcm.format == LND_FORMAT_F32 && e->src->native->stage.pcm.layout == LND_LAYOUT_PLANAR;
#if LND_MODULE_SLIDE
    ready &= n->slides == nullptr;
#endif
    if (!ready) {
        lnd_spinlock_unlock(&n->lock);
        return false;
    }
    lnd_store_relaxed(&n->last_pull, lnd_render_begin());
    n->rendered = frames;
#if LND_MODULE_MONITOR
    lnd_monitor_scope scope;
    lnd_monitor_begin(n, &scope);
#endif
    n->vt->render_planar(n, pcm, offset, frames);
#if LND_MODULE_MONITOR
    lnd_monitor_end(n, &scope, frames);
#endif
    if (n->gain != 1.0f)
        for (uint32_t c = 0; c < pcm->channels; c++) lnd_simd.scale((float *)lnd_pcm_at(pcm, c, offset), n->gain, frames);
    lnd_notify_node(n, n->rendered);
    int32_t clip = lnd_load_relaxed(&n->clip);
    if (clip != LND_CLIP_NONE)
        for (uint32_t c = 0; c < pcm->channels; c++) {
            float *data = (float *)lnd_pcm_at(pcm, c, offset);
            if (clip == LND_CLIP_HARD) lnd_simd.clip_hard(data, frames);
            else if (clip == LND_CLIP_SOFT) lnd_simd.clip_soft(data, frames);
        }
    *got = n->rendered;
    n->produced += *got;
    if (cursor) *cursor = n->produced;
    if (*got < frames) lnd_pcm_silence(pcm, offset + *got, frames - *got);
    lnd_render_end();
    lnd_spinlock_unlock(&n->lock);
    return true;
}

int32_t lnd_node_post(lnd_node *n, uint32_t op, uint64_t u64, uint32_t u32, float f32) {
    lnd_cmd c = {.op = op, .u32 = u32, .u64 = u64, .ptr = nullptr, .f32 = f32};
    int32_t result = lnd_ring_push(&n->ring, &c);
    if (result != LND_OK) return result;
    lnd_node_pending_add(n);
    if (op == LND_OP_GAIN) lnd_store(&n->gain_param, f32);
    return LND_OK;
}

bool lnd_node_drain(lnd_node *n, bool force) {
    uint64_t timeout = (uint64_t)lnd_cfg_u32(LND_CFG_GRAPH_DRAIN_TIMEOUT_MS) * 1000000ull;
    if (!force && lnd_time_ns() - lnd_load_relaxed(&n->last_pull) < timeout) return false;
    lnd_spinlock_lock(&n->lock);
    bool drained = force || lnd_time_ns() - lnd_load_relaxed(&n->last_pull) >= timeout;
    if (drained) lnd_node_process(n, true);
    lnd_spinlock_unlock(&n->lock);
    return drained;
}

int32_t lnd_node_ensure_ring(lnd_node *n) {
    if (n->out) return LND_OK;
    if (n->native) return lnd_node_native_ring(n);
    float *out = lnd_alloc_aligned(((size_t)n->out_cap + n->block) * n->channels * sizeof(float), LND_CACHE_LINE);
    if (!out) return LND_ERR_OUT_OF_MEMORY;
    memset(out, 0, (size_t)n->out_cap * n->channels * sizeof(float));
    lnd_spinlock_lock(&n->lock);
    n->out = out;
    lnd_spinlock_unlock(&n->lock);
    return LND_OK;
}

void lnd_node_destroy(lnd_node *n) {
    lnd_renderers_detach_node(n);
    lnd_node_disconnect_all(n);
    lnd_node_drain(n, true);
    lnd_notify_detach(&n->notifications);
    if (n->vt->destroy) n->vt->destroy(n);
#if LND_MODULE_SLIDE
    lnd_slide_free(n);
#endif
    lnd_node_unlink(n);
    for (uint32_t i = 0; i <= LND_MAX_CHANNELS; i++)
        lnd_free(lnd_load_relaxed(&n->matrices[i]));
    lnd_ring_free(&n->ring);
    lnd_free_aligned(n->scratch);
    lnd_free_aligned(n->scratch_map);
    if (!n->native) lnd_free_aligned(n->out);
    lnd_node_native_free(n);
    lnd_sound_free(n->sound);
#if LND_MODULE_METADATA
    if (n->view) LND_MetadataFree(n->view->base.metadata);
#endif
    lnd_free(n->view);
    lnd_free_aligned(n);
}

static uint64_t lnd_sat_add(uint64_t a, uint64_t b) { return a == UINT64_MAX || b == UINT64_MAX || a + b < a ? UINT64_MAX : a + b; }

int32_t lnd_node_status(lnd_node *n, const uint64_t *cursor) {
    if (!n) return LND_SOURCE_EOF;
    if (cursor && lnd_load(&n->produced) > *cursor) return LND_SOURCE_READY;
    if (n->type == LND_NODE_SOURCE) {
        int32_t status = lnd_source_status(lnd_source_node_origin(n));
        return status == LND_SOURCE_READY && !lnd_load(&n->active) ? LND_SOURCE_WAITING : status;
    }
    LND_SOUND *sound = lnd_node_pcm_sound(n);
    if (sound) {
        lnd_native_sound *s = (lnd_native_sound *)sound;
        int32_t status = lnd_load(&s->source->status);
        return status == LND_SOURCE_READY && lnd_load(&s->state) != LND_SOUND_PLAYING ? LND_SOURCE_WAITING : status;
    }
    return lnd_load(&n->status);
}

static uint64_t lnd_edge_available(lnd_edge *e) {
    if (e->resampler) return UINT64_MAX;
    return lnd_node_available(e->src, &e->cursor);
}

uint64_t lnd_node_inputs_available(lnd_node *n, bool max) {
    uint64_t result = max ? 0 : UINT64_MAX;
    bool any = false;
    for (lnd_edge *e = n->inputs; e; e = e->next_in) {
        if (e->paused || lnd_node_status(e->src, &e->cursor) == LND_SOURCE_EOF) continue;
        uint64_t a = lnd_edge_available(e);
        result = max ? LND_MAX(result, a) : LND_MIN(result, a);
        any = true;
    }
    return any ? result : 0;
}

uint64_t lnd_node_available(lnd_node *n, const uint64_t *cursor) {
    lnd_spinlock_lock(&n->lock);
    lnd_node_process(n, false);
    uint64_t produced = lnd_load(&n->produced);
    uint64_t ahead = cursor && n->out && produced > *cursor ? LND_MIN(produced - *cursor, n->out_cap) : 0;
    uint64_t result = 0;
    if (n->type == LND_NODE_SOURCE) {
        lnd_source *s = lnd_source_node_origin(n);
        if (lnd_load(&n->active) && lnd_source_status(s) != LND_SOURCE_EOF) {
            result = lnd_source_available(s);
            uint64_t length = lnd_source_limit(s);
            if (lnd_source_node_loop(n))
                result = UINT64_MAX;
            else if (length)
                result = LND_MIN(result, length > s->pos ? length - s->pos : 0);
        }
    } else if (n->type == LND_NODE_PCM_INPUT) {
        lnd_native_sound *s = (lnd_native_sound *)lnd_node_pcm_sound(n);
        if (s && (lnd_load(&s->state) == LND_SOUND_PLAYING || lnd_load(&s->state) == LND_SOUND_STALLED)) {
            uint64_t length = lnd_native_source_limit(s->source), pos = lnd_load(&s->source->position);
            if (s->source->input.frames) length = s->source->input.frames;
            result = length && !lnd_load(&s->loop) ? (length > pos ? length - pos : 0) : UINT64_MAX;
#if LND_MODULE_QUEUE
            uint64_t capacity = LND_QueueGetCapacityFrames((LND_SOURCE *)s->source);
            if (capacity) {
                result = LND_QueueGetBufferedFrames((LND_SOURCE *)s->source);
                if (!result && lnd_load(&s->source->end_requested)) result = UINT64_MAX;
            }
#endif
        }
    } else if (lnd_node_is_branch(n)) {
        lnd_split_node *b = (lnd_split_node *)n;
        if (b->splitter) result = lnd_node_available(b->splitter, &b->cursor);
    } else if (n->type == LND_NODE_MIXER && (n->mix_mode & LND_MIX_MODE_MASK) != LND_MIX_CONTINUOUS) {
        result = lnd_node_inputs_available(n, (n->mix_mode & LND_MIX_MODE_MASK) == LND_MIX_AVAILABLE);
    } else if (n->type == LND_NODE_SPLITTER || n->type == LND_NODE_CHANNEL_SPLITTER) {
        result = lnd_node_inputs_available(n, true);
    } else {
        result = UINT64_MAX;
    }
    lnd_spinlock_unlock(&n->lock);
    return lnd_sat_add(ahead, result);
}

static uint32_t lnd_node_read_block(lnd_node *n, float *dst, uint32_t nb) {
    return n->type == LND_NODE_SPLIT ? (uint32_t)lnd_split_read(n, dst, nb) : lnd_node_pull(n, nullptr, dst, nb);
}

uint64_t lnd_node_read_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, size_t frames) {
    if (n->outputs_count + (n->instance != nullptr) >= 1 && lnd_node_ensure_ring(n) != LND_OK) {
        lnd_error(LND_ERR_OUT_OF_MEMORY);
        return 0;
    }
    if (n->type == LND_NODE_SPLIT) return lnd_split_read_pcm(n, pcm, offset, frames);
    size_t done = 0;
    while (done < frames) {
        uint32_t want = (uint32_t)LND_MIN(frames - done, n->block);
        uint32_t got = lnd_node_pull_pcm(n, nullptr, pcm, offset + done, want);
        done += got;
        if (got < want) break;
    }
    return done;
}

uint64_t lnd_node_read(lnd_node *n, void *dst, int32_t format, uint64_t frames) {
    if (n->native) {
        if (frames > SIZE_MAX) {
            lnd_error(LND_ERR_INVALID_ARG);
            return 0;
        }
        LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = n->channels, .format = format};
        return lnd_node_read_pcm(n, &pcm, 0, (size_t)frames);
    }
    uint32_t ch = n->channels;
    if (n->outputs_count + (n->instance ? 1 : 0) >= 1 && lnd_node_ensure_ring(n) != LND_OK) {
        lnd_error(LND_ERR_OUT_OF_MEMORY);
        return 0;
    }
    uint64_t done = 0;
    if (format == LND_FORMAT_F32) {
        while (done < frames) {
            uint32_t nb = (uint32_t)LND_MIN(frames - done, (uint64_t)n->out_cap / 2);
            uint32_t got = lnd_node_read_block(n, (float *)dst + done * ch, nb);
            done += got;
            if (got < nb) break;
        }
        return done;
    }
    uint32_t block = 4096;
    float *tmp = lnd_alloc((size_t)block * ch * sizeof(float));
    if (!tmp) {
        lnd_error(LND_ERR_OUT_OF_MEMORY);
        return 0;
    }
    size_t frame_bytes = (size_t)ch * lnd_format_bytes(format);
    while (done < frames) {
        uint32_t nb = (uint32_t)LND_MIN(frames - done, (uint64_t)block);
        uint32_t got = lnd_node_read_block(n, tmp, nb);
        lnd_pcm_from_f32(format, tmp, (uint8_t *)dst + done * frame_bytes, (size_t)got * ch);
        done += got;
        if (got < nb) break;
    }
    lnd_free(tmp);
    return done;
}

void lnd_nodes_maintain(bool force, size_t limit) {
    limit = LND_MIN(limit, lnd_graph_ctx.pending_count);
    while (limit-- && lnd_graph_ctx.pending_head) {
        lnd_node *node = lnd_graph_ctx.pending_head;
        bool drained = lnd_node_drain(node, force);
        lnd_node_pending_remove(node);
        if (!drained) lnd_node_pending_add(node);
    }
}

void lnd_nodes_gc(void) { lnd_nodes_maintain(false, 8); }

void lnd_nodes_free_all(void) {
    lnd_node *n = lnd_graph_ctx.nodes;
    while (n) {
        lnd_node *next = n->next;
        if (!n->instance && n->type != LND_NODE_SOURCE) lnd_node_destroy(n);
        n = next;
    }
}

bool lnd_context_has_node(const lnd_node *n) {
    for (lnd_node *p = lnd_graph_ctx.nodes; p; p = p->next) {
        if (p == n) return true;
    }
    return false;
}
