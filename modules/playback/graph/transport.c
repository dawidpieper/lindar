#include "lindar_pcm_float.h"
#include "context.h"
#include "sound.h"
#include "walk.h"
#include "src/native.h"
#include "src/config.h"
#include "src/error.h"
#include "io/devices/capture.h"
#if LND_MODULE_DEVICES
#include "io/devices/engine.h"
#include "io/devices/context.h"
#endif

#include <math.h>

static lnd_node *lnd_first_source(lnd_node *root) {
    lnd_walk walk = lnd_walk_begin(root, true, false);
    for (lnd_node *n = lnd_walk_next(&walk); n; n = lnd_walk_next(&walk))
        if (n->type == LND_NODE_SOURCE || n->type == LND_NODE_PCM_INPUT) return n;
    return nullptr;
}

typedef void (*lnd_source_fn)(lnd_node *src, void *ctx);

static void lnd_each_source(lnd_node *root, lnd_source_fn fn, void *ctx) {
    lnd_walk walk = lnd_walk_begin(root, true, false);
    for (lnd_node *n = lnd_walk_next(&walk); n; n = lnd_walk_next(&walk))
        if (n->type == LND_NODE_SOURCE || n->type == LND_NODE_PCM_INPUT) fn(n, ctx);
}

#define LND_SOUND_ENTER(s)                                                                                                                      \
    if (!(s)) return lnd_error(LND_ERR_INVALID_ARG);                                                                                            \
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);                                                                                   \
    lnd_context_gc();                                                                                                                           \
    if (!lnd_sound_valid(s)) {                                                                                                                  \
        lnd_context_unlock();                                                                                                                   \
        return lnd_error(LND_ERR_INVALID_ARG);                                                                                                  \
    }                                                                                                                                           \
    lnd_spinlock_lock(&(s)->lock);

LND_INLINE int32_t lnd_sound_control_leave(lnd_graph_sound *sound, int32_t result) {
    lnd_spinlock_unlock(&sound->lock);
    lnd_context_unlock();
    return lnd_error(result);
}

LND_NODE *lnd_graph_sound_get_node(const lnd_graph_sound *s) { return s ? s->node : nullptr; }

lnd_graph_source *lnd_graph_sound_get_source(const lnd_graph_sound *s) {
    if (!s) return nullptr;
    return (lnd_graph_source *)LND_NodeGetSource(s->node);
}

static int32_t lnd_sound_ensure_output(lnd_graph_sound *s) {
    if (s->node->outputs_count || s->node->extra_consumers || s->node->instance || lnd_cfg_u32(LND_CFG_RUN_MODE) != LND_MODE_REALTIME) return LND_OK;
#if LND_MODULE_DEVICES
    lnd_instance *i = lnd_context_default_output();
    if (!i) return LND_ErrorGetLast() ? LND_ErrorGetLast() : LND_ERR_NO_DEVICE;
    return lnd_node_connect(s->node, i->master);
#else
    return LND_OK;
#endif
}

typedef struct lnd_transport_ctx {
    lnd_graph_sound *sound;
    uint64_t frame;
    union {
        uint32_t sample_rate_hz;
        uint32_t paused;
        uint32_t loop;
    } argument;
    int32_t result;
    bool resume;
} lnd_transport_ctx;

static void lnd_transport_play(lnd_node *src, void *user) {
    lnd_transport_ctx *t = user;
    LND_SOUND *native = lnd_node_pcm_sound(src);
    if (native) {
        int32_t result = lnd_native_sound_play((lnd_native_sound *)native);
        if (result != LND_OK) t->result = result;
        return;
    }
    int32_t state = lnd_source_node_state(src);
    if (state == LND_SOUND_PLAYING || state == LND_SOUND_STALLED) return;
    int32_t r =
        (state == LND_SOUND_PAUSED || t->resume) ? lnd_node_post(src, LND_OP_RESUME, 0, 0, 0.0f) : lnd_node_post(src, LND_OP_START, t->sound->loop, 0, 0.0f);
    if (r != LND_OK) {
        t->result = r;
        return;
    }
    lnd_source_node_set_state(src, LND_SOUND_PLAYING);
    lnd_node_activate(src);
}

