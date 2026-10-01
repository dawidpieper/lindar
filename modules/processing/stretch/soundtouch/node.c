#include "lindar_soundtouch.h"
#include "bridge.h"
#include "processing/stretch/stretch.h"

extern const lnd_stretch_backend lnd_stretch_soundtouch;
#include "src/alloc.h"
#include "src/error.h"
#include "playback/graph/context.h"
#include "playback/graph/node.h"

#include <math.h>
#include <string.h>

typedef struct lnd_soundtouch {
    void *engine;
    float *input;
    float params[LND_STRETCH_PARAM_COUNT];
    const lnd_node_vt *processor;
    uint64_t input_frames;
    uint64_t output_frames;
    uint64_t segment_frames;
    double expected;
    double segment_start;
    int32_t settings[6];
    int32_t error;
    bool ended;
    bool drained;
} lnd_soundtouch;

static const lnd_node_vt lnd_soundtouch_vt;

static bool lnd_soundtouch_node(const lnd_node *node) { return node && node->vt == &lnd_soundtouch_vt; }

static bool lnd_soundtouch_setting_valid(int32_t setting, int32_t value) {
    switch (setting) {
    case LND_SOUNDTOUCH_AA_FILTER:
    case LND_SOUNDTOUCH_QUICK_SEEK:
        return value == 0 || value == 1;
    case LND_SOUNDTOUCH_AA_FILTER_LENGTH:
        return value >= 8 && value <= 128 && value % 8 == 0;
    case LND_SOUNDTOUCH_SEQUENCE_MS:
        return value == 0 || (value >= 8 && value <= 200);
    case LND_SOUNDTOUCH_SEEKWINDOW_MS:
        return value >= 0 && value <= 100;
    case LND_SOUNDTOUCH_OVERLAP_MS:
        return value >= 1 && value <= 64;
    default:
        return false;
    }
}

static void lnd_soundtouch_param(void *user, int32_t param, float value) {
    lnd_soundtouch *s = user;
    int32_t index = param - LND_SOUNDTOUCH_PARAM_TEMPO_RATIO;
    if (s->error || s->params[index] == value) return;
    int32_t result = lnd_st_param(s->engine, index, value);
    if (result != LND_OK) {
        s->error = result;
        return;
    }
    s->segment_start = s->expected;
    s->segment_frames = 0;
    s->params[index] = value;
    if (!s->input_frames) s->error = lnd_st_clear(s->engine);
}

static void lnd_soundtouch_process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    LND_UNUSED(user);
    LND_UNUSED(pcm);
    LND_UNUSED(frames);
    LND_UNUSED(channels);
    LND_UNUSED(sample_rate_hz);
}

static void lnd_soundtouch_release(void *user) {
    lnd_soundtouch *s = user;
    if (s->engine) lnd_st_destroy(s->engine);
    lnd_free_aligned(s->input);
    lnd_free(s);
}

static const LND_PROCESSOR_PROCS lnd_soundtouch_procs = {
    .process = lnd_soundtouch_process,
    .param = lnd_soundtouch_param,
    .release = lnd_soundtouch_release,
    .flags = LND_PROCESSOR_BOUNDED,
    .validate = lnd_stretch_param_valid,
};

static uint64_t lnd_soundtouch_limit(const lnd_soundtouch *s) { return (uint64_t)(s->expected + (s->ended ? 0.5 : 0)); }

