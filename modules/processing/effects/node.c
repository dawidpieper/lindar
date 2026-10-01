#include "src/error.h"
#include "effects.h"
#include "playback/graph/context.h"

#include <float.h>

typedef struct lnd_effect_spec {
    const lnd_effect_ops *ops;
    LND_EFFECT_PARAM defaults[8];
} lnd_effect_spec;

#define P(name, value) {LND_EFFECT_PARAM_##name, value}
static const lnd_effect_spec lnd_effect_specs[LND_EFFECT_COUNT] = {
    [LND_EFFECT_ECHO] = {&lnd_effect_delay_ops, {P(DRY, 1), P(WET, 0.5), P(FEEDBACK, 0.3), P(DELAY_MS, 250), P(STEREO, 1)}},
    [LND_EFFECT_REVERB] = {&lnd_effect_reverb_ops, {P(DRY, 1), P(WET, 0.5), P(ROOM_SIZE, 0.5), P(DAMPING, 0.5), P(WIDTH, 1), P(FREEZE, 0)}},
    [LND_EFFECT_CHORUS] = {&lnd_effect_delay_ops,
                           {P(DRY, 1), P(WET, 0.5), P(FEEDBACK, 0), P(MIN_DELAY_MS, 5), P(MAX_DELAY_MS, 30), P(SWEEP_MS_PER_SECOND, 20)}},
    [LND_EFFECT_FLANGER] = {&lnd_effect_delay_ops,
                            {P(DRY, 1), P(WET, 0.35), P(FEEDBACK, 0.35), P(MIN_DELAY_MS, 0.1), P(MAX_DELAY_MS, 5), P(SWEEP_MS_PER_SECOND, 5)}},
    [LND_EFFECT_PHASER] = {&lnd_effect_modulation_ops, {P(DRY, 1), P(WET, 0.5), P(FEEDBACK, 0), P(RATE_HZ, 1), P(RANGE_OCTAVES, 4), P(FREQUENCY_HZ, 500)}},
    [LND_EFFECT_DISTORTION] = {&lnd_effect_dynamics_ops, {P(DRIVE, 1), P(DRY, 0.8), P(WET, 0.2), P(FEEDBACK, 0), P(OUTPUT_GAIN, 1)}},
    [LND_EFFECT_COMPRESSOR] = {&lnd_effect_dynamics_ops, {P(GAIN_DB, 0), P(THRESHOLD_DB, -12), P(RATIO, 4), P(ATTACK_MS, 10), P(RELEASE_MS, 100)}},
    [LND_EFFECT_AUTOWAH] = {&lnd_effect_modulation_ops, {P(DRY, 0.5), P(WET, 0.5), P(FEEDBACK, 0.2), P(RATE_HZ, 2), P(RANGE_OCTAVES, 4), P(FREQUENCY_HZ, 200)}},
    [LND_EFFECT_PEAK_EQ] = {&lnd_effect_filter_ops, {P(BANDWIDTH_OCTAVES, 1), P(Q, 0), P(FREQUENCY_HZ, 1000), P(GAIN_DB, 0)}},
    [LND_EFFECT_BIQUAD] = {&lnd_effect_filter_ops,
                           {P(FILTER_TYPE, 0), P(FREQUENCY_HZ, 1000), P(GAIN_DB, 0), P(BANDWIDTH_OCTAVES, 0), P(Q, 0.707), P(SLOPE, 1)}},
    [LND_EFFECT_DYNAMIC_GAIN] = {&lnd_effect_dynamics_ops, {P(TARGET_PEAK, 0.95), P(QUIET_THRESHOLD, 0.02), P(ADJUST_SPEED, 0.5), P(GAIN, 1), P(GAIN_DELAY_MS, 0)}},
    [LND_EFFECT_ROTATION] = {&lnd_effect_modulation_ops, {P(RATE_HZ, 0.2)}},
};
#undef P