static void lnd_transport_pause(lnd_node *src, void *user) {
    lnd_transport_ctx *t = user;
    LND_SOUND *native = lnd_node_pcm_sound(src);
    if (native) {
        int32_t result = lnd_native_sound_set_pause((lnd_native_sound *)native, t->argument.paused != 0);
        if (result != LND_OK) t->result = result;
        return;
    }
    bool pause = t->argument.paused != 0;
    int32_t state = lnd_source_node_state(src);
    if (pause && (state == LND_SOUND_PLAYING || state == LND_SOUND_STALLED)) {
        int32_t result = lnd_node_post(src, LND_OP_PAUSE, 0, 0, 0.0f);
        if (result == LND_OK)
            lnd_source_node_set_state(src, LND_SOUND_PAUSED);
        else
            t->result = result;
    } else if (!pause && state == LND_SOUND_PAUSED) {
        int32_t result = lnd_node_post(src, LND_OP_RESUME, 0, 0, 0.0f);
        if (result == LND_OK) {
            lnd_source_node_set_state(src, LND_SOUND_PLAYING);
            lnd_node_activate(src);
        } else {
            t->result = result;
        }
    }
}

static void lnd_transport_stop(lnd_node *src, void *user) {
    lnd_transport_ctx *t = user;
    LND_SOUND *native = lnd_node_pcm_sound(src);
    if (native) {
        int32_t result = lnd_native_sound_stop((lnd_native_sound *)native);
        if (result != LND_OK) t->result = result;
        return;
    }
    lnd_spinlock_lock(&src->lock);
    t->result = lnd_node_post(src, LND_OP_STOP, 0, 0, 0.0f);
    if (t->result == LND_OK) {
        lnd_source_node_set_state(src, LND_SOUND_STOPPED);
        lnd_source_node_set_pos(src, 0);
    }
    lnd_spinlock_unlock(&src->lock);
    if (t->result == LND_OK && !lnd_load(&src->active)) lnd_node_drain(src, true);
}

static void lnd_transport_seek(lnd_node *src, void *user) {
    lnd_transport_ctx *t = user;
    LND_SOUND *native = lnd_node_pcm_sound(src);
    if (native) {
        int32_t result = lnd_native_sound_seek_frames((lnd_native_sound *)native, lnd_scale_frames(t->frame, t->argument.sample_rate_hz, src->sample_rate_hz));
        if (result != LND_OK) t->result = result;
        return;
    }
    lnd_source *origin = lnd_source_node_origin(src);
    if (!lnd_source_can_seek(origin)) {
        t->result = LND_ERR_UNSUPPORTED;
        return;
    }
    uint64_t frame = lnd_scale_frames(t->frame, t->argument.sample_rate_hz, src->sample_rate_hz);
    uint64_t length = lnd_source_limit(origin);
    if (length && frame > length) frame = length;
    int32_t result = lnd_node_post(src, LND_OP_SEEK, frame, 0, 0.0f);
    if (result != LND_OK) {
        t->result = result;
        return;
    }
    lnd_source_node_set_pos(src, frame);
    if (!lnd_load(&src->active)) {
        lnd_node_drain(src, true);
        lnd_source_sync(origin);
        lnd_source_node_set_pos(src, origin->pos);
    }
}

static void lnd_transport_loop(lnd_node *src, void *user) {
    lnd_transport_ctx *t = user;
    LND_SOUND *native = lnd_node_pcm_sound(src);
    if (native) {
        int32_t result = lnd_native_sound_set_loop((lnd_native_sound *)native, t->argument.loop != 0);
        if (result != LND_OK) t->result = result;
        return;
    }
    int32_t result = lnd_node_post(src, LND_OP_LOOP, 0, t->argument.loop, 0.0f);
    if (result != LND_OK)
        t->result = result;
    else if (!lnd_load(&src->active))
        lnd_node_drain(src, true);
}

