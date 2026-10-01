#include "playback/graph/node.h"
#include "bridge.h"
#include "src/alloc.h"
#include "src/error.h"
#include "playback/graph/context.h"
#include "lindar_bungee.h"
#include "processing/stretch/stretch.h"

extern const lnd_stretch_backend lnd_stretch_bungee;

#include <math.h>
#include <string.h>

typedef struct lnd_bungee {
    void *engine;
    float *memory;
    float *input;
    float *ring;
    float *grain;
    float params[LND_STRETCH_PARAM_COUNT];
    const lnd_node_vt *processor;
    lnd_bg_request request;
    lnd_bg_chunk output;
    uint64_t input_frames;
    uint64_t output_frames;
    uint64_t segment_frames;
    double expected;
    double segment_start;
    int64_t grain_begin;
    int64_t grain_end;
    uint32_t ring_capacity;
    uint32_t grain_capacity;
    uint32_t hop;
    uint32_t consumed;
    int32_t grain_adjust;
    int32_t mode;
    int32_t error;
    bool started;
    bool specified;
    bool ended;
    bool drained;
} lnd_bungee;

static const lnd_node_vt lnd_bungee_vt;

static bool lnd_bungee_node(const lnd_node *node) { return node && node->vt == &lnd_bungee_vt; }

static void lnd_bungee_param(void *user, int32_t param, float value) {
    lnd_bungee *s = user;
    int32_t index = param - LND_BUNGEE_PARAM_TEMPO_RATIO;
    if (s->params[index] == value) return;
    s->segment_start = s->expected;
    s->segment_frames = 0;
    s->params[index] = value;
}

static void lnd_bungee_process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t rate) {
    LND_UNUSED(user);
    LND_UNUSED(pcm);
    LND_UNUSED(frames);
    LND_UNUSED(channels);
    LND_UNUSED(rate);
}

static void lnd_bungee_release(void *user) {
    lnd_bungee *s = user;
    if (s->engine) lnd_bg_destroy(s->engine);
    lnd_free_aligned(s->memory);
    lnd_free(s);
}

static const LND_PROCESSOR_PROCS lnd_bungee_procs = {
    .process = lnd_bungee_process,
    .param = lnd_bungee_param,
    .release = lnd_bungee_release,
    .flags = LND_PROCESSOR_BOUNDED,
    .validate = lnd_stretch_param_valid,
};

static uint64_t lnd_bungee_limit(const lnd_bungee *s) { return (uint64_t)(s->expected + (s->ended ? 0.5 : 0)); }

static bool lnd_bungee_read(lnd_node *node, lnd_bungee *s, uint32_t count) {
    lnd_bus_render(node, s->input, count);
    uint32_t read = node->rendered, channels = node->channels;
    int32_t status = lnd_load(&node->status);
    if (read) {
        if (s->input_frames > (UINT64_C(1) << 52) - read) {
            s->error = LND_ERR_STATE;
            return false;
        }
        for (size_t i = 0; i < (size_t)read * channels; i++)
            if (!isfinite(s->input[i])) {
                s->error = LND_ERR_FORMAT;
                return false;
            }
        uint32_t at = (uint32_t)s->input_frames & (s->ring_capacity - 1);
        uint32_t first = LND_MIN(read, s->ring_capacity - at);
        memcpy(s->ring + (size_t)at * channels, s->input, (size_t)first * channels * sizeof(float));
        if (first < read) memcpy(s->ring, s->input + (size_t)first * channels, (size_t)(read - first) * channels * sizeof(float));
        s->input_frames += read;
        s->segment_frames += read;
        s->expected = s->segment_start + (double)s->segment_frames / ((double)s->params[LND_STRETCH_TEMPO_INDEX] * s->params[LND_STRETCH_RATE_INDEX]);
        if (!(s->expected < (double)UINT64_MAX)) s->error = LND_ERR_STATE;
    }
    if (status < 0) s->error = status;
    if (status == LND_SOURCE_EOF) s->ended = true;
    return read == count || s->ended;
}

static bool lnd_bungee_specify(lnd_bungee *s) {
    s->request.speed = (double)s->params[LND_STRETCH_TEMPO_INDEX] * s->params[LND_STRETCH_RATE_INDEX];
    s->request.pitch_ratio = (double)s->params[LND_STRETCH_PITCH_INDEX] * s->params[LND_STRETCH_RATE_INDEX];
    s->request.mode = s->mode;
    if (!s->started) {
        s->request.position = 0;
        lnd_bg_preroll(s->engine, &s->request);
        s->started = true;
    }
    int32_t begin = 0, end = 0;
    s->error = lnd_bg_specify(s->engine, &s->request, (double)s->input_frames, &begin, &end);
    if (s->error) return false;
    if (end <= begin || (uint64_t)((int64_t)end - begin) > s->grain_capacity) {
        s->error = LND_ERR_EXTERNAL;
        return false;
    }
    s->grain_begin = (int64_t)s->input_frames + begin;
    s->grain_end = (int64_t)s->input_frames + end;
    s->specified = true;
    return true;
}