static const struct {
    float min, max;
    bool discrete;
} lnd_effect_ranges[LND_EFFECT_PARAMS] = {
    [LND_EFFECT_PARAM_DRY - LND_PARAM_USER] = {-5, 5, false},
    [LND_EFFECT_PARAM_WET - LND_PARAM_USER] = {-5, 5, false},
    [LND_EFFECT_PARAM_FEEDBACK - LND_PARAM_USER] = {-1, 1, false},
    [LND_EFFECT_PARAM_DELAY_MS - LND_PARAM_USER] = {0.001, 3600000, false},
    [LND_EFFECT_PARAM_STEREO - LND_PARAM_USER] = {0, 1, true},
    [LND_EFFECT_PARAM_MIN_DELAY_MS - LND_PARAM_USER] = {0.001, 6000, false},
    [LND_EFFECT_PARAM_MAX_DELAY_MS - LND_PARAM_USER] = {0.001, 6000, false},
    [LND_EFFECT_PARAM_SWEEP_MS_PER_SECOND - LND_PARAM_USER] = {0, 1000, false},
    [LND_EFFECT_PARAM_RATE_HZ - LND_PARAM_USER] = {-1000, 1000, false},
    [LND_EFFECT_PARAM_ROOM_SIZE - LND_PARAM_USER] = {0, 1, false},
    [LND_EFFECT_PARAM_DAMPING - LND_PARAM_USER] = {0, 1, false},
    [LND_EFFECT_PARAM_WIDTH - LND_PARAM_USER] = {0, 1, false},
    [LND_EFFECT_PARAM_FREEZE - LND_PARAM_USER] = {0, 1, true},
    [LND_EFFECT_PARAM_DRIVE - LND_PARAM_USER] = {0, 5, false},
    [LND_EFFECT_PARAM_OUTPUT_GAIN - LND_PARAM_USER] = {0, 2, false},
    [LND_EFFECT_PARAM_GAIN_DB - LND_PARAM_USER] = {-120, 120, false},
    [LND_EFFECT_PARAM_THRESHOLD_DB - LND_PARAM_USER] = {-120, 0, false},
    [LND_EFFECT_PARAM_RATIO - LND_PARAM_USER] = {1, FLT_MAX, false},
    [LND_EFFECT_PARAM_ATTACK_MS - LND_PARAM_USER] = {0.01, 1000, false},
    [LND_EFFECT_PARAM_RELEASE_MS - LND_PARAM_USER] = {0.01, 5000, false},
    [LND_EFFECT_PARAM_RANGE_OCTAVES - LND_PARAM_USER] = {0, 10, false},
    [LND_EFFECT_PARAM_FREQUENCY_HZ - LND_PARAM_USER] = {0.001, 384000, false},
    [LND_EFFECT_PARAM_BANDWIDTH_OCTAVES - LND_PARAM_USER] = {0, 10, false},
    [LND_EFFECT_PARAM_Q - LND_PARAM_USER] = {0, 1000, false},
    [LND_EFFECT_PARAM_FILTER_TYPE - LND_PARAM_USER] = {0, 8, true},
    [LND_EFFECT_PARAM_SLOPE - LND_PARAM_USER] = {0, 1, false},
    [LND_EFFECT_PARAM_TARGET_PEAK - LND_PARAM_USER] = {0.001, 1, false},
    [LND_EFFECT_PARAM_QUIET_THRESHOLD - LND_PARAM_USER] = {0, 1, false},
    [LND_EFFECT_PARAM_ADJUST_SPEED - LND_PARAM_USER] = {0, 1, false},
    [LND_EFFECT_PARAM_GAIN - LND_PARAM_USER] = {0, 1000000, false},
    [LND_EFFECT_PARAM_GAIN_DELAY_MS - LND_PARAM_USER] = {0, 3600000, false},
    [LND_EFFECT_PARAM_BYPASS - LND_PARAM_USER] = {0, 1, true},
};

