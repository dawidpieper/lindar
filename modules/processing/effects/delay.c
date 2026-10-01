#include "effects.h"

typedef struct lnd_effect_delay {
    uint32_t capacity, write;
    double phase;
    double current;
    float slew;
} lnd_effect_delay;

static int32_t init(lnd_effect *s) {
    double capacity = ceil((double)s->max_delay_ms * s->rate / 1000) + 2;
    if (capacity > UINT32_MAX || capacity > SIZE_MAX / sizeof(float) / s->channels) return LND_ERR_OUT_OF_MEMORY;
    int32_t result = lnd_effect_allocate(s, sizeof(lnd_effect_delay), (size_t)capacity * s->channels);
    if (result == LND_OK) {
        lnd_effect_delay *d = s->state;
        d->capacity = (uint32_t)capacity;
        d->slew = (float)(1 - exp(-1 / (0.02 * s->rate)));
    }
    return result;
}

static void update(lnd_effect *s, int32_t param) {
    LND_UNUSED(s);
    LND_UNUSED(param);
}

static void reset(lnd_effect *s) {
    lnd_effect_delay *d = s->state;
    d->write = 0;
    d->phase = 0;
    d->current = LND_MAX(1, (double)FX(s, DELAY_MS) * s->rate / 1000);
}

static void process(lnd_effect *s, float *pcm, uint32_t frames) {
    lnd_effect_delay *d = s->state;
    bool echo = s->type == LND_EFFECT_ECHO;
    float low = LND_MIN(FX(s, MIN_DELAY_MS), FX(s, MAX_DELAY_MS));
    float span = fabsf(FX(s, MAX_DELAY_MS) - FX(s, MIN_DELAY_MS));
    double step = span > 0 ? FX(s, SWEEP_MS_PER_SECOND) / ((double)span * s->rate) : 0;
    double target = LND_MAX(1, (double)FX(s, DELAY_MS) * s->rate / 1000);
    float wet[LND_MAX_CHANNELS];
    for (uint32_t f = 0; f < frames; f++) {
        double delay;
        if (echo) {
            d->current += (target - d->current) * d->slew;
            delay = d->current;
        } else {
            delay = (low + span * (1 - fabs(d->phase - 1))) * s->rate / 1000;
            d->phase += step;
            if (d->phase >= 2) d->phase -= 2 * floor(d->phase * 0.5);
        }
        delay = LND_CLAMP(delay, 1, d->capacity - 2);
        uint32_t whole = (uint32_t)delay;
        float fraction = (float)(delay - whole);
        uint32_t at = d->write >= whole ? d->write - whole : d->capacity - (whole - d->write);
        uint32_t previous = at ? at - 1 : d->capacity - 1;
        float *out = pcm + (size_t)f * s->channels;
        for (uint32_t i = 0; i < s->selected_count; i++) {
            uint32_t c = s->selected[i];
            const float *ring = s->memory + (size_t)c * d->capacity;
            wet[c] = ring[at] + (ring[previous] - ring[at]) * fraction;
        }
        for (uint32_t i = 0; i < s->selected_count; i++) {
            uint32_t c = s->selected[i], source = c;
            if (echo && FX(s, STEREO) && (c ^ 1u) < s->channels && (s->mask & (UINT32_C(1) << (c ^ 1u)))) source ^= 1u;
            float x = out[c], delayed = wet[source];
            s->memory[(size_t)c * d->capacity + d->write] = lnd_effect_clean(x + delayed * FX(s, FEEDBACK));
            out[c] = x * FX(s, DRY) + delayed * FX(s, WET);
        }
        if (++d->write == d->capacity) d->write = 0;
    }
}

static uint64_t tail(const lnd_effect *s) {
    if (!FX(s, WET)) return 0;
    const lnd_effect_delay *d = s->state;
    double ms = s->type == LND_EFFECT_ECHO ? LND_MAX(FX(s, DELAY_MS), d->current * 1000 / s->rate) : LND_MAX(FX(s, MIN_DELAY_MS), FX(s, MAX_DELAY_MS));
    return lnd_effect_decay(s, LND_MAX(1, ms * s->rate / 1000), FX(s, FEEDBACK));
}

const lnd_effect_ops lnd_effect_delay_ops = {.init = init, .update = update, .reset = reset, .process = process, .tail = tail};
