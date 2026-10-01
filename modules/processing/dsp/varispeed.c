#include "pcm/audio/source.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/error.h"
#include "playback/graph/context.h"
#include "playback/graph/node.h"
#include "playback/graph/native.h"
#include "lindar_dsp.h"

#include <math.h>
#include <string.h>

typedef struct lnd_varispeed {
    lnd_source feed;
    lnd_source *resampler;
    float *buffer;
    lnd_node *node;
    const lnd_node_vt *processor;
} lnd_varispeed;

static int64_t lnd_varispeed_read_pcm(lnd_source *source, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_varispeed *s = (lnd_varispeed *)source;
    size_t done = 0;
    while (done < frames) {
        uint32_t want = (uint32_t)LND_MIN(frames - done, s->node->block);
        if (s->node->native) {
            lnd_bus_render_pcm(s->node, pcm, offset + done, want);
        } else {
            LND_PCM scratch = {.data = s->buffer, .frames = s->node->block, .channels = source->channels, .format = LND_FORMAT_F32};
            lnd_bus_render(s->node, scratch.data, want);
            int32_t result = LND_PcmConvert(pcm, offset + done, &scratch, 0, s->node->rendered);
            if (!lnd_node_pcm_result(s->node, result)) break;
        }
        uint32_t got = s->node->rendered;
        lnd_store_relaxed(&source->pos, lnd_load_relaxed(&source->pos) + got);
        done += got;
        if (got < want) break;
    }
    int32_t status = lnd_load(&s->node->status);
    lnd_store(&source->status, status);
    return !done && status < 0 ? status : (int64_t)done;
}

static uint64_t lnd_varispeed_read(lnd_source *source, float *dst, uint64_t frames) {
    LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = source->channels, .format = LND_FORMAT_F32};
    int64_t got = lnd_varispeed_read_pcm(source, &pcm, 0, (size_t)frames);
    return got > 0 ? (uint64_t)got : 0;
}

static const lnd_source_vt lnd_varispeed_feed_vt = {.read = lnd_varispeed_read, .read_pcm = lnd_varispeed_read_pcm};

static bool lnd_varispeed_validate(void *user, int32_t param, float value) {
    LND_UNUSED(user);
    return param == LND_DSP_PARAM_RATE_RATIO && isfinite(value) && value >= 0.25f && value <= 4.0f;
}

static void lnd_varispeed_param(void *user, int32_t param, float value) {
    lnd_varispeed *s = user;
    if (param == LND_DSP_PARAM_RATE_RATIO) lnd_resample_source_set_ratio(s->resampler, (double)value);
}

static void lnd_varispeed_release(void *user) {
    lnd_varispeed *s = user;
    lnd_source_free(s->resampler);
    lnd_free_aligned(s->buffer);
    lnd_free(s);
}

static int32_t lnd_varispeed_process(void *user, const LND_PCM *pcm, size_t offset, uint32_t frames, uint32_t sample_rate_hz) {
    LND_UNUSED(user);
    LND_UNUSED(pcm);
    LND_UNUSED(offset);
    LND_UNUSED(frames);
    LND_UNUSED(sample_rate_hz);
    return LND_OK;
}

static const LND_PROCESSOR_PROCS lnd_varispeed_procs = {
    .process_pcm = lnd_varispeed_process,
    .param = lnd_varispeed_param,
    .release = lnd_varispeed_release,
    .flags = LND_PROCESSOR_BOUNDED,
    .validate = lnd_varispeed_validate,
};

static void lnd_varispeed_render(lnd_node *node, float *dst, uint32_t frames) {
    lnd_varispeed *s = lnd_processor_user(node);
    uint32_t got = (uint32_t)lnd_source_read(s->resampler, dst, frames);
    int32_t status = lnd_source_status(s->resampler);
    node->rendered = got;
    lnd_store(&node->status, status);
    lnd_store(&node->active, status != LND_SOURCE_EOF || got != 0);
    if (got < frames) memset(dst + (size_t)got * node->channels, 0, (size_t)(frames - got) * node->channels * sizeof(float));
}

