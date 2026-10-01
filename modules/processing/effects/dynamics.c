#include "effects.h"

typedef struct lnd_effect_dynamics {
    float previous[LND_MAX_CHANNELS];
    float gain, envelope, attack, release, threshold, exponent, makeup, adjustment;
    uint64_t hold, delay;
} lnd_effect_dynamics;

static int32_t init(lnd_effect *s) { return lnd_effect_allocate(s, sizeof(lnd_effect_dynamics), 0); }

static void update(lnd_effect *s, int32_t param) {
    lnd_effect_dynamics *d = s->state;
    if (s->type == LND_EFFECT_COMPRESSOR) {
        if (param == -1 || param == LND_EFFECT_PARAM_ATTACK_MS) d->attack = expf(-1000 / (FX(s, ATTACK_MS) * s->rate));
        if (param == -1 || param == LND_EFFECT_PARAM_RELEASE_MS) d->release = expf(-1000 / (FX(s, RELEASE_MS) * s->rate));
        if (param == -1 || param == LND_EFFECT_PARAM_THRESHOLD_DB) d->threshold = powf(10, FX(s, THRESHOLD_DB) / 20);
        if (param == -1 || param == LND_EFFECT_PARAM_RATIO) d->exponent = 1 - 1 / FX(s, RATIO);
        if (param == -1 || param == LND_EFFECT_PARAM_GAIN_DB) d->makeup = powf(10, FX(s, GAIN_DB) / 20);
    } else if (s->type == LND_EFFECT_DYNAMIC_GAIN) {
        if (param == -1 || param == LND_EFFECT_PARAM_ADJUST_SPEED) d->adjustment = -expm1f(-10 * FX(s, ADJUST_SPEED) / s->rate);
        if (param == -1) d->release = expf(-20.0f / s->rate);
        if (param == -1 || param == LND_EFFECT_PARAM_GAIN) d->gain = FX(s, GAIN);
        if (param == -1 || param == LND_EFFECT_PARAM_GAIN_DELAY_MS) {
            d->delay = (uint64_t)ceil((double)FX(s, GAIN_DELAY_MS) * s->rate / 1000);
            d->hold = d->delay;
        }
    }
}

static void reset(lnd_effect *s) {
    lnd_effect_dynamics *d = s->state;
    memset(d, 0, sizeof *d);
    d->gain = s->type == LND_EFFECT_DYNAMIC_GAIN ? FX(s, GAIN) : 1;
}

static void process(lnd_effect *s, float *pcm, uint32_t frames) {
    lnd_effect_dynamics *d = s->state;
    for (uint32_t f = 0; f < frames; f++) {
        float *out = pcm + (size_t)f * s->channels;
        if (s->type == LND_EFFECT_DISTORTION) {
            float drive = 4 * FX(s, DRIVE);
            for (uint32_t i = 0; i < s->selected_count; i++) {
                uint32_t c = s->selected[i];
                float x = lnd_effect_clean(out[c] + FX(s, FEEDBACK) * d->previous[c]);
                float wet = (1 + drive) * x / (1 + drive * fabsf(x));
                d->previous[c] = lnd_effect_clean(wet);
                out[c] = (out[c] * FX(s, DRY) + wet * FX(s, WET)) * FX(s, OUTPUT_GAIN);
            }
            continue;
        }
        float peak = 0;
        for (uint32_t i = 0; i < s->selected_count; i++)
            peak = LND_MAX(peak, fabsf(out[s->selected[i]]));
        float gain;
        if (s->type == LND_EFFECT_COMPRESSOR) {
            float target = peak > d->threshold ? powf(d->threshold / peak, d->exponent) : 1;
            float coefficient = target < d->gain ? d->attack : d->release;
            d->gain = target + (d->gain - target) * coefficient;
            gain = d->gain * d->makeup;
        } else {
            d->envelope = LND_MAX(peak, d->envelope * d->release);
            if (d->envelope > FX(s, QUIET_THRESHOLD) && d->envelope > 1e-12f) {
                float target = LND_MIN(FX(s, TARGET_PEAK) / d->envelope, 1e6f);
                if (target < d->gain) {
                    d->hold = d->delay;
                    d->gain += (target - d->gain) * LND_MIN(1, 10 * d->adjustment);
                } else if (d->hold) {
                    d->hold--;
                } else {
                    d->gain += (target - d->gain) * d->adjustment;
                }
            } else {
                d->hold = d->delay;
            }
            gain = d->gain;
        }
        for (uint32_t i = 0; i < s->selected_count; i++)
            out[s->selected[i]] *= gain;
    }
}

static uint64_t tail(const lnd_effect *s) {
    if (s->type != LND_EFFECT_DISTORTION || !FX(s, WET) || !FX(s, FEEDBACK)) return 0;
    return lnd_effect_decay(s, 1, (1 + 4 * FX(s, DRIVE)) * FX(s, FEEDBACK));
}

const lnd_effect_ops lnd_effect_dynamics_ops = {.init = init, .update = update, .reset = reset, .process = process, .tail = tail};
