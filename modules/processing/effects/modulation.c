#include "effects.h"

typedef struct lnd_effect_modulation {
    lnd_effect_lfo lfo;
    float z[LND_MAX_CHANNELS][6];
    float previous[LND_MAX_CHANNELS];
    float coefficient, increment;
    uint32_t remaining;
} lnd_effect_modulation;

static float coefficient(const lnd_effect *s, float wave) {
    double frequency = FX(s, FREQUENCY_HZ) * exp2((wave + 1) * 0.5 * FX(s, RANGE_OCTAVES));
    double g = tan(3.14159265358979323846 * LND_MIN(frequency, s->rate * 0.45) / s->rate);
    return (float)(s->type == LND_EFFECT_PHASER ? (g - 1) / (g + 1) : g);
}

static int32_t init(lnd_effect *s) { return lnd_effect_allocate(s, sizeof(lnd_effect_modulation), 0); }

static void update(lnd_effect *s, int32_t param) {
    lnd_effect_modulation *m = s->state;
    if (param == -1 || param == LND_EFFECT_PARAM_RATE_HZ) lnd_effect_lfo_rate(&m->lfo, FX(s, RATE_HZ), s->rate);
    if (param == -1 || param == LND_EFFECT_PARAM_FREQUENCY_HZ || param == LND_EFFECT_PARAM_RANGE_OCTAVES) m->remaining = 0;
}

static void reset(lnd_effect *s) {
    lnd_effect_modulation *m = s->state;
    memset(m, 0, sizeof *m);
    m->lfo.cosine = 1;
    if (s->type != LND_EFFECT_ROTATION) m->coefficient = coefficient(s, 0);
}

static void process(lnd_effect *s, float *pcm, uint32_t frames) {
    lnd_effect_modulation *m = s->state;
    for (uint32_t f = 0; f < frames; f++) {
        float *out = pcm + (size_t)f * s->channels;
        float wave = lnd_effect_lfo_next(&m->lfo);
        if (s->type == LND_EFFECT_ROTATION) {
            for (uint32_t i = 0; i < s->selected_count; i++) {
                uint32_t c = s->selected[i];
                if ((c ^ 1u) < s->channels) out[c] *= 0.5f + (c & 1 ? -0.5f : 0.5f) * wave;
            }
            continue;
        }
        if (!m->remaining) {
            m->increment = (coefficient(s, wave) - m->coefficient) / 16;
            m->remaining = 16;
        }
        m->coefficient += m->increment;
        m->remaining--;
        float g = m->coefficient;
        float normalization = s->type == LND_EFFECT_AUTOWAH ? 1 / (1 + g * (g + 1.41421356237f)) : 0;
        for (uint32_t i = 0; i < s->selected_count; i++) {
            uint32_t c = s->selected[i];
            float x = lnd_effect_clean(out[c] + FX(s, FEEDBACK) * m->previous[c]), y;
            float *z = m->z[c];
            if (s->type == LND_EFFECT_PHASER) {
                y = x;
                for (uint32_t stage = 0; stage < 6; stage++) {
                    float v = g * y + z[stage];
                    z[stage] = lnd_effect_clean(y - g * v);
                    y = v;
                }
            } else {
                float band = (z[0] + g * (x - z[1])) * normalization;
                float low = z[1] + g * band;
                z[0] = lnd_effect_clean(2 * band - z[0]);
                z[1] = lnd_effect_clean(2 * low - z[1]);
                y = band * 1.41421356237f;
            }
            m->previous[c] = lnd_effect_clean(y);
            out[c] = out[c] * FX(s, DRY) + y * FX(s, WET);
        }
    }
}

static uint64_t tail(const lnd_effect *s) {
    if (s->type == LND_EFFECT_ROTATION || !FX(s, WET)) return 0;
    double base = LND_MAX(0.02, 8 / LND_MAX(FX(s, FREQUENCY_HZ), 0.001f));
    return lnd_effect_decay(s, base * s->rate, FX(s, FEEDBACK));
}

const lnd_effect_ops lnd_effect_modulation_ops = {.init = init, .update = update, .reset = reset, .process = process, .tail = tail};