static void lnd_transport_resync(lnd_node *src, void *user) {
    LND_SOUND *native = lnd_node_pcm_sound(src);
    if (native) {
        lnd_transport_ctx *t = user;
        lnd_graph_sound *s = t->sound;
        if (s->node == src) {
            uint64_t pos = s->source_base_frames + lnd_scale_frames(s->delivered_frames, lnd_sound_rate(s), src->sample_rate_hz);
            uint64_t length = lnd_native_source_limit(((lnd_native_sound *)native)->source);
            if (length && pos > length) pos = s->loop ? pos % length : length;
            int32_t result = lnd_native_sound_seek_frames((lnd_native_sound *)native, pos);
            if (result != LND_OK) t->result = result;
        }
        return;
    }
    lnd_transport_ctx *t = user;
    lnd_source *origin = lnd_source_node_origin(src);
    if (!lnd_source_can_seek(origin)) return;
    uint64_t pos = lnd_source_node_pos(src);
    if (t->sound->node == src) pos = t->sound->source_base_frames + lnd_scale_frames(t->sound->delivered_frames, lnd_sound_rate(t->sound), src->sample_rate_hz);
    int32_t result = lnd_node_post(src, LND_OP_SEEK, pos, 0, 0.0f);
    if (result != LND_OK) {
        t->result = result;
        return;
    }
    if (!lnd_load(&src->active)) {
        lnd_node_drain(src, true);
        lnd_source_sync(origin);
    }
}

static void lnd_sound_resync(lnd_graph_sound *s, lnd_transport_ctx *t) {
    if (!s->ahead || s->transport) return;
    lnd_each_source(s->node, lnd_transport_resync, t);
    s->ahead = false;
    s->reset_pending = true;
}

int32_t lnd_sound_sync_read(lnd_graph_sound *s) {
    if (!s->transport) return LND_OK;
    s->transport = false;
    lnd_transport_ctx t = {.sound = s, .result = LND_OK};
    lnd_sound_resync(s, &t);
    s->reset_pending = true;
    return t.result;
}

static void lnd_split_transport(lnd_node *b, uint32_t op, int32_t state, lnd_transport_ctx *t) {
    int32_t result = lnd_node_post(b, op, 0, 0, 0.0f);
    if (result != LND_OK) {
        t->result = result;
        return;
    }
    lnd_split_set_state(b, state);
    lnd_node_drain(b, true);
    if (op == LND_OP_START || op == LND_OP_RESUME) lnd_node_activate(b);
}

int32_t lnd_graph_sound_play(lnd_graph_sound *s) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    LND_SOUND_ENTER(s);
    int32_t r = lnd_sound_ensure_output(s);
    if (r != LND_OK) return lnd_sound_control_leave(s, r);
    s->paused = false;
    lnd_transport_ctx t = {.sound = s, .result = LND_OK};
    if (lnd_sound_draining(s)) {
        lnd_each_source(s->node, lnd_transport_pause, &t);
        return lnd_sound_control_leave(s, t.result);
    }
    lnd_sound_resync(s, &t);
    lnd_each_source(s->node, lnd_transport_play, &t);
    if (s->node->type == LND_NODE_SPLIT) {
        int32_t state = lnd_split_state(s->node);
        if (state != LND_SOUND_PLAYING) lnd_split_transport(s->node, state == LND_SOUND_PAUSED ? LND_OP_RESUME : LND_OP_START, LND_SOUND_PLAYING, &t);
    }
    return lnd_sound_control_leave(s, t.result);
}

int32_t lnd_graph_sound_set_pause(lnd_graph_sound *s, bool pause) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    LND_SOUND_ENTER(s);
    s->paused = pause;
    lnd_transport_ctx t = {.sound = s, .argument.paused = pause, .result = LND_OK};
    if (s->node->type == LND_NODE_SPLIT) {
        int32_t state = lnd_split_state(s->node);
        if (pause && (state == LND_SOUND_PLAYING || state == LND_SOUND_STALLED))
            lnd_split_transport(s->node, LND_OP_PAUSE, LND_SOUND_PAUSED, &t);
        else if (!pause && state == LND_SOUND_PAUSED)
            lnd_split_transport(s->node, LND_OP_RESUME, LND_SOUND_PLAYING, &t);
        return lnd_sound_control_leave(s, t.result);
    }
    if (!pause) lnd_sound_resync(s, &t);
    lnd_each_source(s->node, lnd_transport_pause, &t);
    return lnd_sound_control_leave(s, t.result);
}

int32_t lnd_graph_sound_stop(lnd_graph_sound *s) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    LND_SOUND_ENTER(s);
    s->paused = false;
    lnd_transport_ctx t = {.sound = s, .result = LND_OK};
    if (s->node->type == LND_NODE_SPLIT) {
        if (lnd_split_state(s->node) != LND_SOUND_STOPPED) lnd_split_transport(s->node, LND_OP_STOP, LND_SOUND_STOPPED, &t);
        return lnd_sound_control_leave(s, t.result);
    }
    lnd_each_source(s->node, lnd_transport_stop, &t);
    s->reset_pending = true;
    s->ahead = false;
    return lnd_sound_control_leave(s, t.result);
}