static void lnd_soundtouch_render(lnd_node *node, float *dst, uint32_t frames) {
    lnd_soundtouch *s = lnd_processor_user(node);
    uint32_t done = 0, channels = node->channels;
    bool waiting = false;
    uint64_t latency = s->error ? 0 : (uint32_t)lnd_st_get_setting(s->engine, LND_ST_INITIAL_LATENCY_FRAMES);
    uint64_t sequence = s->error ? 0 : (uint32_t)lnd_st_get_setting(s->engine, LND_ST_INPUT_SEQUENCE_FRAMES);
    uint64_t budget = LND_MAX((uint64_t)node->block * 32, latency * 4 + sequence * 2 + (uint64_t)frames * 32);
    while (done < frames && !s->error && !s->drained) {
        uint64_t limit = lnd_soundtouch_limit(s);
        uint64_t remaining = limit > s->output_frames ? limit - s->output_frames : 0;
        uint32_t want = (uint32_t)LND_MIN(frames - done, remaining);
        int64_t got = want ? lnd_st_receive(s->engine, dst + (size_t)done * channels, want) : 0;
        if (got < 0) {
            s->error = (int32_t)got;
            break;
        }
        done += (uint32_t)got;
        s->output_frames += (uint64_t)got;
        if (s->ended && s->output_frames >= limit) {
            s->error = lnd_st_clear(s->engine);
            s->drained = !s->error;
            break;
        }
        if (done == frames || waiting || !budget) break;
        uint32_t count = (uint32_t)LND_MIN(budget, node->block);
        if (s->ended) {
            memset(s->input, 0, (size_t)count * channels * sizeof(float));
            s->error = lnd_st_put(s->engine, s->input, count);
        } else {
            lnd_bus_render(node, s->input, count);
            uint32_t read = node->rendered;
            int32_t status = lnd_load(&node->status);
            if (read) {
                s->error = lnd_st_put(s->engine, s->input, read);
                if (s->error) break;
                s->input_frames += read;
                s->segment_frames += read;
                s->expected = s->segment_start + (double)s->segment_frames / ((double)s->params[LND_STRETCH_TEMPO_INDEX] * s->params[LND_STRETCH_RATE_INDEX]);
                if (!(s->expected < (double)UINT64_MAX)) s->error = LND_ERR_INVALID_ARG;
            }
            if (status < 0) s->error = status;
            if (status == LND_SOURCE_EOF)
                s->ended = true;
            else if (read < count)
                waiting = true;
        }
        budget -= count;
    }
    node->rendered = done;
    int32_t status = s->error ? s->error : s->drained ? LND_SOURCE_EOF : done == frames ? LND_SOURCE_READY : LND_SOURCE_WAITING;
    lnd_store(&node->status, status);
    lnd_store(&node->active, !s->drained && !s->error);
    if (done < frames) memset(dst + (size_t)done * channels, 0, (size_t)(frames - done) * channels * sizeof(float));
}

static void lnd_soundtouch_command(lnd_node *node, const lnd_cmd *command, bool immediate) {
    lnd_soundtouch *s = lnd_processor_user(node);
    if (command->op == LND_OP_FLUSH) {
        s->ended = true;
    } else {
        s->processor->command(node, command, immediate);
    }
}

static void lnd_soundtouch_destroy(lnd_node *node) {
    lnd_soundtouch *s = lnd_processor_user(node);
    s->processor->destroy(node);
}

static const lnd_node_vt lnd_soundtouch_vt = {
    .render = lnd_soundtouch_render,
    .command = lnd_soundtouch_command,
    .destroy = lnd_soundtouch_destroy,
};

static void *lnd_soundtouch_engine(uint32_t channels, uint32_t sample_rate_hz, const int32_t *settings, const float *params, int32_t *result) {
    void *engine = lnd_st_create(channels, sample_rate_hz, result);
    for (int32_t i = 0; i < 6 && *result == LND_OK; i++)
        *result = lnd_st_setting(engine, i, settings[i]);
    for (int32_t i = 0; i < LND_STRETCH_PARAM_COUNT && *result == LND_OK; i++)
        *result = lnd_st_param(engine, i, params[i]);
    if (*result == LND_OK) *result = lnd_st_clear(engine);
    if (*result != LND_OK) {
        if (engine) lnd_st_destroy(engine);
        return nullptr;
    }
    return engine;
}