static const lnd_node_vt lnd_effect_vt;

int32_t lnd_effect_allocate(lnd_effect *s, size_t state_bytes, size_t samples) {
    size_t offset = (state_bytes + LND_CACHE_LINE - 1) & ~(size_t)(LND_CACHE_LINE - 1);
    if (samples > (SIZE_MAX - offset) / sizeof(float)) return LND_ERR_OUT_OF_MEMORY;
    s->state = lnd_alloc_aligned(offset + samples * sizeof(float), LND_CACHE_LINE);
    if (!s->state) return LND_ERR_OUT_OF_MEMORY;
    memset(s->state, 0, offset + samples * sizeof(float));
    s->memory = (float *)((unsigned char *)s->state + offset);
    s->memory_count = samples;
    return LND_OK;
}

uint64_t lnd_effect_decay(const lnd_effect *s, double delay_frames, double feedback) {
    feedback = fabs(feedback);
    if (feedback >= 1) return s->tail_limit;
    double repeats = feedback > 0 ? 1 + ceil(log(1e-6) / log(feedback)) : 1;
    return (uint64_t)LND_MIN(ceil(delay_frames * repeats), (double)s->tail_limit);
}

static bool lnd_effect_validate(void *user, int32_t param, float value) {
    const lnd_effect *s = user;
    if (param < LND_PARAM_USER || param >= LND_EFFECT_PARAM_END || !isfinite(value)) return false;
    int32_t index = param - LND_PARAM_USER;
    bool supported = param == LND_EFFECT_PARAM_BYPASS;
    const LND_EFFECT_PARAM *defaults = lnd_effect_specs[s->type].defaults;
    for (size_t i = 0; i < LND_COUNTOF(lnd_effect_specs[0].defaults); i++)
        supported |= defaults[i].id == param;
    if (!supported || value < lnd_effect_ranges[index].min || value > lnd_effect_ranges[index].max ||
        (lnd_effect_ranges[index].discrete && value != truncf(value)))
        return false;
    if (param == LND_EFFECT_PARAM_DELAY_MS || param == LND_EFFECT_PARAM_MIN_DELAY_MS || param == LND_EFFECT_PARAM_MAX_DELAY_MS) return value <= s->max_delay_ms;
    if (param == LND_EFFECT_PARAM_DRY || param == LND_EFFECT_PARAM_WET) {
        if (s->type == LND_EFFECT_REVERB) return value >= 0 && value <= (param == LND_EFFECT_PARAM_DRY ? 1 : 3);
        return fabsf(value) <= (s->type == LND_EFFECT_DISTORTION ? 5 : 2);
    }
    if (param == LND_EFFECT_PARAM_RATE_HZ && s->type != LND_EFFECT_ROTATION) return value >= 0 && value <= 10;
    return true;
}

static bool lnd_effect_can_slide(void *user, int32_t param) {
    LND_UNUSED(user);
    return param >= LND_PARAM_USER && param < LND_EFFECT_PARAM_END && !lnd_effect_ranges[param - LND_PARAM_USER].discrete;
}

static void lnd_effect_clear(lnd_effect *s) {
    if (s->memory_count) memset(s->memory, 0, s->memory_count * sizeof(float));
    s->ops->reset(s);
    s->ops->update(s, -1);
    s->signal = false;
    s->tail_remaining = 0;
}

static void lnd_effect_param(void *user, int32_t param, float value) {
    lnd_effect *s = user;
    if (s->p[param - LND_PARAM_USER] == value) return;
    s->p[param - LND_PARAM_USER] = value;
    if (param == LND_EFFECT_PARAM_BYPASS)
        lnd_effect_clear(s);
    else
        s->ops->update(s, param);
}

static void lnd_effect_noop(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t rate) {
    LND_UNUSED(user);
    LND_UNUSED(pcm);
    LND_UNUSED(frames);
    LND_UNUSED(channels);
    LND_UNUSED(rate);
}