static void lnd_bungee_grain(lnd_node *node, lnd_bungee *s) {
    uint32_t count = (uint32_t)(s->grain_end - s->grain_begin);
    int64_t first = LND_MAX(s->grain_begin, 0);
    int64_t last = LND_MIN(s->grain_end, (int64_t)s->input_frames);
    if (first < last && (uint64_t)first + s->ring_capacity < s->input_frames) {
        s->error = LND_ERR_EXTERNAL;
        return;
    }
    for (uint32_t c = 0; c < node->channels; c++) {
        float *dst = s->grain + (size_t)c * s->grain_capacity;
        uint32_t head = (uint32_t)LND_MIN(count, LND_MAX(first - s->grain_begin, 0));
        uint32_t valid = (uint32_t)LND_MAX(last - first, 0);
        memset(dst, 0, (size_t)head * sizeof(float));
        for (uint32_t i = 0; i < valid; i++)
            dst[head + i] = s->ring[(((uint64_t)first + i) & (s->ring_capacity - 1)) * node->channels + c];
        memset(dst + head + valid, 0, (size_t)(count - head - valid) * sizeof(float));
    }
    s->error = lnd_bg_process(s->engine, s->grain, s->grain_capacity, &s->output);
    if (s->error) return;
    s->consumed = 0;
    if (!isfinite(s->output.begin) || !isfinite(s->output.end) || s->output.end <= 0) {
        s->consumed = s->output.frames;
    } else if (s->output.end <= s->output.begin || s->output.stride_samples < s->output.frames || (s->output.frames && !s->output.data)) {
        s->error = LND_ERR_EXTERNAL;
        return;
    } else if (s->output.begin < 0) {
        double skip = ceil(-s->output.begin * s->output.frames / (s->output.end - s->output.begin));
        s->consumed = (uint32_t)LND_MIN(skip, s->output.frames);
    }
    lnd_bg_next(s->engine, &s->request);
    s->specified = false;
}

static void lnd_bungee_render(lnd_node *node, float *dst, uint32_t frames) {
    lnd_bungee *s = lnd_processor_user(node);
    uint32_t done = 0, channels = node->channels;
    uint64_t budget = LND_MAX((uint64_t)node->block * 32, (uint64_t)s->grain_capacity * 2 + (uint64_t)frames * 32);
    uint32_t grains = 0;
    while (done < frames && !s->error && !s->drained && grains < frames + 16) {
        uint64_t limit = lnd_bungee_limit(s);
        uint64_t remaining = limit > s->output_frames ? limit - s->output_frames : 0;
        uint32_t available = s->output.frames - s->consumed;
        uint32_t got = (uint32_t)LND_MIN(LND_MIN(frames - done, available), remaining);
        for (uint32_t c = 0; c < channels; c++)
            for (uint32_t i = 0; i < got; i++)
                dst[(size_t)(done + i) * channels + c] = s->output.data[(size_t)c * s->output.stride_samples + s->consumed + i];
        s->consumed += got;
        s->output_frames += got;
        done += got;
        if (s->ended && s->output_frames >= limit) {
            s->drained = true;
            break;
        }
        if (done == frames || !budget) break;
        if (s->output.frames > s->consumed) {
            uint32_t count = (uint32_t)LND_MIN(budget, node->block);
            budget -= count;
            if (!lnd_bungee_read(node, s, count)) break;
            continue;
        }
        if (!s->specified && !lnd_bungee_specify(s)) break;
        while (!s->ended && !s->error && s->grain_end > (int64_t)s->input_frames && budget) {
            uint32_t count = (uint32_t)LND_MIN(LND_MIN((uint64_t)(s->grain_end - (int64_t)s->input_frames), node->block), budget);
            budget -= count;
            if (!lnd_bungee_read(node, s, count)) break;
        }
        if (s->error || (!s->ended && s->grain_end > (int64_t)s->input_frames)) break;
        if (s->ended && !lnd_bungee_limit(s)) {
            s->drained = true;
            break;
        }
        lnd_bungee_grain(node, s);
        grains++;
    }
    node->rendered = done;
    int32_t status = s->error ? s->error : s->drained ? LND_SOURCE_EOF : done == frames ? LND_SOURCE_READY : LND_SOURCE_WAITING;
    lnd_store(&node->status, status);
    lnd_store(&node->active, !s->drained && !s->error);
    if (done < frames) memset(dst + (size_t)done * channels, 0, (size_t)(frames - done) * channels * sizeof(float));
}

