#include "src/alloc.h"
#include "src/notify.h"
#include "node.h"
#include "native.h"
#include "gain.h"
#include "pcm/audio/simd.h"

#include <string.h>

enum {
    LND_RT_RUNNING,
    LND_RT_FADE_STOP,
    LND_RT_FADE_PAUSE,
};

typedef struct lnd_source_node {
    lnd_node base;
    lnd_source *origin;
    lnd_graph_source *owner;
    lnd_atomic_i32 state;
    lnd_atomic_u64 pos;
    uint64_t stop_target;
    float tgain;
    float ttarget;
    float tstep;
    uint32_t tramp;
    uint8_t phase;
    bool paused;
    bool loop;
} lnd_source_node;

static void lnd_source_ramp(lnd_source_node *s, float target, uint32_t ramp) {
    s->ttarget = target;
    if (ramp == 0) {
        s->tgain = target;
        s->tramp = 0;
        s->tstep = 0.0f;
    } else {
        s->tramp = ramp;
        s->tstep = (target - s->tgain) / (float)ramp;
    }
}

static void lnd_source_finish_stop(lnd_source_node *s, uint64_t seek_to) {
    lnd_source_seek(s->origin, seek_to);
    lnd_store(&s->pos, seek_to);
    lnd_store(&s->base.active, 0);
    s->paused = false;
    s->phase = LND_RT_RUNNING;
    s->tgain = 0.0f;
    s->tramp = 0;
}

static int64_t lnd_source_notified_read(lnd_source_node *s, const LND_PCM *pcm, size_t offset, uint32_t frames) {
#if LND_MODULE_NOTIFY
    uint64_t before = s->origin->pos;
#endif
    int64_t got = lnd_source_read_pcm(s->origin, pcm, offset, frames, false);
    if (got > 0) lnd_notify_range(s->base.notifications, before, s->origin->pos, s->origin->sample_rate_hz, true);
    return got;
}

static void lnd_source_notify_state(lnd_source_node *s, int32_t state) {
#if LND_MODULE_NOTIFY
    int32_t previous = lnd_load_relaxed(&s->state);
#endif
    lnd_store(&s->state, state);
#if LND_MODULE_NOTIFY
    if (state == previous) return;
    int32_t event = state == LND_SOUND_STOPPED ? LND_NOTIFY_END : state == LND_SOUND_STALLED ? LND_NOTIFY_STALLED : previous == LND_SOUND_STALLED ? LND_NOTIFY_RESUMED : -1;
    if (event >= 0) lnd_notify_transport(s->base.notifications, event, s->origin->pos, s->base.notification_position + s->base.rendered, s->origin->sample_rate_hz);
#endif
}