static void lnd_effect_release(void *user) {
    lnd_effect *s = user;
    lnd_free_aligned(s->state);
    lnd_free(s);
}

static const LND_PROCESSOR_PROCS lnd_effect_procs = {
    .process = lnd_effect_noop,
    .param = lnd_effect_param,
    .release = lnd_effect_release,
    .flags = LND_PROCESSOR_BOUNDED,
    .validate = lnd_effect_validate,
    .can_slide = lnd_effect_can_slide,
};

static void lnd_effect_render(lnd_node *node, float *pcm, uint32_t frames) {
    lnd_effect *s = lnd_processor_user(node);
    uint32_t count = 0;
    int32_t status = LND_SOURCE_EOF;
    if (!s->ended && !s->error) {
        lnd_bus_render(node, pcm, frames);
        count = node->rendered;
        status = lnd_load(&node->status);
        if (s->selected_count == s->channels) {
            bool finite = true, signal = s->signal;
            for (size_t i = 0; i < (size_t)count * s->channels; i++) {
                finite &= isfinite(pcm[i]);
                signal |= pcm[i] != 0;
            }
            if (!finite) s->error = LND_ERR_FORMAT;
            s->signal = signal;
        } else {
            for (uint32_t f = 0; f < count && !s->error; f++)
                for (uint32_t i = 0; i < s->selected_count; i++) {
                    float x = pcm[(size_t)f * s->channels + s->selected[i]];
                    if (!isfinite(x)) s->error = LND_ERR_FORMAT;
                    if (x != 0) s->signal = true;
                }
        }
        s->input_frames += count;
        if (!s->error && !FX(s, BYPASS) && s->selected_count) s->ops->process(s, pcm, count);
        if (status < 0) s->error = status;
        if (status == LND_SOURCE_EOF) {
            s->ended = true;
            if (!s->no_tail && !FX(s, BYPASS) && s->signal && s->selected_count && s->ops->tail) s->tail_remaining = LND_MIN(s->tail_limit, s->ops->tail(s));
        }
    } else {
        memset(pcm, 0, (size_t)frames * s->channels * sizeof(float));
    }
    if (s->ended && !s->error && count < frames && s->tail_remaining) {
        uint32_t tail = (uint32_t)LND_MIN(frames - count, s->tail_remaining);
        s->ops->process(s, pcm + (size_t)count * s->channels, tail);
        s->tail_remaining -= tail;
        count += tail;
    }
    if (!s->error && s->selected_count == s->channels) {
        bool finite = true;
        for (size_t i = 0; i < (size_t)count * s->channels; i++) finite &= isfinite(pcm[i]);
        if (!finite) s->error = LND_ERR_EXTERNAL;
    } else {
        for (uint32_t i = 0; i < s->selected_count && !s->error; i++)
            for (uint32_t f = 0; f < count; f++)
                if (!isfinite(pcm[(size_t)f * s->channels + s->selected[i]])) {
                    s->error = LND_ERR_EXTERNAL;
                    break;
                }
    }
    if (s->error) count = 0;
    s->output_frames += count;
    bool ended = s->ended && !s->tail_remaining;
    node->rendered = count;
    lnd_store(&node->status, s->error ? s->error : ended ? LND_SOURCE_EOF : count == frames ? LND_SOURCE_READY : LND_SOURCE_WAITING);
    lnd_store(&node->active, !s->error && !ended);
    if (count < frames) memset(pcm + (size_t)count * s->channels, 0, (size_t)(frames - count) * s->channels * sizeof(float));
}

static void lnd_effect_command(lnd_node *node, const lnd_cmd *command, bool immediate) {
    lnd_effect *s = lnd_processor_user(node);
    s->processor->command(node, command, immediate);
}

static void lnd_effect_destroy(lnd_node *node) {
    lnd_effect *s = lnd_processor_user(node);
    s->processor->destroy(node);
}

