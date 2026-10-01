#include "channels.h"
#include "simd.h"
#include "lindar.h"

#include <math.h>
#include <string.h>

enum {
    LND_R_FL,
    LND_R_FR,
    LND_R_FC,
    LND_R_LFE,
    LND_R_BL,
    LND_R_BR,
    LND_R_SL,
    LND_R_SR,
    LND_R_BC,
    LND_R_NONE,
};

static const uint8_t lnd_layouts[9][8] = {
    [1] = {LND_R_FC},
    [2] = {LND_R_FL, LND_R_FR},
    [3] = {LND_R_FL, LND_R_FR, LND_R_FC},
    [4] = {LND_R_FL, LND_R_FR, LND_R_BL, LND_R_BR},
    [5] = {LND_R_FL, LND_R_FR, LND_R_FC, LND_R_BL, LND_R_BR},
    [6] = {LND_R_FL, LND_R_FR, LND_R_FC, LND_R_LFE, LND_R_BL, LND_R_BR},
    [7] = {LND_R_FL, LND_R_FR, LND_R_FC, LND_R_LFE, LND_R_BC, LND_R_SL, LND_R_SR},
    [8] = {LND_R_FL, LND_R_FR, LND_R_FC, LND_R_LFE, LND_R_BL, LND_R_BR, LND_R_SL, LND_R_SR},
};

#define LND_SQRT_HALF 0.70710678f

void lnd_channels_map(const float *LND_RESTRICT src, uint32_t sc, float *LND_RESTRICT dst, uint32_t dc, size_t frames) {
    if (sc == dc) {
        memcpy(dst, src, frames * sc * sizeof(float));
        return;
    }
    if (sc == 1) {
        for (size_t f = 0; f < frames; f++) {
            float v = src[f];
            for (uint32_t c = 0; c < dc; c++)
                dst[f * dc + c] = v;
        }
        return;
    }
    if (dc == 1) {
        float scale = 1.0f / (float)sc;
        for (size_t f = 0; f < frames; f++) {
            float sum = 0.0f;
            for (uint32_t c = 0; c < sc; c++)
                sum += src[f * sc + c];
            dst[f] = sum * scale;
        }
        return;
    }
    uint32_t n = LND_MIN(sc, dc);
    for (size_t f = 0; f < frames; f++) {
        for (uint32_t c = 0; c < n; c++)
            dst[f * dc + c] = src[f * sc + c];
        for (uint32_t c = n; c < dc; c++)
            dst[f * dc + c] = 0.0f;
    }
}

static int lnd_role_index(const uint8_t *layout, uint32_t n, int role) {
    for (uint32_t i = 0; i < n; i++) {
        if (layout[i] == role) return (int)i;
    }
    return -1;
}

static void lnd_matrix_simple(uint32_t sc, uint32_t dc, float *m) {
    if (sc == dc) {
        for (uint32_t c = 0; c < sc; c++)
            m[c * sc + c] = 1.0f;
    } else if (sc == 1) {
        for (uint32_t d = 0; d < dc; d++)
            m[d] = 1.0f;
    } else if (dc == 1) {
        for (uint32_t s = 0; s < sc; s++)
            m[s] = 1.0f / (float)sc;
    } else {
        for (uint32_t c = 0; c < LND_MIN(sc, dc); c++)
            m[c * sc + c] = 1.0f;
    }
}

static void lnd_matrix_add(float *m, uint32_t sc, const uint8_t *dl, uint32_t dc, int role, uint32_t s, float gain) {
    int d = lnd_role_index(dl, dc, role);
    if (d >= 0) m[(uint32_t)d * sc + s] += gain;
}