static void lnd_source_render_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_source_node *s = (lnd_source_node *)n;
    n->rendered = 0;
    if (!lnd_load_relaxed(&n->active) || s->paused) {
        LND_PcmSilence(pcm, offset, frames);
        return;
    }
    uint32_t done = 0;
    bool ended = false, wrapped = false;
    while (done < frames && !ended) {
        uint32_t want = frames - done;
        int64_t count = lnd_source_notified_read(s, pcm, offset + done, want);
        uint32_t got = count > 0 ? (uint32_t)count : 0;
        if (got) wrapped = false;
        bool ramped = s->tramp != 0;
        lnd_graph_pcm_ramp(pcm, offset + done, got, &s->tgain, s->tstep, s->ttarget, &s->tramp);
        if (ramped) {
            if (s->tramp == 0 && s->phase != LND_RT_RUNNING) {
                done += got;
                n->rendered = done;
                if (s->phase == LND_RT_FADE_STOP) {
                    lnd_source_finish_stop(s, s->stop_target);
                } else {
                    s->paused = true;
                    s->phase = LND_RT_RUNNING;
                    lnd_store(&s->pos, s->origin->pos);
                    lnd_store(&n->active, 0);
                }
                if (done < frames) LND_PcmSilence(pcm, offset + done, frames - done);
                return;
            }
        }
        done += got;
        if (lnd_source_status(s->origin) == LND_SOURCE_EOF) {
            if (s->loop && !wrapped && lnd_source_seek(s->origin, 0) == LND_OK) wrapped = true;
            else ended = true;
        } else if (!got) break;
    }
    n->rendered = done;
    if (ended && s->phase == LND_RT_FADE_STOP) {
        lnd_source_finish_stop(s, s->stop_target);
    } else if (ended) {
        lnd_store(&s->pos, s->origin->pos);
        lnd_store(&n->active, 0);
        if (s->phase == LND_RT_RUNNING) lnd_source_notify_state(s, LND_SOUND_STOPPED);
        else lnd_store(&s->state, LND_SOUND_STOPPED);
    } else if (s->phase != LND_RT_FADE_STOP && lnd_load_relaxed(&s->state) != LND_SOUND_STOPPED) {
        lnd_store(&s->pos, s->origin->pos);
        if (s->phase == LND_RT_RUNNING && lnd_load_relaxed(&s->state) != LND_SOUND_PAUSED)
            lnd_source_notify_state(s, lnd_source_status(s->origin) == LND_SOURCE_WAITING ? LND_SOUND_STALLED : LND_SOUND_PLAYING);
    }
    if (done < frames) LND_PcmSilence(pcm, offset + done, frames - done);
}

static void lnd_source_render(lnd_node *n, float *dst, uint32_t frames) {
    LND_PCM pcm = {.data = dst, .frames = frames, .channels = n->channels, .format = LND_FORMAT_F32};
    lnd_source_render_pcm(n, &pcm, 0, frames);
}

static void lnd_source_command(lnd_node *n, const lnd_cmd *c, bool immediate) {
    lnd_source_node *s = (lnd_source_node *)n;
    uint32_t ramp = immediate ? 0 : n->ramp;
    bool active = lnd_load_relaxed(&n->active) != 0;
    switch (c->op) {
    case LND_OP_START:
        lnd_add(&n->revision, 1);
        if (lnd_source_status(s->origin) == LND_SOURCE_EOF && lnd_source_seek(s->origin, 0) != LND_OK) {
            lnd_store(&s->state, LND_SOUND_STOPPED);
            lnd_store(&n->active, 0);
            break;
        }
        s->loop = c->u64 != 0;
        s->paused = false;
        s->phase = LND_RT_RUNNING;
        if (!active) {
            s->tgain = 0.0f;
            lnd_store(&n->active, 1);
        }
        lnd_source_ramp(s, 1.0f, ramp);
        break;
    case LND_OP_STOP:
        s->stop_target = c->u64;
        if (!active || ramp == 0 || s->paused || lnd_source_status(s->origin) == LND_SOURCE_WAITING) {
            lnd_source_finish_stop(s, s->stop_target);
            break;
        }
        s->phase = LND_RT_FADE_STOP;
        lnd_source_ramp(s, 0.0f, ramp);
        break;
    case LND_OP_PAUSE:
        if (!active || s->paused) break;
        if (ramp == 0 || lnd_source_status(s->origin) == LND_SOURCE_WAITING) {
            s->paused = true;
            s->tgain = 0.0f;
            s->phase = LND_RT_RUNNING;
            lnd_store(&n->active, 0);
            break;
        }
        s->phase = LND_RT_FADE_PAUSE;
        lnd_source_ramp(s, 0.0f, ramp);
        break;
    case LND_OP_RESUME:
        lnd_store(&n->active, 1);
        s->paused = false;
        s->phase = LND_RT_RUNNING;
        lnd_source_ramp(s, 1.0f, ramp);
        break;
    case LND_OP_SEEK: {
        uint64_t target = c->u64;
        uint64_t length = lnd_source_limit(s->origin);
        if (length && target > length) target = length;
        if (lnd_source_seek(s->origin, target) == LND_OK) {
            lnd_store(&s->pos, target);
            lnd_add(&n->revision, 1);
        }
        if (s->phase == LND_RT_FADE_STOP || !active) s->stop_target = target;
        break;
    }
    case LND_OP_LOOP:
        s->loop = c->u32 != 0;
        break;
    default:
        break;
    }
}

