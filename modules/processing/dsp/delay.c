#include "src/alloc.h"
#include "src/context.h"
#include "playback/graph/node.h"
#include "src/error.h"
#include "src/platform.h"
#include "lindar_dsp.h"

#include <math.h>
#include <string.h>

typedef struct lnd_delay {
    uint32_t channels;
    uint32_t sample_rate_hz;
    float *ring;
    uint32_t cap;
    uint32_t write;
    float max_ms;
    float delay_ms;
    float feedback;
    float mix;
    float current;
    float target;
    float slew;
} lnd_delay;

static void lnd_delay_retarget(lnd_delay *s) {
    float ms = LND_CLAMP(s->delay_ms, 0.0f, s->max_ms);
    s->target = (float)((double)ms * (double)s->sample_rate_hz / 1000.0);
    if (s->target > (float)(s->cap - 2)) s->target = (float)(s->cap - 2);
}

LND_INLINE void lnd_delay_process_buffer(void *user, float *pcm, float *const *planes, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    lnd_delay *s = user;
    if (!s->ring || channels != s->channels) return;
    if (sample_rate_hz != s->sample_rate_hz) {
        s->sample_rate_hz = sample_rate_hz;
        lnd_delay_retarget(s);
        s->current = s->target;
    }
    uint32_t ch = channels;
    float wet = LND_CLAMP(s->mix, 0.0f, 1.0f);
    float dry = 1.0f - wet;
    float fb = s->feedback;
    for (uint32_t f = 0; f < frames; f++) {
        s->current += (s->target - s->current) * s->slew;
        float d = s->current < 0.0f ? 0.0f : s->current;
        uint32_t di = (uint32_t)d;
        float frac = d - (float)di;
        uint32_t r0 = s->write >= di ? s->write - di : s->cap - (di - s->write);
        uint32_t r1 = r0 ? r0 - 1 : s->cap - 1;
        float *w = s->ring + (size_t)s->write * ch;
        const float *p0 = s->ring + (size_t)r0 * ch;
        const float *p1 = s->ring + (size_t)r1 * ch;
        for (uint32_t c = 0; c < ch; c++) {
            float delayed = p0[c] + (p1[c] - p0[c]) * frac;
            float *out = planes ? planes[c] + f : pcm + (size_t)f * ch + c;
            float x = *out;
            w[c] = x + delayed * fb;
            *out = x * dry + delayed * wet;
        }
        if (++s->write == s->cap) s->write = 0;
    }
}

static void lnd_delay_process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    lnd_delay_process_buffer(user, pcm, nullptr, frames, channels, sample_rate_hz);
}

static void lnd_delay_process_planar(void *user, float *const *planes, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    lnd_delay_process_buffer(user, nullptr, planes, frames, channels, sample_rate_hz);
}

static bool lnd_delay_validate(void *user, int32_t param, float value) {
    const lnd_delay *s = user;
    if (!isfinite(value)) return false;
    switch (param) {
    case LND_DSP_PARAM_DELAY_MS: return value >= 0.0f && value <= s->max_ms;
    case LND_DSP_PARAM_FEEDBACK: return value >= 0.0f && value < 1.0f;
    case LND_DSP_PARAM_MIX: return value >= 0.0f && value <= 1.0f;
    default: return false;
    }
}

static void lnd_delay_param(void *user, int32_t param, float value) {
    lnd_delay *s = user;
    switch (param) {
    case LND_DSP_PARAM_DELAY_MS:
        s->delay_ms = value;
        lnd_delay_retarget(s);
        break;
    case LND_DSP_PARAM_FEEDBACK:
        s->feedback = value;
        break;
    case LND_DSP_PARAM_MIX:
        s->mix = value;
        break;
    default:
        break;
    }
}

static void lnd_delay_release(void *user) {
    lnd_delay *s = user;
    lnd_free_aligned(s->ring);
    lnd_free(s);
}

static const LND_PROCESSOR_PROCS lnd_delay_procs = {
    .process = lnd_delay_process,
    .param = lnd_delay_param,
    .validate = lnd_delay_validate,
    .release = lnd_delay_release,
};

LND_NODE *LND_NodeCreateDelay(uint32_t channels, uint32_t sample_rate_hz, const LND_DELAY_CONFIG *config) {
    if (!config) return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_delay limits = {.max_ms = config->max_delay_ms};
    if (!isfinite(config->max_delay_ms) || config->max_delay_ms <= 0.0f || config->max_delay_ms > 60000.0f ||
        !lnd_delay_validate(&limits, LND_DSP_PARAM_DELAY_MS, config->delay_ms) || !lnd_delay_validate(&limits, LND_DSP_PARAM_FEEDBACK, config->feedback) ||
        !lnd_delay_validate(&limits, LND_DSP_PARAM_MIX, config->mix)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_delay *s = lnd_alloc_zero(sizeof *s);
    if (!s) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    s->max_ms = config->max_delay_ms;
    s->delay_ms = config->delay_ms;
    s->feedback = config->feedback;
    s->mix = config->mix;
    LND_NODE *n = LND_NodeCreateProcessor(&lnd_delay_procs, s, channels, sample_rate_hz);
    if (!n) {
        lnd_free(s);
        lnd_context_unlock();
        return nullptr;
    }
    s->channels = LND_NodeGetChannels(n);
    s->sample_rate_hz = LND_NodeGetSampleRateHz(n);
    double capacity = ceil((double)config->max_delay_ms * s->sample_rate_hz / 1000) + 2;
    if (capacity > UINT32_MAX || capacity > SIZE_MAX / s->channels / sizeof(float)) {
        LND_NodeFree(n);
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    s->cap = (uint32_t)capacity;
    s->ring = lnd_alloc_aligned((size_t)s->cap * s->channels * sizeof(float), LND_CACHE_LINE);
    if (!s->ring) {
        LND_NodeFree(n);
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    memset(s->ring, 0, (size_t)s->cap * s->channels * sizeof(float));
    s->slew = 1.0f - expf(-1.0f / (0.02f * (float)s->sample_rate_hz));
    lnd_delay_retarget(s);
    s->current = s->target;
    lnd_processor_set_param(n, LND_DSP_PARAM_DELAY_MS, config->delay_ms);
    lnd_processor_set_param(n, LND_DSP_PARAM_FEEDBACK, config->feedback);
    lnd_processor_set_param(n, LND_DSP_PARAM_MIX, config->mix);
    if (s->channels >= 4) lnd_processor_set_planar(n, lnd_delay_process_planar);
    lnd_context_unlock();
    return n;
}
