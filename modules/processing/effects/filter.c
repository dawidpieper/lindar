#include "pcm/audio/simd.h"
#include "effects.h"

typedef struct lnd_effect_filter {
    lnd_biquad_coeff coefficient;
    double z[LND_MAX_CHANNELS][2];
} lnd_effect_filter;

static int32_t init(lnd_effect *s) { return lnd_effect_allocate(s, sizeof(lnd_effect_filter), 0); }

static void update(lnd_effect *s, int32_t param) {
    LND_UNUSED(param);
    lnd_effect_filter *f = s->state;
    int32_t type = s->type == LND_EFFECT_PEAK_EQ ? LND_EFFECT_FILTER_PEAKING : (int32_t)FX(s, FILTER_TYPE);
    bool shelf = type == LND_EFFECT_FILTER_LOWSHELF || type == LND_EFFECT_FILTER_HIGHSHELF;
    double q = FX(s, Q) > 0 ? FX(s, Q) : 0.7071067811865475;
    double slope = shelf ? LND_MAX(FX(s, SLOPE), 0.0001f) : 0;
    f->coefficient = lnd_biquad_coefficients(type, s->rate, FX(s, FREQUENCY_HZ), q, FX(s, GAIN_DB), shelf ? 0 : FX(s, BANDWIDTH_OCTAVES), slope);
}

static void reset(lnd_effect *s) {
    lnd_effect_filter *f = s->state;
    memset(f->z, 0, sizeof f->z);
}

static void process(lnd_effect *s, float *pcm, uint32_t frames) {
    lnd_effect_filter *f = s->state;
    lnd_biquad_coeff k = f->coefficient;
    for (uint32_t i = 0; i < s->selected_count; i++) {
        uint32_t c = s->selected[i];
        if (lnd_simd.biquad_f64 && frames >= 128 && i + 1 < s->selected_count && s->selected[i + 1] == c + 1) {
            lnd_simd.biquad_f64(pcm + c, frames, s->channels, &k, f->z + c);
            i++;
            continue;
        }
        double z1 = f->z[c][0], z2 = f->z[c][1];
        for (uint32_t n = 0; n < frames; n++) {
            float *out = pcm + (size_t)n * s->channels + c;
            double x = *out, y = k.b0 * x + z1;
            z1 = k.b1 * x - k.a1 * y + z2;
            z2 = k.b2 * x - k.a2 * y;
            *out = (float)y;
        }
        f->z[c][0] = fabs(z1) < 1e-30 ? 0 : z1;
        f->z[c][1] = fabs(z2) < 1e-30 ? 0 : z2;
    }
}

static uint64_t tail(const lnd_effect *s) {
    const lnd_effect_filter *f = s->state;
    for (uint32_t i = 0; i < s->selected_count; i++) {
        if (f->z[s->selected[i]][0] == 0 && f->z[s->selected[i]][1] == 0) continue;
        double a1 = f->coefficient.a1, a2 = f->coefficient.a2;
        double discriminant = a1 * a1 - 4 * a2;
        double radius = discriminant < 0 ? sqrt(fabs(a2)) : (fabs(a1) + sqrt(discriminant)) * 0.5;
        return lnd_effect_decay(s, 1, radius);
    }
    return 0;
}

const lnd_effect_ops lnd_effect_filter_ops = {.init = init, .update = update, .reset = reset, .process = process, .tail = tail};
