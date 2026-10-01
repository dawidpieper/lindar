#include "lindar_autofree.h"
#include "lindar_pcm_float.h"
#include "playback/graph/context.h"
#include "playback/graph/sound.h"
#include "playback/graph/native.h"
#include "src/native.h"
#include "src/error.h"
#if LND_MODULE_QUEUE
#include "lindar_queue.h"
#endif

typedef struct lnd_autofree {
    lnd_node base;
    LND_SOURCE *source;
    LND_SOUND *sound;
    lnd_node *input;
    LND_AUTOFREE id;
    bool owns_source;
    lnd_atomic_u64 end;
    lnd_atomic_u32 armed;
    struct lnd_autofree *next;
} lnd_autofree;

static lnd_autofree *lnd_autofrees;
static uint64_t lnd_autofree_serial;

static lnd_autofree *lnd_autofree_find(LND_AUTOFREE id) {
    for (lnd_autofree *a = lnd_autofrees; a; a = a->next)
        if (a->id == id) return a;
    return nullptr;
}

static void lnd_autofree_finish(lnd_autofree *a, uint32_t frames) {
    int32_t status = LND_SourceGetStatus(a->source);
    int32_t state = a->source->ops ? lnd_source_node_state(a->input) : lnd_native_sound_get_state((lnd_native_sound *)a->sound);
    if (status < 0 || (status == LND_SOURCE_EOF && state == LND_SOUND_STOPPED)) lnd_store(&a->end, a->base.produced + a->base.rendered);
    lnd_store(&a->base.active, 1);
}

static void lnd_autofree_render(lnd_node *n, float *dst, uint32_t frames) {
    lnd_autofree *a = (lnd_autofree *)n;
    if (!lnd_load(&a->armed) || lnd_load(&a->end) != UINT64_MAX) {
        n->rendered = 0;
        lnd_store(&n->status, lnd_load(&a->armed) ? LND_SOURCE_EOF : LND_SOURCE_WAITING);
        LND_PCM pcm = {.data = dst, .frames = frames, .channels = n->channels, .format = LND_FORMAT_F32};
        LND_PcmSilence(&pcm, 0, frames);
        return;
    }
    lnd_bus_render(n, dst, frames);
    lnd_autofree_finish(a, frames);
}

static void lnd_autofree_render_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_autofree *a = (lnd_autofree *)n;
    if (!lnd_load(&a->armed) || lnd_load(&a->end) != UINT64_MAX) {
        n->rendered = 0;
        lnd_store(&n->status, lnd_load(&a->armed) ? LND_SOURCE_EOF : LND_SOURCE_WAITING);
        LND_PcmSilence(pcm, offset, frames);
        return;
    }
    lnd_bus_render_pcm(n, pcm, offset, frames);
    lnd_autofree_finish(a, frames);
}

static void lnd_autofree_destroy(lnd_node *n) {
    lnd_autofree *a = (lnd_autofree *)n;
    for (lnd_autofree **p = &lnd_autofrees; *p; p = &(*p)->next) {
        if (*p != a) continue;
        *p = a->next;
        break;
    }
    if (!a->owns_source) return;
    if (!a->source->ops) lnd_node_destroy(a->input);
    if (!lnd_ctx.closing) LND_SourceFree(a->source);
}

static const lnd_node_vt lnd_autofree_vt = {.render = lnd_autofree_render, .render_pcm = lnd_autofree_render_pcm, .destroy = lnd_autofree_destroy};

LND_AUTOFREE LND_AutofreeTakeSource(LND_SOURCE *source, LND_NODE *output) {
    if (lnd_callback_active() || !lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }
    int32_t result = LND_OK;
    lnd_node *input = nullptr;
    LND_SOUND *sound = nullptr;
    lnd_autofree *a = nullptr;
    bool created = false;
    if (!lnd_ctx.initialized || !source || !output || !lnd_context_has_node(output)) result = LND_ERR_INVALID_ARG;
    if (result == LND_OK) {
        if (source->ops) {
            lnd_graph_source *s = (lnd_graph_source *)source;
            if (source->ops != &lnd_graph_source_ops || !lnd_source_obj_valid(s) || s->view)
                result = LND_ERR_INVALID_ARG;
            else
                input = s->node;
        } else {
            lnd_native_source *s = (lnd_native_source *)source;
            if (s->sound) {
                input = lnd_pcm_input_node((LND_SOUND *)s->sound, false);
                if (s->sound->references > (input ? 1u : 0u)) result = LND_ERR_BUSY;
            }
        }
        if (input && (input->outputs_count || input->extra_consumers || input->instance)) result = LND_ERR_BUSY;
        for (lnd_autofree *p = lnd_autofrees; p; p = p->next)
            if (p->source == source) result = LND_ERR_BUSY;
    }
    if (result == LND_OK) {
        created = !input;
        sound = LND_SourceEnsureSound(source, nullptr);
        input = sound ? LND_SourceEnsureNode(source) : nullptr;
        if (!input)
            result = LND_ErrorGetLast();
        else if (LND_SoundGetState(sound) != LND_SOUND_STOPPED)
            result = LND_ERR_BUSY;
    }
    if (result == LND_OK && lnd_autofree_serial == UINT64_MAX) result = LND_ERR_STATE;
    if (result == LND_OK) {
        a = (lnd_autofree *)lnd_node_alloc(sizeof *a, &lnd_autofree_vt, LND_NODE_MIXER, input->channels, input->sample_rate_hz);
        if (!a) result = LND_ERR_OUT_OF_MEMORY;
    }
    if (a) {
        a->base.mix_mode = LND_MIX_AVAILABLE | LND_MIX_END;
        a->source = source;
        a->sound = sound;
        a->input = input;
        lnd_store(&a->end, UINT64_MAX);
    }
    if (result == LND_OK) result = lnd_node_connect(input, &a->base);
    if (result == LND_OK) result = lnd_node_connect(&a->base, output);
    if (result == LND_OK) result = LND_SoundPlay(sound);
    LND_AUTOFREE id = 0;
    if (result == LND_OK) {
        a->owns_source = true;
        a->id = id = ++lnd_autofree_serial;
        a->next = lnd_autofrees;
        lnd_autofrees = a;
        lnd_store(&a->armed, 1);
    } else {
        if (a) lnd_node_destroy(&a->base);
        if (created && input && !source->ops) lnd_node_destroy(input);
        lnd_error(result);
    }
    lnd_context_unlock();
    return id;
}

