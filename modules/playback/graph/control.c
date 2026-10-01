#include "context.h"
#include "node.h"
#include "sound.h"
#include "split.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/error.h"
#if LND_MODULE_DEVICES
#include "io/devices/engine.h"
#include "io/devices/context.h"
#endif

#include <math.h>
#include <string.h>

static bool lnd_node_valid(const lnd_node *n) { return n && lnd_context_has_node(n); }

int32_t lnd_node_status_locked(lnd_node *node) {
    if (node->type == LND_NODE_SPLIT) {
        lnd_split_node *branch = (lnd_split_node *)node;
        return lnd_node_status(branch->splitter, &branch->cursor);
    }
    return lnd_node_status(node, nullptr);
}

int32_t LND_NodeGetStatus(const LND_NODE *object) {
    LND_NODE *n = (LND_NODE *)object;
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t status = LND_ERR_INVALID_ARG;
    if (lnd_node_valid(n)) {
        lnd_spinlock_lock(&n->lock);
        status = lnd_node_status_locked(n);
        lnd_spinlock_unlock(&n->lock);
    }
    lnd_context_unlock();
    return status;
}

int32_t LND_NodeSetMixerClock(LND_NODE *n, LND_NODE *input) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_node_valid(n) && n->type == LND_NODE_MIXER && (!input || lnd_node_valid(input))) {
        lnd_spinlock_lock(&n->lock);
        lnd_edge *found = nullptr;
        for (lnd_edge *e = n->inputs; e; e = e->next_in)
            if (e->src == input) found = e;
        if (!input || found) {
            n->clock = found;
            result = LND_OK;
        }
        lnd_spinlock_unlock(&n->lock);
    }
    lnd_context_unlock();
    return lnd_error(result);
}

LND_NODE *LND_NodeGetMixerClock(const LND_NODE *object) {
    LND_NODE *n = (LND_NODE *)object;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    LND_NODE *input = lnd_node_valid(n) && n->type == LND_NODE_MIXER && n->clock ? n->clock->src : nullptr;
    lnd_context_unlock();
    return input;
}

int32_t LND_NodeSetInputMatrix(LND_NODE *n, LND_NODE *input, const float *matrix) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_node_valid(n) && lnd_node_valid(input) && n->type != LND_NODE_CHANNEL_MERGER) {
        lnd_edge *found = nullptr;
        for (lnd_edge *e = n->inputs; e; e = e->next_in)
            if (e->src == input) found = e;
        if (found) {
            size_t count = (size_t)n->channels * input->channels;
            result = LND_OK;
            for (size_t i = 0; matrix && i < count; i++)
                if (!isfinite(matrix[i]) || fabsf(matrix[i]) > 16.0f) result = LND_ERR_INVALID_ARG;
            if (result == LND_OK && matrix) result = lnd_node_reserve_input(n, input->channels, true);
            float *copy = result == LND_OK && matrix ? lnd_alloc(count * sizeof(float)) : nullptr;
            if (result == LND_OK && matrix && !copy) result = LND_ERR_OUT_OF_MEMORY;
            if (result == LND_OK) {
                if (copy) memcpy(copy, matrix, count * sizeof(float));
                lnd_spinlock_lock(&n->lock);
                float *old = found->matrix;
                found->matrix = copy;
                lnd_spinlock_unlock(&n->lock);
                lnd_free(old);
            }
        }
    }
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_NodeSetGainRampFrames(LND_NODE *n, uint32_t frames) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_node_valid(n)) {
        lnd_spinlock_lock(&n->lock);
        n->ramp = frames;
        lnd_spinlock_unlock(&n->lock);
        result = LND_OK;
    }
    lnd_context_unlock();
    return lnd_error(result);
}

enum { LND_INPUT_PAUSE_QUERY = -1 };

static int32_t lnd_input_pause(LND_NODE *n, const LND_NODE *input, int32_t pause) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_node_valid(n) && lnd_node_valid(input)) {
        lnd_spinlock_lock(&n->lock);
        for (lnd_edge *e = n->inputs; e; e = e->next_in) {
            if (e->src != input) continue;
            if (pause >= 0) e->paused = pause != 0;
            result = pause < 0 ? e->paused : LND_OK;
            break;
        }
        lnd_spinlock_unlock(&n->lock);
        if (!pause && result == LND_OK) lnd_node_activate(n);
    }
    lnd_context_unlock();
    return result < 0 ? lnd_error(result) : result;
}

int32_t LND_NodeSetInputPause(LND_NODE *n, LND_NODE *input, bool pause) { return lnd_input_pause(n, input, pause); }
int32_t LND_NodeGetInputPause(const LND_NODE *object, const LND_NODE *input) { return lnd_input_pause((LND_NODE *)object, input, LND_INPUT_PAUSE_QUERY); }