int32_t lnd_graph_sound_get_state(const lnd_graph_sound *s) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!s) return LND_SOUND_STOPPED;
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_spinlock_lock((lnd_spinlock *)&s->lock);
    int32_t state;
    if (s->node->type == LND_NODE_SPLIT) {
        state = lnd_split_state(s->node);
    } else {
        lnd_node *src = lnd_first_source(s->node);
        state = src ? (lnd_node_pcm_sound(src) ? lnd_native_sound_get_state((lnd_native_sound *)lnd_node_pcm_sound(src)) : lnd_source_node_state(src))
                    : LND_SOUND_STOPPED;
    }
    if (s->paused)
        state = LND_SOUND_PAUSED;
    else if (state == LND_SOUND_STOPPED && lnd_sound_draining(s))
        state = LND_SOUND_PLAYING;
    lnd_spinlock_unlock((lnd_spinlock *)&s->lock);
    lnd_context_unlock();
    return state;
}

#if LND_MODULE_DEVICES
static lnd_instance *lnd_node_destination(lnd_node *root) {
    lnd_walk walk = lnd_walk_begin(root, false, false);
    for (lnd_node *n = lnd_walk_next(&walk); n; n = lnd_walk_next(&walk))
        if (n->instance) return n->instance;
    return nullptr;
}

#endif

uint64_t lnd_graph_sound_get_position_frames(const lnd_graph_sound *s) {
    if (lnd_callback_active()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }

    if (!s) return 0;
    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }
    lnd_spinlock_lock((lnd_spinlock *)&s->lock);
    bool split = s->node->type == LND_NODE_SPLIT;
    lnd_node *src = split ? s->node : lnd_first_source(s->node);
    uint64_t pos =
        split ? lnd_split_pos(s->node)
              : (src ? (lnd_node_pcm_sound(src) ? lnd_native_sound_get_position_frames((lnd_native_sound *)lnd_node_pcm_sound(src)) : lnd_source_node_pos(src))
                     : s->node->produced);
    if (src && src == s->node && s->ahead) {
        pos = s->source_base_frames + lnd_scale_frames(s->delivered_frames, lnd_sound_rate(s), src->sample_rate_hz);
        uint64_t length = lnd_node_pcm_sound(src) ? lnd_native_source_limit(((lnd_native_sound *)lnd_node_pcm_sound(src))->source)
                                                  : lnd_source_limit(lnd_source_node_origin(src));
        if (length && pos > length) pos = s->loop ? pos % length : length;
    }
#if LND_MODULE_DEVICES
    if (src && lnd_cfg_bool(LND_CFG_GRAPH_POSITION_COMPENSATE) && lnd_load(&src->active)) {
        lnd_instance *i = lnd_node_destination(src);
        if (i) {
            uint64_t latency = (uint64_t)i->cfg.latency_frames * src->sample_rate_hz / (i->cfg.sample_rate_hz ? i->cfg.sample_rate_hz : 1);
            pos = pos > latency ? pos - latency : 0;
        }
    }
#endif
    pos = lnd_scale_frames(pos, src ? src->sample_rate_hz : s->node->sample_rate_hz, lnd_sound_rate(s));
    lnd_spinlock_unlock((lnd_spinlock *)&s->lock);
    lnd_context_unlock();
    return pos;
}

uint32_t lnd_graph_sound_get_sample_rate_hz(const lnd_graph_sound *s) { return s ? lnd_sound_rate(s) : 0; }

uint32_t lnd_graph_sound_get_channels(const lnd_graph_sound *s) { return s ? lnd_sound_channels(s) : 0; }

double lnd_graph_sound_get_position_seconds(const lnd_graph_sound *s) {
    uint32_t sample_rate_hz = lnd_graph_sound_get_sample_rate_hz(s);
    return sample_rate_hz ? (double)lnd_graph_sound_get_position_frames(s) / sample_rate_hz : 0.0f;
}