LND_NODE *LND_NodeCreateSoundTouch(uint32_t channels, uint32_t sample_rate_hz, const LND_SOUNDTOUCH_CONFIG *config) {
    LND_SOUNDTOUCH_CONFIG c = config ? *config : (LND_SOUNDTOUCH_CONFIG){0};
    if (!c.tempo_ratio) c.tempo_ratio = 1;
    if (!c.pitch_ratio) c.pitch_ratio = 1;
    if (!c.rate_ratio) c.rate_ratio = 1;
    if (!c.overlap_ms) c.overlap_ms = 8;
    if (!c.aa_filter_length) c.aa_filter_length = 64;
    if (channels > 32 || (sample_rate_hz && (sample_rate_hz < 8000 || sample_rate_hz > 192000)) ||
        (c.flags & ~(uint32_t)(LND_SOUNDTOUCH_QUICK | LND_SOUNDTOUCH_NO_AA)) ||
        !lnd_stretch_param_valid(nullptr, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO, c.tempo_ratio) ||
        !lnd_stretch_param_valid(nullptr, LND_SOUNDTOUCH_PARAM_PITCH_RATIO, c.pitch_ratio) ||
        !lnd_stretch_param_valid(nullptr, LND_SOUNDTOUCH_PARAM_RATE_RATIO, c.rate_ratio) ||
        !lnd_soundtouch_setting_valid(LND_SOUNDTOUCH_SEQUENCE_MS, (int32_t)c.sequence_ms) ||
        !lnd_soundtouch_setting_valid(LND_SOUNDTOUCH_SEEKWINDOW_MS, (int32_t)c.seekwindow_ms) ||
        !lnd_soundtouch_setting_valid(LND_SOUNDTOUCH_OVERLAP_MS, (int32_t)c.overlap_ms) ||
        !lnd_soundtouch_setting_valid(LND_SOUNDTOUCH_AA_FILTER_LENGTH, (int32_t)c.aa_filter_length))
        return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_soundtouch *s = lnd_alloc_zero(sizeof *s);
    if (!s) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    lnd_node *node = LND_NodeCreateProcessor(&lnd_soundtouch_procs, s, channels, sample_rate_hz);
    if (!node) {
        lnd_free(s);
        return nullptr;
    }
    int32_t result = node->sample_rate_hz >= 8000 && node->sample_rate_hz <= 192000 ? LND_OK : LND_ERR_INVALID_ARG;
    s->input = lnd_alloc_aligned((size_t)node->block * node->channels * sizeof(float), LND_CACHE_LINE);
    if (!s->input) result = LND_ERR_OUT_OF_MEMORY;
    const int32_t settings[] = {!(c.flags & LND_SOUNDTOUCH_NO_AA), (int32_t)c.aa_filter_length, !!(c.flags & LND_SOUNDTOUCH_QUICK), (int32_t)c.sequence_ms,
                                (int32_t)c.seekwindow_ms,          (int32_t)c.overlap_ms};
    memcpy(s->settings, settings, sizeof settings);
    const float params[] = {c.tempo_ratio, c.pitch_ratio, c.rate_ratio};
    if (result == LND_OK) s->engine = lnd_soundtouch_engine(node->channels, node->sample_rate_hz, settings, params, &result);
    for (int32_t i = 0; i < LND_STRETCH_PARAM_COUNT && result == LND_OK; i++) {
        s->params[i] = params[i];
        lnd_processor_set_param(node, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO + i, params[i]);
    }
    if (result != LND_OK) {
        LND_NodeFree(node);
        return lnd_error_null(result);
    }
    s->processor = node->vt;
    node->vt = &lnd_soundtouch_vt;
    return node;
}

static int32_t lnd_soundtouch_post(lnd_node *node, uint32_t op) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_context_has_node(node) && lnd_soundtouch_node(node)) {
        result = lnd_node_post(node, op, 0, 0, 0);
        if (result == LND_OK && !lnd_load(&node->active)) lnd_node_drain(node, true);
    }
    lnd_context_unlock();
    return lnd_error(result);
}