void lnd_channels_matrix(uint32_t sc, uint32_t dc, int32_t mode, float *m) {
    memset(m, 0, (size_t)sc * dc * sizeof(float));
    if (mode == LND_CHANNEL_MIX_SIMPLE || sc == 0 || dc == 0 || sc > 8 || dc > 8) {
        lnd_matrix_simple(sc, dc, m);
        return;
    }
    const uint8_t *sl = lnd_layouts[sc];
    const uint8_t *dl = lnd_layouts[dc];
    bool mono = dc == 1;
    float front_to_mono = sc == 4 ? 0.25f : (sc <= 3 ? 0.5f : LND_SQRT_HALF);
    float surround_to_mono = sc == 4 ? 0.25f : 0.5f;
    float surround_to_front = sc == 4 ? 0.5f : LND_SQRT_HALF;
    for (uint32_t s = 0; s < sc; s++) {
        int role = sl[s];
        int d = lnd_role_index(dl, dc, role);
        if (d >= 0) {
            m[(uint32_t)d * sc + s] = 1.0f;
            continue;
        }
        switch (role) {
        case LND_R_FL:
        case LND_R_FR:
            lnd_matrix_add(m, sc, dl, dc, LND_R_FC, s, front_to_mono);
            break;
        case LND_R_FC:
            if (mono) {
                lnd_matrix_add(m, sc, dl, dc, LND_R_FC, s, 1.0f);
            } else {
                float k = sc == 1 ? 1.0f : LND_SQRT_HALF;
                lnd_matrix_add(m, sc, dl, dc, LND_R_FL, s, k);
                lnd_matrix_add(m, sc, dl, dc, LND_R_FR, s, k);
            }
            break;
        case LND_R_LFE:
            break;
        case LND_R_BL:
        case LND_R_SL:
            if (lnd_role_index(dl, dc, role == LND_R_BL ? LND_R_SL : LND_R_BL) >= 0)
                lnd_matrix_add(m, sc, dl, dc, role == LND_R_BL ? LND_R_SL : LND_R_BL, s, 1.0f);
            else if (mono)
                lnd_matrix_add(m, sc, dl, dc, LND_R_FC, s, surround_to_mono);
            else
                lnd_matrix_add(m, sc, dl, dc, LND_R_FL, s, surround_to_front);
            break;
        case LND_R_BR:
        case LND_R_SR:
            if (lnd_role_index(dl, dc, role == LND_R_BR ? LND_R_SR : LND_R_BR) >= 0)
                lnd_matrix_add(m, sc, dl, dc, role == LND_R_BR ? LND_R_SR : LND_R_BR, s, 1.0f);
            else if (mono)
                lnd_matrix_add(m, sc, dl, dc, LND_R_FC, s, surround_to_mono);
            else
                lnd_matrix_add(m, sc, dl, dc, LND_R_FR, s, surround_to_front);
            break;
        case LND_R_BC:
            if (lnd_role_index(dl, dc, LND_R_BL) >= 0) {
                lnd_matrix_add(m, sc, dl, dc, LND_R_BL, s, LND_SQRT_HALF);
                lnd_matrix_add(m, sc, dl, dc, LND_R_BR, s, LND_SQRT_HALF);
            } else if (lnd_role_index(dl, dc, LND_R_SL) >= 0) {
                lnd_matrix_add(m, sc, dl, dc, LND_R_SL, s, LND_SQRT_HALF);
                lnd_matrix_add(m, sc, dl, dc, LND_R_SR, s, LND_SQRT_HALF);
            } else if (mono) {
                lnd_matrix_add(m, sc, dl, dc, LND_R_FC, s, 0.5f);
            } else {
                lnd_matrix_add(m, sc, dl, dc, LND_R_FL, s, 0.5f);
                lnd_matrix_add(m, sc, dl, dc, LND_R_FR, s, 0.5f);
            }
            break;
        default:
            break;
        }
    }
}

/* Separate kernels preserve measured code generation; keep the generic zero accumulator. */
static LND_NOINLINE void lnd_channels_mono_stereo(const float *LND_RESTRICT m, const float *LND_RESTRICT src, float *LND_RESTRICT dst, size_t frames) {
    for (size_t f = 0; f < frames; f++) {
        dst[f * 2] = 0.0f + m[0] * src[f];
        dst[f * 2 + 1] = 0.0f + m[1] * src[f];
    }
}

static LND_NOINLINE void lnd_channels_stereo_mono(const float *LND_RESTRICT m, const float *LND_RESTRICT src, float *LND_RESTRICT dst, size_t frames) {
    for (size_t f = 0; f < frames; f++) {
        float acc = 0.0f;
        acc += m[0] * src[f * 2];
        acc += m[1] * src[f * 2 + 1];
        dst[f] = acc;
    }
}

void lnd_channels_apply(const float *LND_RESTRICT m, const float *LND_RESTRICT src, uint32_t sc, float *LND_RESTRICT dst, uint32_t dc, size_t frames) {
    if (sc == 2 && dc == 2) {
        lnd_simd.matrix_stereo(src, dst, frames, m);
        return;
    }
    if (sc == 1 && dc == 2) {
        lnd_channels_mono_stereo(m, src, dst, frames);
        return;
    }
    if (sc == 2 && dc == 1) {
        lnd_channels_stereo_mono(m, src, dst, frames);
        return;
    }
    for (size_t f = 0; f < frames; f++) {
        const float *in = src + f * sc;
        float *out = dst + f * dc;
        for (uint32_t d = 0; d < dc; d++) {
            const float *row = m + d * sc;
            float acc = 0.0f;
            for (uint32_t s = 0; s < sc; s++)
                acc += row[s] * in[s];
            out[d] = acc;
        }
    }
}
