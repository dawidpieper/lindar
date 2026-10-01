#include "src/alloc.h"
#include "src/context.h"
#include "playback/graph/node.h"
#include "src/error.h"
#include "src/platform.h"
#include "lindar_dsp.h"
#include "pcm/audio/simd.h"

#include <math.h>

typedef struct lnd_biquad {
    uint32_t channels;
    uint32_t sample_rate_hz;
    int32_t type;
    float frequency;
    float q;
    float gain_db;
    lnd_biquad_coeff_f32 coefficient;
    float z1[LND_MAX_CHANNELS];
    float z2[LND_MAX_CHANNELS];
} lnd_biquad;

static void lnd_biquad_update(lnd_biquad *s) {
    lnd_biquad_coeff k = lnd_biquad_coefficients(s->type, s->sample_rate_hz, LND_MAX(s->frequency, 1), s->q, s->gain_db, 0, 0);
    s->coefficient.b0 = (float)k.b0;
    s->coefficient.b1 = (float)k.b1;
    s->coefficient.b2 = (float)k.b2;
    s->coefficient.a1 = (float)k.a1;
    s->coefficient.a2 = (float)k.a2;
}

static void lnd_biquad_process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    lnd_biquad *s = user;
    if (sample_rate_hz != s->sample_rate_hz) {
        s->sample_rate_hz = sample_rate_hz;
        lnd_biquad_update(s);
    }
    uint32_t ch = LND_MIN(channels, LND_MAX_CHANNELS);
    uint32_t first = 0;
    if (lnd_simd.biquad_f32 && ch >= 2 && frames >= (ch >= 4 ? 16 : 128))
        first = lnd_simd.biquad_f32(pcm, frames, ch, &s->coefficient, s->z1, s->z2);
    for (uint32_t c = first; c < ch; c++) {
        float z1 = s->z1[c], z2 = s->z2[c];
        for (uint32_t f = 0; f < frames; f++) {
            float *p = pcm + (size_t)f * channels + c;
            float x = *p;
            float y = s->coefficient.b0 * x + z1;
            z1 = s->coefficient.b1 * x - s->coefficient.a1 * y + z2;
            z2 = s->coefficient.b2 * x - s->coefficient.a2 * y;
            *p = y;
        }
        s->z1[c] = fabsf(z1) < 1e-30f ? 0.0f : z1;
        s->z2[c] = fabsf(z2) < 1e-30f ? 0.0f : z2;
    }
}

static bool lnd_biquad_validate(void *user, int32_t param, float value) {
    if (!isfinite(value)) return false;
    switch (param) {
    case LND_DSP_PARAM_TYPE: return value >= LND_BIQUAD_LOWPASS && value <= LND_BIQUAD_ALLPASS && value == truncf(value);
    case LND_DSP_PARAM_FREQUENCY_HZ:
    case LND_DSP_PARAM_Q: return value > 0.0f;
    case LND_DSP_PARAM_GAIN_DB: return value >= -120.0f && value <= 120.0f;
    default: return false;
    }
}

static bool lnd_biquad_can_slide(void *user, int32_t param) { return param != LND_DSP_PARAM_TYPE; }

static void lnd_biquad_param(void *user, int32_t param, float value) {
    lnd_biquad *s = user;
    switch (param) {
    case LND_DSP_PARAM_TYPE:
        s->type = (int32_t)value;
        break;
    case LND_DSP_PARAM_FREQUENCY_HZ:
        s->frequency = value;
        break;
    case LND_DSP_PARAM_Q:
        s->q = value;
        break;
    case LND_DSP_PARAM_GAIN_DB:
        s->gain_db = value;
        break;
    default:
        return;
    }
    lnd_biquad_update(s);
}

static void lnd_biquad_release(void *user) { lnd_free(user); }

static const LND_PROCESSOR_PROCS lnd_biquad_procs = {
    .process = lnd_biquad_process,
    .param = lnd_biquad_param,
    .validate = lnd_biquad_validate,
    .can_slide = lnd_biquad_can_slide,
    .release = lnd_biquad_release,
};

LND_NODE *LND_NodeCreateBiquad(uint32_t channels, uint32_t sample_rate_hz, const LND_BIQUAD_CONFIG *config) {
    if (!config) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_biquad_validate(nullptr, LND_DSP_PARAM_TYPE, (float)config->type) || !lnd_biquad_validate(nullptr, LND_DSP_PARAM_FREQUENCY_HZ, config->frequency_hz) ||
        !lnd_biquad_validate(nullptr, LND_DSP_PARAM_Q, config->q) || !lnd_biquad_validate(nullptr, LND_DSP_PARAM_GAIN_DB, config->gain_db))
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_biquad *s = lnd_alloc_zero(sizeof *s);
    if (!s) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    s->type = config->type;
    s->frequency = config->frequency_hz;
    s->q = config->q;
    s->gain_db = config->gain_db;
    s->sample_rate_hz = 1;
    LND_NODE *n = LND_NodeCreateProcessor(&lnd_biquad_procs, s, channels, sample_rate_hz);
    if (!n) {
        lnd_free(s);
        lnd_context_unlock();
        return nullptr;
    }
    s->channels = LND_NodeGetChannels(n);
    s->sample_rate_hz = LND_NodeGetSampleRateHz(n);
    lnd_biquad_update(s);
    lnd_processor_set_param(n, LND_DSP_PARAM_TYPE, (float)config->type);
    lnd_processor_set_param(n, LND_DSP_PARAM_FREQUENCY_HZ, config->frequency_hz);
    lnd_processor_set_param(n, LND_DSP_PARAM_Q, config->q);
    lnd_processor_set_param(n, LND_DSP_PARAM_GAIN_DB, config->gain_db);
    lnd_context_unlock();
    return n;
}