static void lnd_source_finish(lnd_node *n) {
    lnd_source_node *s = (lnd_source_node *)n;
    if (s->phase == LND_RT_FADE_STOP) {
        lnd_source_finish_stop(s, s->stop_target);
    } else if (s->phase == LND_RT_FADE_PAUSE) {
        s->paused = true;
        s->phase = LND_RT_RUNNING;
        lnd_store(&n->active, 0);
    }
    s->tgain = s->ttarget;
    s->tramp = 0;
}

static const lnd_node_vt lnd_source_node_vt = {
    .render = lnd_source_render,
    .render_pcm = lnd_source_render_pcm,
    .command = lnd_source_command,
    .finish = lnd_source_finish,
};

lnd_node *lnd_node_create_source(lnd_source *origin, lnd_graph_source *owner) {
    lnd_source_node *s = (lnd_source_node *)lnd_node_alloc(sizeof *s, &lnd_source_node_vt, LND_NODE_SOURCE, origin->channels, origin->sample_rate_hz);
    if (!s) return nullptr;
    s->origin = origin;
    s->owner = owner;
    s->ttarget = 1.0f;
    lnd_store_relaxed(&s->state, LND_SOUND_STOPPED);
    return &s->base;
}

lnd_source *lnd_source_node_origin(const lnd_node *n) { return n && n->type == LND_NODE_SOURCE ? ((const lnd_source_node *)n)->origin : nullptr; }

lnd_graph_source *lnd_source_node_owner(const lnd_node *n) { return n && n->type == LND_NODE_SOURCE ? ((const lnd_source_node *)n)->owner : nullptr; }

bool lnd_source_node_loop(const lnd_node *n) { return ((const lnd_source_node *)n)->loop; }

int32_t lnd_source_node_state(const lnd_node *n) {
    return n && n->type == LND_NODE_SOURCE ? lnd_load(&((const lnd_source_node *)n)->state) : LND_SOUND_STOPPED;
}

uint64_t lnd_source_node_pos(const lnd_node *n) { return n && n->type == LND_NODE_SOURCE ? lnd_load(&((const lnd_source_node *)n)->pos) : 0; }

void lnd_source_node_set_state(lnd_node *n, int32_t state) { lnd_store(&((lnd_source_node *)n)->state, state); }

void lnd_source_node_set_pos(lnd_node *n, uint64_t pos) { lnd_store(&((lnd_source_node *)n)->pos, pos); }

void lnd_source_node_reset(lnd_node *n) {
    lnd_spinlock_lock(&n->lock);
    lnd_node_process(n, true);
    lnd_source_node_set_state(n, LND_SOUND_STOPPED);
    lnd_source_command(n, &(lnd_cmd){.op = LND_OP_STOP}, true);
    lnd_source_finish(n);
    lnd_spinlock_unlock(&n->lock);
}

uint64_t lnd_source_node_read(lnd_node *n, float *dst, uint64_t frames, bool loop) {
    lnd_source_node *s = (lnd_source_node *)n;
    uint32_t ch = n->channels;
    uint64_t total = 0;
    bool wrapped = false;
    while (total < frames) {
        uint64_t got = lnd_source_read_sync(s->origin, dst + total * ch, frames - total);
        total += got;
        if (total == frames) break;
        if (lnd_source_status(s->origin) != LND_SOURCE_EOF || !loop || (got == 0 && wrapped) || lnd_source_seek(s->origin, 0) != LND_OK) break;
        lnd_source_sync(s->origin);
        wrapped = true;
    }
    lnd_store(&s->pos, s->origin->pos);
    return total;
}