static void lnd_varispeed_render_pcm(lnd_node *node, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_varispeed *s = lnd_processor_user(node);
    int64_t read = lnd_source_read_pcm(s->resampler, pcm, offset, frames, false);
    uint32_t got = read > 0 ? (uint32_t)read : 0;
    int32_t status = read < 0 ? (int32_t)read : lnd_source_status(s->resampler);
    node->rendered = got;
    lnd_store(&node->status, status);
    lnd_store(&node->active, status >= 0 && (status != LND_SOURCE_EOF || got != 0));
    if (got < frames) LND_PcmSilence(pcm, offset + got, frames - got);
}

static void lnd_varispeed_command(lnd_node *node, const lnd_cmd *command, bool immediate) {
    lnd_varispeed *s = lnd_processor_user(node);
    if (command->op == LND_OP_RESET) {
        lnd_resample_source_reset(s->resampler);
        lnd_store(&node->status, LND_SOURCE_READY);
        lnd_store(&node->active, 1);
        lnd_add(&node->revision, 1);
    } else {
        s->processor->command(node, command, immediate);
    }
}

static void lnd_varispeed_destroy(lnd_node *node) {
    lnd_varispeed *s = lnd_processor_user(node);
    s->processor->destroy(node);
}

static const lnd_node_vt lnd_varispeed_vt = {
    .render = lnd_varispeed_render,
    .render_pcm = lnd_varispeed_render_pcm,
    .command = lnd_varispeed_command,
    .destroy = lnd_varispeed_destroy,
};

LND_NODE *LND_NodeCreateVarispeed(uint32_t channels, uint32_t sample_rate_hz, float rate_ratio, uint32_t resample_quality) {
    if (!isfinite(rate_ratio) || rate_ratio < 0 || resample_quality > LND_RESAMPLE_SINC32) return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_varispeed *s = lnd_alloc_zero(sizeof *s);
    if (!s) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    lnd_node *node = LND_NodeCreateProcessor(&lnd_varispeed_procs, s, channels, sample_rate_hz);
    if (!node) {
        lnd_free(s);
        return nullptr;
    }
    s->node = node;
    if (!node->native) {
        s->buffer = lnd_alloc_aligned((size_t)node->block * channels * sizeof(float), LND_CACHE_LINE);
        if (!s->buffer) {
            LND_NodeFree(node);
            return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
        }
    }
    s->feed = (lnd_source){.vt = &lnd_varispeed_feed_vt, .channels = node->channels, .sample_rate_hz = node->sample_rate_hz, .live = true};
    if (!rate_ratio) rate_ratio = 1.0f;
    if (!lnd_varispeed_validate(s, LND_DSP_PARAM_RATE_RATIO, rate_ratio)) {
        LND_NodeFree(node);
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
    s->resampler = lnd_resample_source_create_variable(&s->feed, node->block, resample_quality);
    if (!s->resampler) {
        LND_NodeFree(node);
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    s->processor = node->vt;
    node->vt = &lnd_varispeed_vt;
    lnd_varispeed_param(s, LND_DSP_PARAM_RATE_RATIO, rate_ratio);
    lnd_processor_set_param(node, LND_DSP_PARAM_RATE_RATIO, rate_ratio);
    return node;
}

uint32_t LND_NodeGetVarispeedBaseSampleRateHz(const LND_NODE *node) {
    return node && node->vt == &lnd_varispeed_vt ? ((const lnd_varispeed *)lnd_processor_user(node))->feed.sample_rate_hz : 0;
}

int32_t LND_NodeResetVarispeed(LND_NODE *node) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_context_has_node(node) && node->vt == &lnd_varispeed_vt) {
        result = lnd_node_post(node, LND_OP_RESET, 0, 0, 0);
        if (result == LND_OK && !lnd_load(&node->active)) lnd_node_drain(node, true);
    }
    lnd_context_unlock();
    return lnd_error(result);
}
