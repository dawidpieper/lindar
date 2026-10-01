#include "src/alloc.h"
#include "src/context.h"
#include "playback/graph/node.h"
#include "src/error.h"
#include "src/platform.h"
#include "lindar_dsp.h"

#include <math.h>

typedef struct lnd_panner {
    float pan;
    int32_t mode;
    float gl, gr, cross;
    float cur_l, cur_r, cur_x;
    float slew;
    bool steady;
} lnd_panner;

static void lnd_panner_update(lnd_panner *s) {
    float pan = LND_CLAMP(s->pan, -1.0f, 1.0f);
    const float half_pi = 1.57079632679f;
    if (s->mode == LND_PAN_MONO) {
        float x = (pan + 1.0f) * 0.5f;
        s->gl = cosf(x * half_pi);
        s->gr = sinf(x * half_pi);
        s->cross = 0.0f;
        return;
    }
    if (pan <= 0.0f) {
        float x = pan + 1.0f;
        s->gl = 1.0f;
        s->gr = sinf(x * half_pi);
        s->cross = cosf(x * half_pi);
    } else {
        s->gl = cosf(pan * half_pi);
        s->gr = 1.0f;
        s->cross = sinf(pan * half_pi);
    }
}

static void lnd_panner_process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    lnd_panner *s = user;
    LND_UNUSED(sample_rate_hz);
    if (channels != 2) return;
    bool left = s->pan <= 0.0f;
    if (s->steady) {
        for (uint32_t f = 0; f < frames; f++) {
            float l = pcm[f * 2], r = pcm[f * 2 + 1];
            if (s->mode == LND_PAN_MONO) {
                pcm[f * 2] = l * s->cur_l;
                pcm[f * 2 + 1] = l * s->cur_r;
            } else if (left) {
                pcm[f * 2] = l + r * s->cur_x;
                pcm[f * 2 + 1] = r * s->cur_r;
            } else {
                pcm[f * 2] = l * s->cur_l;
                pcm[f * 2 + 1] = r + l * s->cur_x;
            }
        }
        return;
    }
    for (uint32_t f = 0; f < frames; f++) {
        s->cur_l += (s->gl - s->cur_l) * s->slew;
        s->cur_r += (s->gr - s->cur_r) * s->slew;
        s->cur_x += (s->cross - s->cur_x) * s->slew;
        float l = pcm[f * 2], r = pcm[f * 2 + 1];
        if (s->mode == LND_PAN_MONO) {
            pcm[f * 2] = l * s->cur_l;
            pcm[f * 2 + 1] = l * s->cur_r;
        } else if (left) {
            pcm[f * 2] = l + r * s->cur_x;
            pcm[f * 2 + 1] = r * s->cur_r;
        } else {
            pcm[f * 2] = l * s->cur_l;
            pcm[f * 2 + 1] = r + l * s->cur_x;
        }
    }
}

static bool lnd_panner_validate(void *user, int32_t param, float value) {
    if (!isfinite(value)) return false;
    if (param == LND_DSP_PARAM_PAN) return value >= -1.0f && value <= 1.0f;
    return param == LND_DSP_PARAM_PAN_MODE && (value == LND_PAN_STEREO || value == LND_PAN_MONO);
}

static bool lnd_panner_can_slide(void *user, int32_t param) { return param == LND_DSP_PARAM_PAN; }

static void lnd_panner_param(void *user, int32_t param, float value) {
    lnd_panner *s = user;
    if (param == LND_DSP_PARAM_PAN)
        s->pan = value;
    else if (param == LND_DSP_PARAM_PAN_MODE)
        s->mode = value >= 0.5f ? LND_PAN_MONO : LND_PAN_STEREO;
    else
        return;
    s->steady = false;
    lnd_panner_update(s);
}

static void lnd_panner_release(void *user) { lnd_free(user); }

static const LND_PROCESSOR_PROCS lnd_panner_procs = {
    .process = lnd_panner_process,
    .param = lnd_panner_param,
    .validate = lnd_panner_validate,
    .can_slide = lnd_panner_can_slide,
    .release = lnd_panner_release,
    .flags = LND_PROCESSOR_BOUNDED,
};

LND_NODE *LND_NodeCreatePanner(uint32_t sample_rate_hz, float pan, int32_t mode) {
    if (!lnd_panner_validate(nullptr, LND_DSP_PARAM_PAN, pan) || !lnd_panner_validate(nullptr, LND_DSP_PARAM_PAN_MODE, (float)mode)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_panner *s = lnd_alloc_zero(sizeof *s);
    if (!s) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    s->pan = pan;
    s->mode = mode;
    lnd_panner_update(s);
    s->cur_l = s->gl;
    s->cur_r = s->gr;
    s->cur_x = s->cross;
    s->steady = true;
    LND_NODE *n = LND_NodeCreateProcessor(&lnd_panner_procs, s, 2, sample_rate_hz);
    if (!n) {
        lnd_free(s);
        lnd_context_unlock();
        return nullptr;
    }
    s->slew = 1.0f - expf(-1.0f / (0.005f * (float)LND_NodeGetSampleRateHz(n)));
    lnd_processor_set_param(n, LND_DSP_PARAM_PAN, pan);
    lnd_processor_set_param(n, LND_DSP_PARAM_PAN_MODE, (float)mode);
    lnd_context_unlock();
    return n;
}