static int32_t lnd_soundtouch_rebuild(lnd_node *node, int32_t setting, int32_t value, bool reset) {
    if (!reset && !lnd_soundtouch_setting_valid(setting, value)) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_context_has_node(node) || !lnd_soundtouch_node(node)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    lnd_soundtouch *s = lnd_processor_user(node);
    int32_t settings[6], result = LND_OK;
    float params[LND_STRETCH_PARAM_COUNT];
    lnd_spinlock_lock(&node->lock);
    lnd_node_process(node, false);
    if (!reset && (s->input_frames || s->ended)) result = LND_ERR_STATE;
    memcpy(settings, s->settings, sizeof settings);
    for (int32_t i = 0; i < LND_STRETCH_PARAM_COUNT; i++)
        params[i] = lnd_processor_param(node, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO + i);
    lnd_spinlock_unlock(&node->lock);
    if (result == LND_OK) {
        if (!reset) settings[setting] = value;
        void *engine = lnd_soundtouch_engine(node->channels, node->sample_rate_hz, settings, params, &result);
        if (engine) {
            lnd_spinlock_lock(&node->lock);
            bool changed = false;
            for (int32_t i = 0; i < LND_STRETCH_PARAM_COUNT; i++)
                changed |= params[i] != lnd_processor_param(node, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO + i);
            if ((!reset && (s->input_frames || s->ended)) || changed) {
                result = LND_ERR_BUSY;
            } else {
                void *old = s->engine;
                s->engine = engine;
                engine = old;
                memcpy(s->settings, settings, sizeof settings);
                memcpy(s->params, params, sizeof params);
                s->input_frames = s->output_frames = s->segment_frames = 0;
                s->expected = s->segment_start = 0;
                s->ended = s->drained = false;
                s->error = LND_OK;
                lnd_store(&node->status, LND_SOURCE_READY);
                lnd_store(&node->active, 1);
                lnd_add(&node->revision, 1);
            }
            lnd_spinlock_unlock(&node->lock);
            lnd_st_destroy(engine);
        }
    }
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_NodeResetSoundTouch(LND_NODE *node) { return lnd_soundtouch_rebuild(node, 0, 0, true); }
int32_t LND_NodeEndSoundTouchInput(LND_NODE *node) { return lnd_soundtouch_post(node, LND_OP_FLUSH); }

int32_t LND_NodeSetSoundTouchSetting(LND_NODE *node, int32_t setting, int32_t value) { return lnd_soundtouch_rebuild(node, setting, value, false); }

int32_t LND_NodeGetSoundTouchSetting(const LND_NODE *object, int32_t setting) {
    LND_NODE *node = (LND_NODE *)object;
    if (setting < 0 || setting > LND_SOUNDTOUCH_OVERLAP_MS) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_context_has_node(node) && lnd_soundtouch_node(node)) {
        lnd_soundtouch *s = lnd_processor_user(node);
        result = s->settings[setting];
    }
    lnd_context_unlock();
    return result < 0 ? lnd_error(result) : result;
}