static void lnd_bungee_command(lnd_node *node, const lnd_cmd *command, bool immediate) {
    lnd_bungee *s = lnd_processor_user(node);
    if (command->op == LND_OP_FLUSH)
        s->ended = true;
    else
        s->processor->command(node, command, immediate);
}

static void lnd_bungee_destroy(lnd_node *node) {
    lnd_bungee *s = lnd_processor_user(node);
    s->processor->destroy(node);
}

static const lnd_node_vt lnd_bungee_vt = {
    .render = lnd_bungee_render,
    .command = lnd_bungee_command,
    .destroy = lnd_bungee_destroy,
};

LND_NODE *LND_NodeCreateBungee(uint32_t channels, uint32_t sample_rate_hz, const LND_BUNGEE_CONFIG *config) {
    LND_BUNGEE_CONFIG c = config ? *config : (LND_BUNGEE_CONFIG){0};
    if (!c.tempo_ratio) c.tempo_ratio = 1;
    if (!c.pitch_ratio) c.pitch_ratio = 1;
    if (!c.rate_ratio) c.rate_ratio = 1;
    if (channels > 32 || (sample_rate_hz && (sample_rate_hz < 8000 || sample_rate_hz > 192000)) || c.grain_adjust < -1 || c.grain_adjust > 1 ||
        c.resample_mode < LND_BUNGEE_RESAMPLE_AUTO || c.resample_mode > LND_BUNGEE_RESAMPLE_OUTPUT ||
        !lnd_stretch_param_valid(nullptr, LND_BUNGEE_PARAM_TEMPO_RATIO, c.tempo_ratio) ||
        !lnd_stretch_param_valid(nullptr, LND_BUNGEE_PARAM_PITCH_RATIO, c.pitch_ratio) ||
        !lnd_stretch_param_valid(nullptr, LND_BUNGEE_PARAM_RATE_RATIO, c.rate_ratio))
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_STATE);
    }
    lnd_bungee *s = lnd_alloc_zero(sizeof *s);
    if (!s) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    lnd_node *node = LND_NodeCreateProcessor(&lnd_bungee_procs, s, channels, sample_rate_hz);
    if (!node) {
        lnd_free(s);
        lnd_context_unlock();
        return nullptr;
    }
    int32_t result = node->sample_rate_hz >= 8000 && node->sample_rate_hz <= 192000 ? LND_OK : LND_ERR_INVALID_ARG;
    s->grain_adjust = c.grain_adjust;
    s->mode = c.resample_mode;
    if (result == LND_OK) s->engine = lnd_bg_create(node->channels, node->sample_rate_hz, c.grain_adjust, &result);
    if (result == LND_OK) {
        uint32_t maximum = lnd_bg_max_input(s->engine);
        s->hop = (maximum - 1) / 32;
        s->grain_capacity = maximum * 4;
        s->ring_capacity = lnd_next_pow2_u32(s->grain_capacity + node->block);
        size_t count = ((size_t)node->block + s->grain_capacity + s->ring_capacity) * node->channels;
        s->memory = lnd_alloc_aligned(count * sizeof(float), LND_CACHE_LINE);
        if (!s->memory)
            result = LND_ERR_OUT_OF_MEMORY;
        else {
            s->input = s->memory;
            s->ring = s->input + (size_t)node->block * node->channels;
            s->grain = s->ring + (size_t)s->ring_capacity * node->channels;
        }
    }
    const float params[] = {c.tempo_ratio, c.pitch_ratio, c.rate_ratio};
    for (int32_t i = 0; i < LND_STRETCH_PARAM_COUNT && result == LND_OK; i++) {
        s->params[i] = params[i];
        lnd_processor_set_param(node, LND_BUNGEE_PARAM_TEMPO_RATIO + i, params[i]);
    }
    if (result != LND_OK) {
        LND_NodeFree(node);
        lnd_context_unlock();
        return lnd_error_null(result);
    }
    s->processor = node->vt;
    node->vt = &lnd_bungee_vt;
    lnd_context_unlock();
    return node;
}

int32_t LND_NodeEndBungeeInput(LND_NODE *node) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_context_has_node(node) && lnd_bungee_node(node)) {
        result = lnd_node_post(node, LND_OP_FLUSH, 0, 0, 0);
        if (result == LND_OK && !lnd_load(&node->active)) lnd_node_drain(node, true);
    }
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_NodeResetBungee(LND_NODE *node) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_context_has_node(node) || !lnd_bungee_node(node)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    lnd_bungee *s = lnd_processor_user(node);
    int32_t result = LND_OK;
    void *engine = lnd_bg_create(node->channels, node->sample_rate_hz, s->grain_adjust, &result);
    if (engine) {
        lnd_spinlock_lock(&node->lock);
        lnd_node_process(node, false);
        void *old = s->engine;
        s->engine = engine;
        engine = old;
        for (int32_t i = 0; i < LND_STRETCH_PARAM_COUNT; i++)
            s->params[i] = lnd_processor_param(node, LND_BUNGEE_PARAM_TEMPO_RATIO + i);
        s->input_frames = s->output_frames = s->segment_frames = 0;
        s->expected = s->segment_start = 0;
        s->output = (lnd_bg_chunk){0};
        s->request = (lnd_bg_request){0};
        s->consumed = 0;
        s->started = s->specified = s->ended = s->drained = false;
        s->error = LND_OK;
        lnd_store(&node->status, LND_SOURCE_READY);
        lnd_store(&node->active, 1);
        lnd_add(&node->revision, 1);
        lnd_spinlock_unlock(&node->lock);
        lnd_bg_destroy(engine);
    }
    lnd_context_unlock();
    return lnd_error(result);
}