static const lnd_node_vt lnd_effect_vt = {
    .render = lnd_effect_render,
    .command = lnd_effect_command,
    .destroy = lnd_effect_destroy,
};

static void lnd_effect_channels(lnd_effect *s, uint32_t mask) {
    s->mask = mask;
    s->selected_count = 0;
    for (uint32_t i = 0; i < s->channels; i++)
        if (mask & (UINT32_C(1) << i)) s->selected[s->selected_count++] = (uint8_t)i;
}

LND_NODE *LND_NodeCreateEffect(uint32_t channels, uint32_t sample_rate_hz, int32_t type, const LND_EFFECT_CONFIG *config) {
    LND_EFFECT_CONFIG c = config ? *config : (LND_EFFECT_CONFIG){0};
    if (type < 0 || type >= LND_EFFECT_COUNT || channels > LND_MAX_CHANNELS || (sample_rate_hz && (sample_rate_hz < 8000 || sample_rate_hz > 384000)) ||
        c.flags & ~(LND_EFFECT_SELECT_CHANNELS | LND_EFFECT_NO_TAIL) || c.param_count > LND_EFFECT_PARAMS || (c.param_count && !c.params) ||
        !isfinite(c.max_delay_ms) || c.max_delay_ms < 0 || c.max_delay_ms > 3600000 || !isfinite(c.tail_ms) || c.tail_ms < 0 || c.tail_ms > 3600000)
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_STATE);
    }
    lnd_effect *s = lnd_alloc_zero(sizeof *s);
    if (!s) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    s->type = type;
    s->ops = lnd_effect_specs[type].ops;
    s->no_tail = !!(c.flags & LND_EFFECT_NO_TAIL);
    s->max_delay_ms = c.max_delay_ms ? c.max_delay_ms : type == LND_EFFECT_ECHO ? 1000 : 100;
    const LND_EFFECT_PARAM *defaults = lnd_effect_specs[type].defaults;
    for (size_t i = 0; i < LND_COUNTOF(lnd_effect_specs[0].defaults); i++)
        if (defaults[i].id) s->p[defaults[i].id - LND_PARAM_USER] = defaults[i].value;
    int32_t result = LND_OK;
    uint64_t seen = 0;
    for (uint32_t i = 0; i < c.param_count; i++) {
        int32_t id = c.params[i].id;
        if (!lnd_effect_validate(s, id, c.params[i].value) || (seen & (UINT64_C(1) << (id - LND_PARAM_USER)))) {
            result = LND_ERR_INVALID_ARG;
            break;
        }
        seen |= UINT64_C(1) << (id - LND_PARAM_USER);
        s->p[id - LND_PARAM_USER] = c.params[i].value;
    }
    for (size_t i = 0; i < LND_COUNTOF(lnd_effect_specs[0].defaults) && result == LND_OK; i++)
        if (defaults[i].id && !lnd_effect_validate(s, defaults[i].id, s->p[defaults[i].id - LND_PARAM_USER])) result = LND_ERR_INVALID_ARG;
    lnd_node *node = result == LND_OK ? LND_NodeCreateProcessor(&lnd_effect_procs, s, channels, sample_rate_hz) : nullptr;
    if (!node) {
        lnd_free(s);
        lnd_context_unlock();
        return result == LND_OK ? nullptr : lnd_error_null(result);
    }
    s->channels = node->channels;
    s->rate = node->sample_rate_hz;
    uint32_t all = s->channels ? UINT32_MAX >> (32 - s->channels) : 0;
    uint32_t mask = c.flags & LND_EFFECT_SELECT_CHANNELS ? c.channel_mask : all;
    if (!s->channels || s->rate < 8000 || s->rate > 384000 || mask & ~all) result = LND_ERR_INVALID_ARG;
    if (result == LND_OK) {
        lnd_effect_channels(s, mask);
        s->tail_limit = (uint64_t)ceil((c.tail_ms ? (double)c.tail_ms : 10000) * s->rate / 1000);
        result = s->ops->init(s);
    }
    if (result != LND_OK) {
        LND_NodeFree(node);
        lnd_context_unlock();
        return lnd_error_null(result);
    }
    lnd_effect_clear(s);
    for (int32_t i = 0; i < LND_EFFECT_PARAMS; i++)
        lnd_processor_set_param(node, LND_PARAM_USER + i, s->p[i]);
    s->processor = node->vt;
    node->vt = &lnd_effect_vt;
    lnd_context_unlock();
    return node;
}