int32_t LND_NodeGetSoundTouchInfo(const LND_NODE *object, LND_SOUNDTOUCH_INFO *info) {
    LND_NODE *node = (LND_NODE *)object;
    if (!info) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_context_has_node(node) && lnd_soundtouch_node(node)) {
        lnd_spinlock_lock(&node->lock);
        lnd_soundtouch *s = lnd_processor_user(node);
        *info = (LND_SOUNDTOUCH_INFO){.input_frames = s->input_frames,
                                      .output_frames = s->output_frames,
                                      .duration_ratio = 1 / ((double)s->params[LND_STRETCH_TEMPO_INDEX] * s->params[LND_STRETCH_RATE_INDEX]),
                                      .initial_latency_frames = s->error ? 0 : (uint32_t)lnd_st_get_setting(s->engine, LND_ST_INITIAL_LATENCY_FRAMES),
                                      .input_sequence_frames = s->error ? 0 : (uint32_t)lnd_st_get_setting(s->engine, LND_ST_INPUT_SEQUENCE_FRAMES),
                                      .output_sequence_frames = s->error ? 0 : (uint32_t)lnd_st_get_setting(s->engine, LND_ST_OUTPUT_SEQUENCE_FRAMES),
                                      .buffered_output_frames = s->error ? 0 : lnd_st_available(s->engine),
                                      .unprocessed_frames = s->error ? 0 : lnd_st_pending(s->engine),
                                      .input_ended = s->ended,
                                      .error = s->error};
        lnd_spinlock_unlock(&node->lock);
        result = LND_OK;
    }
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_NodeSetSoundTouchPitchSemitones(LND_NODE *node, float semitones) {
    if (!lnd_soundtouch_node(node) || !isfinite(semitones) || semitones < -24 || semitones > 24) return lnd_error(LND_ERR_INVALID_ARG);
    return LND_NodeSetParam(node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO, exp2f(semitones / 12));
}

float LND_NodeGetSoundTouchPitchSemitones(const LND_NODE *node) {
    return lnd_soundtouch_node(node) ? 12 * log2f(LND_NodeGetParam(node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO)) : 0;
}

int32_t LND_NodeSetSoundTouchTempoChangePercent(LND_NODE *node, float percent) {
    if (!lnd_soundtouch_node(node) || !isfinite(percent)) return lnd_error(LND_ERR_INVALID_ARG);
    return LND_NodeSetParam(node, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO, 1 + percent * 0.01f);
}

int32_t LND_NodeSetSoundTouchRateChangePercent(LND_NODE *node, float percent) {
    if (!lnd_soundtouch_node(node) || !isfinite(percent)) return lnd_error(LND_ERR_INVALID_ARG);
    return LND_NodeSetParam(node, LND_SOUNDTOUCH_PARAM_RATE_RATIO, 1 + percent * 0.01f);
}

const char *LND_SoundTouchGetVersion(void) { return lnd_st_version(); }

static LND_NODE *lnd_soundtouch_create(uint32_t channels, uint32_t rate, const LND_STRETCH_CONFIG *config) {
    LND_SOUNDTOUCH_CONFIG c = {.tempo_ratio = config->tempo_ratio, .pitch_ratio = config->pitch_ratio, .rate_ratio = config->rate_ratio};
    return LND_NodeCreateSoundTouch(channels, rate, &c);
}

static int32_t lnd_soundtouch_info(const LND_NODE *node, LND_STRETCH_INFO *info) {
    LND_SOUNDTOUCH_INFO i;
    int32_t result = LND_NodeGetSoundTouchInfo(node, &i);
    if (result == LND_OK)
        *info = (LND_STRETCH_INFO){.input_frames = i.input_frames,
                                   .output_frames = i.output_frames,
                                   .duration_ratio = i.duration_ratio,
                                   .initial_latency_frames = i.initial_latency_frames,
                                   .buffered_output_frames = i.buffered_output_frames,
                                   .unprocessed_frames = i.unprocessed_frames,
                                   .backend = &lnd_stretch_soundtouch,
                                   .error = i.error,
                                   .input_ended = i.input_ended};
    return result;
}

static int32_t lnd_stretch_priority = 100;

const lnd_stretch_backend lnd_stretch_soundtouch = {
    .name = "soundtouch",
    .priority = &lnd_stretch_priority,
    .default_priority = 100,
    .create = lnd_soundtouch_create,
    .matches = lnd_soundtouch_node,
    .info = lnd_soundtouch_info,
    .reset = LND_NodeResetSoundTouch,
    .flush = LND_NodeEndSoundTouchInput,
};