static bool lnd_autofree_ready(lnd_autofree *a) {
    lnd_edge *e = a->base.outputs;
    if (!e) return true;
    lnd_spinlock_lock(&e->dst->lock);
    uint64_t end = lnd_load(&a->end);
    bool ready = end != UINT64_MAX && e->cursor >= end &&
                 (!e->resampler || lnd_source_status(e->resampler) == LND_SOURCE_EOF || lnd_resample_source_drained(e->resampler, e->cursor - end));
    lnd_spinlock_unlock(&e->dst->lock);
    return ready;
}

void lnd_autofree_update(void) {
    lnd_autofree *a = lnd_autofrees;
    while (a) {
        lnd_autofree *next = a->next;
        if (lnd_autofree_ready(a)) lnd_node_destroy(&a->base);
        a = next;
    }
}

void lnd_autofree_free(void) {
    while (lnd_autofrees)
        lnd_node_destroy(&lnd_autofrees->base);
}

bool LND_AutofreeIsValid(LND_AUTOFREE id) {
    if (lnd_callback_active() || !lnd_context_enter()) return false;
    bool valid = lnd_autofree_find(id) != nullptr;
    lnd_context_unlock();
    return valid;
}

int32_t LND_AutofreeGetInfo(LND_AUTOFREE id, LND_AUTOFREE_INFO *info) {
    if (!info) return lnd_error(LND_ERR_INVALID_ARG);
    if (lnd_callback_active() || !lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_autofree *a = lnd_autofree_find(id);
    if (a)
        *info = (LND_AUTOFREE_INFO){.state = LND_SoundGetState(a->sound),
                                    .source_status = LND_SourceGetStatus(a->source),
                                    .position_frames = LND_SourceGetPositionFrames(a->source),
                                    .length_frames = LND_SourceGetLengthFrames(a->source),
                                    .draining = lnd_load(&a->end) != UINT64_MAX};
    lnd_context_unlock();
    return a ? LND_OK : lnd_error(LND_ERR_STATE);
}

int32_t LND_AutofreeCancel(LND_AUTOFREE id) {
    if (lnd_callback_active() || !lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_autofree *a = lnd_autofree_find(id);
    if (a) lnd_node_destroy(&a->base);
    lnd_context_unlock();
    return a ? LND_OK : lnd_error(LND_ERR_STATE);
}

#define LND_AUTOFREE_CONTROL(name, type, argument, call)                                                                                                       \
    int32_t name(LND_AUTOFREE id, type argument) {                                                                                                             \
        if (lnd_callback_active() || !lnd_context_enter()) return lnd_error(LND_ERR_BUSY);                                                                     \
        lnd_autofree *a = lnd_autofree_find(id);                                                                                                               \
        int32_t result = a && lnd_load(&a->end) == UINT64_MAX ? (call) : LND_ERR_STATE;                                                                        \
        lnd_context_unlock();                                                                                                                                  \
        return lnd_error(result);                                                                                                                              \
    }

LND_AUTOFREE_CONTROL(LND_AutofreeSetPause, bool, pause, LND_SoundSetPause(a->sound, pause))
LND_AUTOFREE_CONTROL(LND_AutofreeSetGain, float, gain, LND_SoundSetGain(a->sound, gain))
LND_AUTOFREE_CONTROL(LND_AutofreeSetLoop, bool, loop, LND_SoundSetLoop(a->sound, loop))
LND_AUTOFREE_CONTROL(LND_AutofreeSeekFrames, uint64_t, frame, LND_SoundSeekFrames(a->sound, frame))

int32_t LND_AutofreeEnd(LND_AUTOFREE id) {
    if (lnd_callback_active() || !lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_autofree *a = lnd_autofree_find(id);
    int32_t result = a ? LND_SourceEnd(a->source) : LND_ERR_STATE;
    lnd_context_unlock();
    return lnd_error(result);
}

int64_t LND_AutofreeWritePcm(LND_AUTOFREE id, const LND_PCM *pcm, size_t offset, size_t frames) {
    if (lnd_callback_active() || !lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_autofree *a = lnd_autofree_find(id);
    int64_t result = LND_ERR_STATE;
#if LND_MODULE_QUEUE
    if (a) result = LND_QueueWritePcm(a->source, pcm, offset, frames);
#else
    (void)pcm;
    (void)offset;
    (void)frames;
    if (a) result = LND_ERR_UNSUPPORTED;
#endif
    lnd_context_unlock();
    return result < 0 ? lnd_error((int32_t)result) : result;
}