int32_t LND_NodeResetSplit(LND_NODE *n) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_node_valid(n) && n->type == LND_NODE_SPLIT) {
        lnd_split_node *b = (lnd_split_node *)n;
        lnd_spinlock_lock(&n->lock);
        b->cursor = b->splitter ? lnd_load(&b->splitter->produced) : 0;
        b->armed = true;
        lnd_store(&b->pos, 0);
        lnd_store(&b->dropped, 0);
        lnd_store(&n->status, LND_SOURCE_READY);
        lnd_spinlock_unlock(&n->lock);
        result = LND_OK;
    }
    lnd_context_unlock();
    return lnd_error(result);
}

uint64_t LND_NodeGetSplitDroppedFrames(const LND_NODE *n) { return n && n->type == LND_NODE_SPLIT ? lnd_load(&((const lnd_split_node *)n)->dropped) : 0; }

LND_NODE *LND_NodeCreateBus(uint32_t channels, uint32_t sample_rate_hz) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (channels > LND_MAX_CHANNELS) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_STATE);
    }
    if (lnd_cfg_u32(LND_CFG_RUN_MODE) != LND_MODE_REALTIME) {
        if (!channels) channels = lnd_cfg_u32(LND_CFG_GRAPH_CHANNELS) ? lnd_cfg_u32(LND_CFG_GRAPH_CHANNELS) : 2;
        if (!sample_rate_hz) sample_rate_hz = lnd_cfg_u32(LND_CFG_GRAPH_SAMPLE_RATE_HZ) ? lnd_cfg_u32(LND_CFG_GRAPH_SAMPLE_RATE_HZ) : 48000;
    }
#if LND_MODULE_DEVICES
    if (!channels || !sample_rate_hz) {
        lnd_instance *i = lnd_context_default_output();
        if (!i) {
            lnd_context_unlock();
            return nullptr;
        }
        if (!channels) channels = i->cfg.channels;
        if (!sample_rate_hz) sample_rate_hz = i->cfg.sample_rate_hz;
    }
#endif
    lnd_node *n = lnd_node_create_bus(channels, sample_rate_hz, nullptr);
    lnd_context_unlock();
    return n ? n : lnd_error_null(LND_ERR_OUT_OF_MEMORY);
}

static lnd_node *lnd_node_create_kind(uint32_t channels, uint32_t sample_rate_hz, int32_t kind, uint32_t arg, const void *ptr, void *user) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (channels > LND_MAX_CHANNELS) return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_context_lock();
    lnd_context_gc();
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_STATE);
    }
    if (lnd_cfg_u32(LND_CFG_RUN_MODE) != LND_MODE_REALTIME) {
        if (!channels) channels = lnd_cfg_u32(LND_CFG_GRAPH_CHANNELS) ? lnd_cfg_u32(LND_CFG_GRAPH_CHANNELS) : 2;
        if (!sample_rate_hz) sample_rate_hz = lnd_cfg_u32(LND_CFG_GRAPH_SAMPLE_RATE_HZ) ? lnd_cfg_u32(LND_CFG_GRAPH_SAMPLE_RATE_HZ) : 48000;
    }
#if LND_MODULE_DEVICES
    if (!channels || !sample_rate_hz) {
        lnd_instance *i = lnd_context_default_output();
        if (!i) {
            lnd_context_unlock();
            return nullptr;
        }
        if (!channels) channels = i->cfg.channels;
        if (!sample_rate_hz) sample_rate_hz = i->cfg.sample_rate_hz;
    }
#endif
    lnd_node *n;
    switch (kind) {
    case LND_NODE_SPLITTER:
        n = lnd_node_create_splitter(channels, sample_rate_hz, arg);
        break;
    case LND_NODE_MIXER:
        n = lnd_node_create_mixer(channels, sample_rate_hz, (int32_t)arg);
        break;
    case LND_NODE_CHANNEL_SPLITTER:
        n = lnd_node_create_channel_splitter(channels, sample_rate_hz);
        break;
    case LND_NODE_CHANNEL_MERGER:
        n = lnd_node_create_merger(channels, sample_rate_hz);
        break;
    case LND_NODE_PROCESSOR:
        n = lnd_node_create_processor(ptr, user, channels, sample_rate_hz);
        break;
    default:
        n = lnd_node_create_bus(channels, sample_rate_hz, nullptr);
        break;
    }
    lnd_context_unlock();
    return n ? n : lnd_error_null(LND_ERR_OUT_OF_MEMORY);
}