static bool lnd_effect_node(const lnd_node *node) { return lnd_context_has_node(node) && node->vt == &lnd_effect_vt; }

int32_t LND_NodeResetEffect(LND_NODE *node) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_effect_node(node)) {
        lnd_spinlock_lock(&node->lock);
        lnd_node_process(node, false);
        lnd_effect *s = lnd_processor_user(node);
        lnd_effect_clear(s);
        s->ended = false;
        s->error = LND_OK;
        s->input_frames = s->output_frames = 0;
        lnd_store(&node->status, LND_SOURCE_READY);
        lnd_store(&node->active, 1);
        lnd_add(&node->revision, 1);
        lnd_spinlock_unlock(&node->lock);
        result = LND_OK;
    }
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_NodeSetEffectChannelMask(LND_NODE *node, uint32_t channel_mask) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_effect_node(node) && !(channel_mask & ~(UINT32_MAX >> (32 - node->channels)))) {
        lnd_spinlock_lock(&node->lock);
        lnd_node_process(node, false);
        lnd_effect *s = lnd_processor_user(node);
        if (s->mask != channel_mask) {
            lnd_effect_channels(s, channel_mask);
            lnd_effect_clear(s);
        }
        lnd_spinlock_unlock(&node->lock);
        result = LND_OK;
    }
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_NodeGetEffectInfo(const LND_NODE *object, LND_EFFECT_INFO *info) {
    LND_NODE *node = (LND_NODE *)object;
    if (!info) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_effect_node(node)) {
        lnd_spinlock_lock(&node->lock);
        lnd_effect *s = lnd_processor_user(node);
        *info = (LND_EFFECT_INFO){.input_frames = s->input_frames,
                                  .output_frames = s->output_frames,
                                  .tail_remaining_frames = s->tail_remaining,
                                  .tail_limit_frames = s->tail_limit,
                                  .channel_mask = s->mask,
                                  .type = s->type,
                                  .error = s->error,
                                  .input_ended = s->ended,
                                  .bypassed = FX(s, BYPASS) != 0};
        lnd_spinlock_unlock(&node->lock);
        result = LND_OK;
    }
    lnd_context_unlock();
    return lnd_error(result);
}

LND_NODE *LND_NodeCreateEcho(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_ECHO, config);
}

LND_NODE *LND_NodeCreateReverb(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_REVERB, config);
}

LND_NODE *LND_NodeCreateChorus(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_CHORUS, config);
}

LND_NODE *LND_NodeCreateFlanger(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_FLANGER, config);
}

LND_NODE *LND_NodeCreatePhaser(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_PHASER, config);
}

LND_NODE *LND_NodeCreateDistortion(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_DISTORTION, config);
}

LND_NODE *LND_NodeCreateCompressor(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_COMPRESSOR, config);
}

LND_NODE *LND_NodeCreateAutoWah(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_AUTOWAH, config);
}

LND_NODE *LND_NodeCreatePeakEqualizer(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_PEAK_EQ, config);
}

LND_NODE *LND_NodeCreateBiquadEffect(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_BIQUAD, config);
}

LND_NODE *LND_NodeCreateDynamicGain(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_DYNAMIC_GAIN, config);
}

LND_NODE *LND_NodeCreateRotation(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config) {
    return LND_NodeCreateEffect(channels, sample_rate_hz, LND_EFFECT_ROTATION, config);
}
