#include "lindar_slide.h"
#include "src/notify.h"
#include "slide.h"
#include "playback/graph/context.h"
#include "playback/graph/native.h"
#include "playback/graph/gain.h"
#include "src/alloc.h"
#include "src/error.h"
#include "src/pcm.h"

#include <math.h>

typedef struct lnd_slide {
    struct lnd_slide *next;
    LND_SLIDE_STATE state;
    double value;
    double step;
    double factor;
    int32_t param;
    int32_t curve;
    uint32_t quantum;
    uint32_t remaining;
} lnd_slide;

static lnd_slide *lnd_slide_find(const lnd_node *n, int32_t param) {
    for (lnd_slide *s = n->slides; s; s = s->next)
        if (s->param == param) return s;
    return nullptr;
}

static void lnd_slide_set(lnd_node *n, int32_t param, float value) {
    if (param == LND_PARAM_GAIN) {
        n->gain = n->gain_target = value;
        n->gain_param = value;
        n->gain_ramp = 0;
    } else {
        lnd_processor_set_param(n, param, value);
        lnd_cmd c = {.op = LND_OP_PARAM, .u32 = (uint32_t)(param - LND_PARAM_USER), .f32 = value};
        if (n->vt->command) n->vt->command(n, &c, true);
    }
}

void lnd_slide_cancel(lnd_node *n, int32_t param) {
    lnd_slide *s = lnd_slide_find(n, param);
    if (s) s->state.active = false;
}

static void lnd_slide_advance(lnd_slide *s, uint32_t frames) {
    uint64_t count = LND_MIN((uint64_t)frames, s->state.duration_frames - s->state.elapsed_frames);
    s->state.elapsed_frames += count;
    if (s->state.elapsed_frames == s->state.duration_frames) {
        s->value = s->state.target;
        s->state.active = false;
    } else if (s->curve == LND_SLIDE_LOGARITHMIC) {
        s->value *= count == 1 ? s->factor : pow(s->factor, (double)count);
    } else
        s->value += s->step * (double)count;
    s->state.value = (float)s->value;
}

uint32_t lnd_slide_before(lnd_node *n, uint32_t frames) {
    uint32_t count = frames;
    for (lnd_slide *s = n->slides; s; s = s->next) {
        if (!s->state.active || s->param == LND_PARAM_GAIN) continue;
        if (!s->remaining) {
            lnd_slide_set(n, s->param, (float)s->value);
            s->remaining = (uint32_t)LND_MIN(s->quantum, s->state.duration_frames - s->state.elapsed_frames);
        }
        count = LND_MIN(count, s->remaining);
    }
    return count;
}

void lnd_slide_after(lnd_node *n, uint32_t frames) {
    for (lnd_slide *s = n->slides; s; s = s->next) {
        if (!s->state.active || s->param == LND_PARAM_GAIN) continue;
        lnd_slide_advance(s, frames);
        s->remaining -= frames;
        if (!s->state.active) {
            lnd_slide_set(n, s->param, s->state.target);
            lnd_notify_emit(n->notifications, LND_NOTIFY_SLIDE_END, n->notification_position, n->sample_rate_hz, s->param, s->state.target);
        }
    }
}

bool lnd_slide_gain(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_slide *s = lnd_slide_find(n, LND_PARAM_GAIN);
    if (!s || !s->state.active) return false;
    uint32_t count = (uint32_t)LND_MIN(frames, s->state.duration_frames - s->state.elapsed_frames);
    bool direct = pcm->format == LND_FORMAT_F32 && pcm->layout == LND_LAYOUT_INTERLEAVED && lnd_pcm_stride(pcm) == pcm->channels * sizeof(float) &&
                  (uintptr_t)lnd_pcm_at(pcm, 0, offset) % alignof(float) == 0;
    if (direct) {
        float *out = (float *)lnd_pcm_at(pcm, 0, offset);
        for (uint32_t f = 0; f < count; f++) {
            lnd_slide_advance(s, 1);
            float gain = (float)s->value;
            for (uint32_t c = 0; c < pcm->channels; c++)
                *out++ *= gain;
        }
    } else {
        float gains[64];
        for (uint32_t done = 0; done < count;) {
            uint32_t block = LND_MIN(count - done, 64);
            for (uint32_t f = 0; f < block; f++) {
                lnd_slide_advance(s, 1);
                gains[f] = (float)s->value;
            }
            lnd_graph_pcm_gains(pcm, offset + done, block, gains);
            done += block;
        }
    }
    lnd_slide_set(n, LND_PARAM_GAIN, (float)s->value);
    if (!s->state.active) lnd_notify_emit(n->notifications, LND_NOTIFY_SLIDE_END, n->notification_position + LND_MIN(count, n->rendered), n->sample_rate_hz, LND_PARAM_GAIN, s->state.target);
    if (count < frames) lnd_graph_pcm_gain(pcm, offset + count, frames - count, n->gain);
    return true;
}

void lnd_slide_free(lnd_node *n) {
    while (n->slides) {
        lnd_slide *s = n->slides;
        n->slides = s->next;
        lnd_free(s);
    }
}

