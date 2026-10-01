#pragma once

#include "src/alloc.h"
#include "playback/graph/node.h"
#include "lindar_effects.h"

#include <math.h>
#include <string.h>

enum { LND_EFFECT_PARAMS = LND_EFFECT_PARAM_END - LND_PARAM_USER };

typedef struct lnd_effect lnd_effect;

typedef struct lnd_effect_ops {
    int32_t (*init)(lnd_effect *s);
    void (*update)(lnd_effect *s, int32_t param);
    void (*reset)(lnd_effect *s);
    void (*process)(lnd_effect *s, float *pcm, uint32_t frames);
    uint64_t (*tail)(const lnd_effect *s);
} lnd_effect_ops;

struct lnd_effect {
    const lnd_effect_ops *ops;
    const lnd_node_vt *processor;
    void *state;
    float *memory;
    size_t memory_count;
    float p[LND_EFFECT_PARAMS];
    float max_delay_ms;
    uint32_t channels, rate, mask, selected_count;
    uint8_t selected[LND_MAX_CHANNELS];
    uint64_t input_frames, output_frames, tail_limit, tail_remaining;
    int32_t type, error;
    bool ended, signal, no_tail;
};

#define FX(s, name) ((s)->p[LND_EFFECT_PARAM_##name - LND_PARAM_USER])

extern const lnd_effect_ops lnd_effect_delay_ops;
extern const lnd_effect_ops lnd_effect_reverb_ops;
extern const lnd_effect_ops lnd_effect_modulation_ops;
extern const lnd_effect_ops lnd_effect_dynamics_ops;
extern const lnd_effect_ops lnd_effect_filter_ops;

int32_t lnd_effect_allocate(lnd_effect *s, size_t state_bytes, size_t samples);
uint64_t lnd_effect_decay(const lnd_effect *s, double delay_frames, double feedback);

LND_INLINE float lnd_effect_clean(float x) { return fabsf(x) < 1e-30f ? 0 : LND_CLAMP(x, -1e12f, 1e12f); }

typedef struct lnd_effect_lfo {
    double sine, cosine, step_sine, step_cosine;
    uint32_t ticks;
} lnd_effect_lfo;

LND_INLINE void lnd_effect_lfo_rate(lnd_effect_lfo *lfo, double hz, uint32_t rate) {
    double angle = 6.2831853071795864769 * hz / rate;
    lfo->step_sine = sin(angle);
    lfo->step_cosine = cos(angle);
}

LND_INLINE float lnd_effect_lfo_next(lnd_effect_lfo *lfo) {
    double value = lfo->sine;
    double sine = lfo->sine * lfo->step_cosine + lfo->cosine * lfo->step_sine;
    lfo->cosine = lfo->cosine * lfo->step_cosine - lfo->sine * lfo->step_sine;
    lfo->sine = sine;
    if (++lfo->ticks == 4096) {
        double norm = 1 / hypot(lfo->sine, lfo->cosine);
        lfo->sine *= norm;
        lfo->cosine *= norm;
        lfo->ticks = 0;
    }
    return (float)value;
}