LND_NODE *LND_NodeCreateSplitter(uint32_t channels, uint32_t sample_rate_hz, uint32_t outputs) {
    if (outputs < 1 || outputs > 64) return lnd_error_null(LND_ERR_INVALID_ARG);
    return lnd_node_create_kind(channels, sample_rate_hz, LND_NODE_SPLITTER, outputs, nullptr, nullptr);
}

LND_NODE *LND_NodeGetSplitterOutput(const LND_NODE *object, uint32_t index) {
    LND_NODE *splitter = (LND_NODE *)object;
    lnd_node *b = lnd_splitter_output(splitter, index);
    return b ? b : lnd_error_null(LND_ERR_INVALID_ARG);
}

uint32_t LND_NodeGetSplitterOutputCount(const LND_NODE *splitter) { return lnd_splitter_outputs(splitter); }

LND_NODE *LND_NodeCreateMixer(uint32_t channels, uint32_t sample_rate_hz, uint32_t flags) {
    if ((flags & ~(uint32_t)(LND_MIX_MODE_MASK | LND_MIX_END)) || (flags & LND_MIX_MODE_MASK) > LND_MIX_AVAILABLE) return lnd_error_null(LND_ERR_INVALID_ARG);
    return lnd_node_create_kind(channels, sample_rate_hz, LND_NODE_MIXER, flags, nullptr, nullptr);
}

LND_NODE *LND_NodeCreateChannelSplitter(uint32_t channels, uint32_t sample_rate_hz) {
    return lnd_node_create_kind(channels, sample_rate_hz, LND_NODE_CHANNEL_SPLITTER, 0, nullptr, nullptr);
}

LND_NODE *LND_NodeCreateChannelMerger(uint32_t channels, uint32_t sample_rate_hz) {
    return lnd_node_create_kind(channels, sample_rate_hz, LND_NODE_CHANNEL_MERGER, 0, nullptr, nullptr);
}

int32_t LND_NodeSetChannelMergerInput(LND_NODE *merger, uint32_t channel, LND_NODE *src) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_context_gc();
    if (!lnd_node_valid(merger) || !lnd_node_valid(src) || merger->type != LND_NODE_CHANNEL_MERGER || channel >= merger->channels ||
        lnd_node_is_terminal(src)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    int32_t r = lnd_node_connect(src, merger);
    if (r == LND_OK) r = lnd_merger_set_channel(merger, src, channel);
    lnd_context_unlock();
    return lnd_error(r);
}

LND_NODE *LND_NodeCreateProcessor(const LND_PROCESSOR_PROCS *procs, void *user, uint32_t channels, uint32_t sample_rate_hz) {
    if (!procs || (!!procs->process == !!procs->process_pcm) || (procs->flags & ~(uint32_t)LND_PROCESSOR_BOUNDED) ||
        (procs->process_layouts & ~LND_LAYOUT_MASK_ALL) || procs->process_format < LND_FORMAT_NONE || procs->process_format > LND_FORMAT_F64 ||
        (!procs->process_pcm && (procs->process_format || procs->process_layouts)))
        return lnd_error_null(LND_ERR_INVALID_ARG);
    return lnd_node_create_kind(channels, sample_rate_hz, LND_NODE_PROCESSOR, 0, procs, user);
}

LND_NODE *LND_NodeCreateProcessorProc(LND_PROCESS_PROC process, void *user, uint32_t channels, uint32_t sample_rate_hz, uint32_t flags) {
    LND_PROCESSOR_PROCS procs = {.process = process, .flags = flags};
    return LND_NodeCreateProcessor(&procs, user, channels, sample_rate_hz);
}

void *LND_NodeGetProcessorUser(const LND_NODE *n) { return n && n->type == LND_NODE_PROCESSOR ? lnd_processor_user(n) : nullptr; }

const LND_PROCESSOR_PROCS *LND_NodeGetProcessorProcs(const LND_NODE *n) { return n && n->type == LND_NODE_PROCESSOR ? lnd_processor_procs(n) : nullptr; }

uint32_t LND_NodeGetMixMode(const LND_NODE *n) { return n ? (uint32_t)n->mix_mode : 0; }

int32_t LND_NodeFree(LND_NODE *n) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_context_gc();
    if (!lnd_node_valid(n) || n->instance || n->type == LND_NODE_SOURCE || lnd_node_is_branch(n)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    if (lnd_sound_busy(n)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_BUSY);
    }
    if (lnd_node_is_splitter(n)) {
        uint32_t count = lnd_splitter_outputs(n);
        for (uint32_t i = 0; i < count; i++) {
            if (lnd_sound_busy(lnd_splitter_output(n, i))) {
                lnd_context_unlock();
                return lnd_error(LND_ERR_BUSY);
            }
        }
        for (uint32_t i = 0; i < count; i++) {
            lnd_node *b = lnd_splitter_output(n, i);
            if (b) lnd_node_destroy(b);
        }
    }
    lnd_node_destroy(n);
    lnd_context_unlock();
    return LND_OK;
}