static bool lnd_slide_param(lnd_node *n, int32_t param) {
    if (param == LND_PARAM_GAIN) return true;
    const LND_PROCESSOR_PROCS *procs = n->type == LND_NODE_PROCESSOR ? lnd_processor_procs(n) : nullptr;
    if (!procs || !procs->param || param < LND_PARAM_USER || param >= LND_PARAM_USER + LND_PARAM_USER_COUNT) return false;
    if (!procs->can_slide) return true;
    lnd_spinlock_lock(&n->lock);
    lnd_callback_enter();
    bool result = procs->can_slide(lnd_processor_user(n), param);
    lnd_callback_leave();
    lnd_spinlock_unlock(&n->lock);
    return result;
}

static double lnd_slide_current(lnd_node *n, const lnd_slide *s, int32_t param) {
    return s && s->state.active ? s->value : param == LND_PARAM_GAIN ? n->gain : lnd_processor_param(n, param);
}

int32_t LND_NodeSlideParam(LND_NODE *n, int32_t param, float target, const LND_SLIDE_CONFIG *config) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!n || !config || !isfinite(target) || config->curve < LND_SLIDE_LINEAR || config->curve > LND_SLIDE_LOGARITHMIC ||
        (param == LND_PARAM_GAIN && target < 0.0f)) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_context_has_node(n) || !lnd_slide_param(n, param) || (param != LND_PARAM_GAIN && !lnd_processor_validate(n, param, target))) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    LND_SLIDE_CONFIG saved = *config;
    lnd_slide *s = lnd_slide_find(n, param);
    lnd_slide *fresh = !s && saved.duration_frames ? lnd_alloc_zero(sizeof *fresh) : nullptr;
    int32_t result = !s && saved.duration_frames && !fresh ? LND_ERR_OUT_OF_MEMORY : LND_ERR_BUSY;
    for (uint32_t attempt = 0; result == LND_ERR_BUSY && attempt < 4; attempt++) {
        lnd_spinlock_lock(&n->lock);
        lnd_node_process(n, false);
        double current = lnd_slide_current(n, s, param);
        lnd_spinlock_unlock(&n->lock);
        if (saved.duration_frames && saved.curve == LND_SLIDE_LOGARITHMIC && (current == 0 || target == 0 || signbit(current) != signbit(target))) {
            result = LND_ERR_INVALID_ARG;
            break;
        }
        double step = saved.duration_frames ? ((double)target - current) / (double)saved.duration_frames : 0;
        double factor = saved.duration_frames && saved.curve == LND_SLIDE_LOGARITHMIC ? exp(log((double)target / current) / (double)saved.duration_frames) : 1;
        lnd_spinlock_lock(&n->lock);
        if (saved.duration_frames && lnd_slide_current(n, s, param) != current) {
            lnd_spinlock_unlock(&n->lock);
            continue;
        }
        if (!saved.duration_frames) {
            if (s) s->state = (LND_SLIDE_STATE){.value = target, .target = target};
            lnd_slide_set(n, param, target);
        } else {
            if (!s) {
                s = fresh;
                fresh = nullptr;
                s->param = param;
                s->next = n->slides;
                n->slides = s;
            }
            s->state = (LND_SLIDE_STATE){.value = (float)current, .target = target, .duration_frames = saved.duration_frames, .active = current != target};
            s->value = current;
            s->curve = saved.curve;
            s->step = step;
            s->factor = factor;
            s->quantum = saved.step_frames ? saved.step_frames : 32;
            s->remaining = 0;
            if (param == LND_PARAM_GAIN) n->gain_ramp = 0;
            if (!s->state.active) lnd_slide_set(n, param, target);
        }
        if (!saved.duration_frames || (s && !s->state.active))
            lnd_notify_emit(n->notifications, LND_NOTIFY_SLIDE_END, n->notification_position, n->sample_rate_hz, param, target);
        lnd_spinlock_unlock(&n->lock);
        result = LND_OK;
    }
    lnd_free(fresh);
    lnd_context_unlock();
    return lnd_error(result);
}


int32_t LND_NodeCancelSlide(LND_NODE *n, int32_t param) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!n) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_context_has_node(n)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    lnd_spinlock_lock(&n->lock);
    lnd_node_process(n, false);
    lnd_slide *s = lnd_slide_find(n, param);
    if (s && s->state.active) {
        lnd_slide_set(n, param, (float)s->value);
        s->state.active = false;
    }
    lnd_spinlock_unlock(&n->lock);
    lnd_context_unlock();
    return s ? LND_OK : lnd_error(LND_ERR_STATE);
}

int32_t LND_NodeGetSlideState(const LND_NODE *node, int32_t param, LND_SLIDE_STATE *state) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!node || !state) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_node *n = (lnd_node *)node;
    if (!lnd_context_has_node(n)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    lnd_spinlock_lock(&n->lock);
    lnd_slide *s = lnd_slide_find(n, param);
    if (s) *state = s->state;
    lnd_spinlock_unlock(&n->lock);
    lnd_context_unlock();
    return s ? LND_OK : lnd_error(LND_ERR_STATE);
}

bool LND_NodeIsSliding(const LND_NODE *n, int32_t param) {
    LND_SLIDE_STATE state = {0};
    return LND_NodeGetSlideState(n, param, &state) == LND_OK && state.active;
}