static uint32_t lnd_bungee_latency(const lnd_bungee *s) {
    double pitch_ratio = (double)s->params[LND_STRETCH_PITCH_INDEX] * s->params[LND_STRETCH_RATE_INDEX],
           speed = (double)s->params[LND_STRETCH_TEMPO_INDEX] * s->params[LND_STRETCH_RATE_INDEX];
    bool input = s->mode == LND_BUNGEE_RESAMPLE_INPUT || pitch_ratio < 0.25 || (s->mode == LND_BUNGEE_RESAMPLE_AUTO && pitch_ratio > 1);
    double half = input ? ceil(4 * s->hop * pitch_ratio) + (pitch_ratio == 1 ? 0 : 2) : 4 * s->hop;
    return (uint32_t)ceil(half + s->hop * speed / (input ? 1 : pitch_ratio));
}

int32_t LND_NodeGetBungeeInfo(const LND_NODE *object, LND_BUNGEE_INFO *info) {
    LND_NODE *node = (LND_NODE *)object;
    if (!info) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_context_has_node(node) && lnd_bungee_node(node)) {
        lnd_spinlock_lock(&node->lock);
        lnd_bungee *s = lnd_processor_user(node);
        double position = LND_MAX(s->request.position, 0);
        uint32_t pending = position < (double)s->input_frames ? (uint32_t)LND_MIN((double)s->input_frames - position, s->ring_capacity) : 0;
        *info = (LND_BUNGEE_INFO){.input_frames = s->input_frames,
                                  .output_frames = s->output_frames,
                                  .duration_ratio = 1 / ((double)s->params[LND_STRETCH_TEMPO_INDEX] * s->params[LND_STRETCH_RATE_INDEX]),
                                  .initial_latency_frames = lnd_bungee_latency(s),
                                  .grain_frames = s->hop * 8,
                                  .hop_frames = s->hop,
                                  .max_input_frames = s->grain_capacity,
                                  .buffered_output_frames = s->drained || s->error ? 0 : s->output.frames - s->consumed,
                                  .unprocessed_frames = s->drained || s->error ? 0 : pending,
                                  .input_ended = s->ended,
                                  .error = s->error};
        lnd_spinlock_unlock(&node->lock);
        result = LND_OK;
    }
    lnd_context_unlock();
    return lnd_error(result);
}

const char *LND_BungeeGetVersion(void) { return lnd_bg_version(); }

static LND_NODE *lnd_bungee_create(uint32_t channels, uint32_t rate, const LND_STRETCH_CONFIG *config) {
    LND_BUNGEE_CONFIG c = {.tempo_ratio = config->tempo_ratio, .pitch_ratio = config->pitch_ratio, .rate_ratio = config->rate_ratio};
    return LND_NodeCreateBungee(channels, rate, &c);
}

static int32_t lnd_bungee_info(const LND_NODE *node, LND_STRETCH_INFO *info) {
    LND_BUNGEE_INFO i;
    int32_t result = LND_NodeGetBungeeInfo(node, &i);
    if (result == LND_OK)
        *info = (LND_STRETCH_INFO){.input_frames = i.input_frames,
                                   .output_frames = i.output_frames,
                                   .duration_ratio = i.duration_ratio,
                                   .initial_latency_frames = i.initial_latency_frames,
                                   .buffered_output_frames = i.buffered_output_frames,
                                   .unprocessed_frames = i.unprocessed_frames,
                                   .backend = &lnd_stretch_bungee,
                                   .error = i.error,
                                   .input_ended = i.input_ended};
    return result;
}

static int32_t lnd_stretch_priority = 200;

const lnd_stretch_backend lnd_stretch_bungee = {
    .name = "bungee",
    .priority = &lnd_stretch_priority,
    .default_priority = 200,
    .create = lnd_bungee_create,
    .matches = lnd_bungee_node,
    .info = lnd_bungee_info,
    .reset = LND_NodeResetBungee,
    .flush = LND_NodeEndBungeeInput,
};