int32_t lnd_graph_sound_seek_frames(lnd_graph_sound *s, uint64_t frame) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    LND_SOUND_ENTER(s);
    if (s->node->type == LND_NODE_SPLIT) return lnd_sound_control_leave(s, LND_ERR_UNSUPPORTED);
    lnd_transport_ctx t = {.sound = s, .frame = frame, .argument.sample_rate_hz = lnd_sound_rate(s), .result = LND_OK, .resume = lnd_sound_draining(s)};
    lnd_each_source(s->node, lnd_transport_seek, &t);
    if (t.result == LND_OK && t.resume) {
        lnd_each_source(s->node, lnd_transport_play, &t);
        if (s->paused) {
            t.argument.paused = true;
            lnd_each_source(s->node, lnd_transport_pause, &t);
        }
    }
    s->reset_pending = true;
    s->ahead = false;
    return lnd_sound_control_leave(s, t.result);
}

int32_t lnd_graph_sound_seek_seconds(lnd_graph_sound *s, double sec) {
    if (!s || !(sec >= 0.0f)) return lnd_error(LND_ERR_INVALID_ARG);
    uint32_t sample_rate_hz = lnd_graph_sound_get_sample_rate_hz(s);
    double frame = (double)sec * sample_rate_hz;
    if (!isfinite(frame) || frame >= (double)UINT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_graph_sound_seek_frames(s, (uint64_t)(frame + 0.5));
}

uint64_t lnd_graph_sound_get_length_frames(const lnd_graph_sound *s) {
    if (lnd_callback_active()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }

    if (!s) return 0;
    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }
    lnd_node *src = lnd_first_source(s->node);
    uint64_t length = src ? lnd_scale_frames(lnd_node_pcm_sound(src) ? lnd_native_sound_get_length_frames((lnd_native_sound *)lnd_node_pcm_sound(src))
                                                                     : lnd_source_length(lnd_source_node_origin(src)),
                                             src->sample_rate_hz, lnd_sound_rate(s))
                          : 0;
    lnd_context_unlock();
    return length;
}

double lnd_graph_sound_get_length_seconds(const lnd_graph_sound *s) {
    uint32_t sample_rate_hz = lnd_graph_sound_get_sample_rate_hz(s);
    return sample_rate_hz ? (double)lnd_graph_sound_get_length_frames(s) / sample_rate_hz : 0.0f;
}

int32_t lnd_graph_sound_set_gain(lnd_graph_sound *s, float gain) {
    if (!s) return lnd_error(LND_ERR_INVALID_ARG);
    LND_SOUND *native = lnd_node_pcm_sound(s->node);
    int32_t r = native ? lnd_native_sound_set_gain((lnd_native_sound *)native, gain) : LND_NodeSetGain(s->node, gain);
    if (r == LND_OK) s->gain = gain;
    return r;
}

float lnd_graph_sound_get_gain(const lnd_graph_sound *s) {
    LND_SOUND *native = s ? lnd_node_pcm_sound(s->node) : nullptr;
    return native ? lnd_native_sound_get_gain((const lnd_native_sound *)native) : s ? s->gain : 0.0f;
}

int32_t lnd_graph_sound_set_loop(lnd_graph_sound *s, bool loop) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    LND_SOUND_ENTER(s);
    s->loop = loop;
    lnd_transport_ctx t = {.sound = s, .argument.loop = loop, .result = LND_OK};
    lnd_each_source(s->node, lnd_transport_loop, &t);
    return lnd_sound_control_leave(s, t.result);
}

bool lnd_graph_sound_get_loop(const lnd_graph_sound *s) { return s ? s->loop : false; }

int32_t lnd_graph_sound_set_output(lnd_graph_sound *s, LND_NODE *dst) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    LND_SOUND_ENTER(s);
    if (s->node->instance) return lnd_sound_control_leave(s, LND_ERR_UNSUPPORTED);
    if (dst && !lnd_context_has_node(dst)) return lnd_sound_control_leave(s, LND_ERR_INVALID_ARG);
    if (dst && (dst == s->node || lnd_node_reaches(dst, s->node))) return lnd_sound_control_leave(s, LND_ERR_CYCLE);
    int32_t r = lnd_node_set_output(s->node, dst);
    return lnd_sound_control_leave(s, r);
}

LND_NODE *lnd_graph_sound_get_output(const lnd_graph_sound *s) { return s ? LND_NodeGetOutput(s->node, 0) : nullptr; }