int32_t LND_NodeConnect(LND_NODE *src, LND_NODE *dst) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_context_gc();
    if (!lnd_node_valid(src) || !lnd_node_valid(dst) || lnd_node_is_branch(dst) || lnd_node_is_terminal(src)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    int32_t r = lnd_node_connect(src, dst);
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t LND_NodeDisconnect(LND_NODE *src, LND_NODE *dst) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_context_gc();
    if (!lnd_node_valid(src) || (dst && !lnd_node_valid(dst))) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    int32_t r = lnd_node_disconnect(src, dst);
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t LND_NodeGetType(const LND_NODE *n) { return n ? n->type : LND_NODE_BUS; }

uint32_t LND_NodeGetChannels(const LND_NODE *n) { return n ? n->channels : 0; }

uint32_t LND_NodeGetSampleRateHz(const LND_NODE *n) { return n ? n->sample_rate_hz : 0; }

uint32_t LND_NodeGetInputCount(const LND_NODE *n) { return n ? n->inputs_count : 0; }

uint32_t LND_NodeGetOutputCount(const LND_NODE *n) { return n ? n->outputs_count : 0; }

LND_NODE *LND_NodeGetInput(const LND_NODE *n, uint32_t index) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!n) return nullptr;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_node *r = nullptr;
    for (lnd_edge *e = n->inputs; e; e = e->next_in, index--) {
        if (index == 0) {
            r = e->src;
            break;
        }
    }
    lnd_context_unlock();
    return r;
}

LND_NODE *LND_NodeGetOutput(const LND_NODE *n, uint32_t index) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!n) return nullptr;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_node *r = nullptr;
    for (lnd_edge *e = n->outputs; e; e = e->next_out, index--) {
        if (index == 0) {
            r = e->dst;
            break;
        }
    }
    lnd_context_unlock();
    return r;
}

static bool lnd_param_user(const lnd_node *n, int32_t param) {
    return n->type == LND_NODE_PROCESSOR && param >= LND_PARAM_USER && param < LND_PARAM_USER + LND_PARAM_USER_COUNT;
}

int32_t LND_NodeSetParam(LND_NODE *n, int32_t param, float value) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!n || !isfinite(value)) return lnd_error(LND_ERR_INVALID_ARG);
    bool user = lnd_param_user(n, param);
    if (!user && (param != LND_PARAM_GAIN || !(value >= 0.0f))) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_context_gc();
    if (!lnd_node_valid(n) || (user && !lnd_processor_validate(n, param, value))) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    int32_t r = user ? lnd_node_post(n, LND_OP_PARAM, 0, (uint32_t)(param - LND_PARAM_USER), value) : lnd_node_post(n, LND_OP_GAIN, 0, 0, value);
    if (r == LND_OK) {
        if (user) lnd_processor_set_param(n, param, value);
        if (!lnd_load(&n->active)) lnd_node_drain(n, true);
    }
    lnd_context_unlock();
    return lnd_error(r);
}

float LND_NodeGetParam(const LND_NODE *n, int32_t param) {
    if (!n) return 0.0f;
    if (param == LND_PARAM_GAIN) return n->gain_param;
    return lnd_param_user(n, param) ? lnd_processor_param(n, param) : 0.0f;
}

int32_t LND_NodeSetGain(LND_NODE *n, float gain) { return LND_NodeSetParam(n, LND_PARAM_GAIN, gain); }

float LND_NodeGetGain(const LND_NODE *n) { return LND_NodeGetParam(n, LND_PARAM_GAIN); }

int32_t LND_NodeSetClipMode(LND_NODE *n, int32_t mode) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!n || mode < LND_CLIP_NONE || mode > LND_CLIP_SOFT) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t r = lnd_node_post(n, LND_OP_CLIP, 0, (uint32_t)mode, 0.0f);
    if (r == LND_OK && !lnd_load(&n->active)) lnd_node_drain(n, true);
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t LND_NodeGetClipMode(const LND_NODE *n) { return n ? n->clip : LND_CLIP_NONE; }

uint64_t LND_NodeGetPositionFrames(const LND_NODE *n) { return n ? n->produced : 0; }

bool LND_NodeIsActive(const LND_NODE *n) { return n ? lnd_load(&n->active) != 0 : false; }
