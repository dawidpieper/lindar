#pragma once

#include "src/platform.h"
#include <math.h>

enum {
    LND_IIR_LOWPASS,
    LND_IIR_HIGHPASS,
    LND_IIR_BANDPASS,
    LND_IIR_NOTCH,
    LND_IIR_PEAKING,
    LND_IIR_LOWSHELF,
    LND_IIR_HIGHSHELF,
    LND_IIR_ALLPASS,
    LND_IIR_BANDPASS_Q
};

typedef struct lnd_biquad_coeff_f32 {
    float b0, b1, b2, a1, a2;
} lnd_biquad_coeff_f32;

typedef struct lnd_biquad_coeff {
    double b0, b1, b2, a1, a2;
} lnd_biquad_coeff;

LND_INLINE lnd_biquad_coeff lnd_biquad_coefficients(int32_t type, double rate, double frequency, double q, double gain_db, double bandwidth, double slope) {
    double f = LND_CLAMP(frequency, 0.001, rate * 0.49);
    double a = pow(10, gain_db / 40);
    double w = 6.2831853071795864769 * f / rate;
    double cw = cos(w), sw = sin(w);
    double alpha = sw / (2 * LND_MAX(q, 0.0001));
    if (slope > 0 && (type == LND_IIR_LOWSHELF || type == LND_IIR_HIGHSHELF))
        alpha = sw * 0.5 * sqrt((a + 1 / a) * (1 / slope - 1) + 2);
    else if (bandwidth > 0)
        alpha = sw * sinh(0.34657359027997265471 * bandwidth * w / sw);
    double b0, b1, b2, a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha;
    switch (type) {
    case LND_IIR_HIGHPASS:
        b0 = (1 + cw) / 2;
        b1 = -(1 + cw);
        b2 = b0;
        break;
    case LND_IIR_BANDPASS:
        b0 = alpha;
        b1 = 0;
        b2 = -alpha;
        break;
    case LND_IIR_NOTCH:
        b0 = 1;
        b1 = -2 * cw;
        b2 = 1;
        break;
    case LND_IIR_PEAKING:
        b0 = 1 + alpha * a;
        b1 = -2 * cw;
        b2 = 1 - alpha * a;
        a0 = 1 + alpha / a;
        a2 = 1 - alpha / a;
        break;
    case LND_IIR_LOWSHELF: {
        double root = 2 * sqrt(a) * alpha;
        b0 = a * ((a + 1) - (a - 1) * cw + root);
        b1 = 2 * a * ((a - 1) - (a + 1) * cw);
        b2 = a * ((a + 1) - (a - 1) * cw - root);
        a0 = (a + 1) + (a - 1) * cw + root;
        a1 = -2 * ((a - 1) + (a + 1) * cw);
        a2 = (a + 1) + (a - 1) * cw - root;
        break;
    }
    case LND_IIR_HIGHSHELF: {
        double root = 2 * sqrt(a) * alpha;
        b0 = a * ((a + 1) + (a - 1) * cw + root);
        b1 = -2 * a * ((a - 1) + (a + 1) * cw);
        b2 = a * ((a + 1) + (a - 1) * cw - root);
        a0 = (a + 1) - (a - 1) * cw + root;
        a1 = 2 * ((a - 1) - (a + 1) * cw);
        a2 = (a + 1) - (a - 1) * cw - root;
        break;
    }
    case LND_IIR_ALLPASS:
        b0 = 1 - alpha;
        b1 = -2 * cw;
        b2 = 1 + alpha;
        break;
    case LND_IIR_BANDPASS_Q:
        b0 = sw / 2;
        b1 = 0;
        b2 = -b0;
        break;
    case LND_IIR_LOWPASS:
    default:
        b0 = (1 - cw) / 2;
        b1 = 1 - cw;
        b2 = b0;
        break;
    }
    return (lnd_biquad_coeff){b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}
