#include "src/alloc.h"
#include "src/context.h"
#include "playback/graph/node.h"
#include "src/error.h"
#include "src/atomic.h"
#include "src/platform.h"
#include "lindar_dsp.h"

#include <math.h>
#include <string.h>

typedef struct lnd_meter {
    uint32_t channels;
    uint32_t sample_rate_hz;
    float decay_ms;
    float peak_hold[LND_MAX_CHANNELS];
    float rms_hold[LND_MAX_CHANNELS];
    lnd_atomic_u32 peak[LND_MAX_CHANNELS];
    lnd_atomic_u32 rms[LND_MAX_CHANNELS];
} lnd_meter;

static uint32_t lnd_meter_bits(float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

static float lnd_meter_float(uint32_t u) {
    float v;
    memcpy(&v, &u, sizeof v);
    return v;
}

LND_INLINE void lnd_meter_process_buffer(void *user, float *pcm, float *const *planes, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    lnd_meter *s = user;
    if (!frames) return;
    uint32_t ch = LND_MIN(channels, LND_MAX_CHANNELS);
    float decay = s->decay_ms > 0.0f ? expf(-(float)frames / (s->decay_ms * 0.001f * (float)sample_rate_hz)) : 0.0f;
    for (uint32_t c = 0; c < ch; c++) {
        float peak = 0.0f;
        double sum = 0.0;
        for (uint32_t f = 0; f < frames; f++) {
            const float *p = planes ? planes[c] + f : pcm + (size_t)f * channels + c;
            float a = fabsf(*p);
            if (a > peak) peak = a;
            sum += (double)*p * (double)*p;
        }
        float rms = (float)sqrt(sum / (double)frames);
        float held_peak = s->peak_hold[c] * decay;
        float held_rms = s->rms_hold[c] * decay;
        s->peak_hold[c] = peak > held_peak ? peak : held_peak;
        s->rms_hold[c] = rms > held_rms ? rms : held_rms;
        lnd_store(&s->peak[c], lnd_meter_bits(s->peak_hold[c]));
        lnd_store(&s->rms[c], lnd_meter_bits(s->rms_hold[c]));
    }
}

static void lnd_meter_process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    lnd_meter_process_buffer(user, pcm, nullptr, frames, channels, sample_rate_hz);
}

static void lnd_meter_process_planar(void *user, float *const *planes, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    lnd_meter_process_buffer(user, nullptr, planes, frames, channels, sample_rate_hz);
}

static bool lnd_meter_validate(void *user, int32_t param, float value) {
    return param == LND_DSP_PARAM_DECAY_MS && isfinite(value) && value >= 0.0f;
}

static void lnd_meter_param(void *user, int32_t param, float value) {
    lnd_meter *s = user;
    if (param == LND_DSP_PARAM_DECAY_MS) s->decay_ms = value;
}

static void lnd_meter_release(void *user) { lnd_free(user); }

static const LND_PROCESSOR_PROCS lnd_meter_procs = {
    .process = lnd_meter_process,
    .param = lnd_meter_param,
    .validate = lnd_meter_validate,
    .release = lnd_meter_release,
};

LND_NODE *LND_NodeCreateMeter(uint32_t channels, uint32_t sample_rate_hz, float decay_ms) {
    if (!lnd_meter_validate(nullptr, LND_DSP_PARAM_DECAY_MS, decay_ms)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_meter *s = lnd_alloc_zero(sizeof *s);
    if (!s) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    s->decay_ms = decay_ms;
    LND_NODE *n = LND_NodeCreateProcessor(&lnd_meter_procs, s, channels, sample_rate_hz);
    if (!n) {
        lnd_free(s);
        lnd_context_unlock();
        return nullptr;
    }
    s->channels = LND_NodeGetChannels(n);
    s->sample_rate_hz = LND_NodeGetSampleRateHz(n);
    lnd_processor_set_param(n, LND_DSP_PARAM_DECAY_MS, decay_ms);
    if (s->channels >= 4) lnd_processor_set_planar(n, lnd_meter_process_planar);
    lnd_context_unlock();
    return n;
}

static const lnd_meter *lnd_meter_of(const LND_NODE *n) {
    const LND_PROCESSOR_PROCS *procs = LND_NodeGetProcessorProcs(n);
    return procs && procs->process == lnd_meter_process ? LND_NodeGetProcessorUser(n) : nullptr;
}

float LND_NodeGetMeterPeak(const LND_NODE *meter, uint32_t channel) {
    const lnd_meter *s = lnd_meter_of(meter);
    return s && channel < s->channels ? lnd_meter_float(lnd_load(&s->peak[channel])) : 0.0f;
}

float LND_NodeGetMeterRms(const LND_NODE *meter, uint32_t channel) {
    const lnd_meter *s = lnd_meter_of(meter);
    return s && channel < s->channels ? lnd_meter_float(lnd_load(&s->rms[channel])) : 0.0f;
}
