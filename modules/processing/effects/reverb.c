#include "effects.h"

typedef struct lnd_reverb_line {
    uint32_t offset, length, position;
    float lowpass;
} lnd_reverb_line;

typedef struct lnd_effect_reverb {
    lnd_reverb_line line[LND_MAX_CHANNELS][12];
    float feedback, damping, wet_direct, wet_cross, input_gain;
    uint32_t longest;
} lnd_effect_reverb;

static const uint16_t lengths[12] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617, 556, 441, 341, 225};

static int32_t init(lnd_effect *s) {
    size_t samples = 0;
    for (uint32_t c = 0; c < s->channels; c++)
        for (uint32_t i = 0; i < 12; i++)
            samples += LND_MAX(1, (uint32_t)lround((lengths[i] + (c & 1 ? 23 : 0)) * (double)s->rate / 44100));
    int32_t result = lnd_effect_allocate(s, sizeof(lnd_effect_reverb), samples);
    if (result != LND_OK) return result;
    lnd_effect_reverb *r = s->state;
    uint32_t offset = 0;
    for (uint32_t c = 0; c < s->channels; c++)
        for (uint32_t i = 0; i < 12; i++) {
            lnd_reverb_line *line = &r->line[c][i];
            line->length = LND_MAX(1, (uint32_t)lround((lengths[i] + (c & 1 ? 23 : 0)) * (double)s->rate / 44100));
            line->offset = offset;
            offset += line->length;
            r->longest = LND_MAX(r->longest, line->length);
        }
    return LND_OK;
}

static void update(lnd_effect *s, int32_t param) {
    LND_UNUSED(param);
    lnd_effect_reverb *r = s->state;
    r->feedback = FX(s, FREEZE) ? 1 : 0.7f + 0.28f * FX(s, ROOM_SIZE);
    r->damping = FX(s, FREEZE) ? 0 : 0.4f * FX(s, DAMPING);
    r->input_gain = FX(s, FREEZE) ? 0 : 0.015f;
    r->wet_direct = FX(s, WET) * (1 + FX(s, WIDTH)) * 0.5f;
    r->wet_cross = FX(s, WET) * (1 - FX(s, WIDTH)) * 0.5f;
}

static void reset(lnd_effect *s) {
    lnd_effect_reverb *r = s->state;
    for (uint32_t c = 0; c < s->channels; c++)
        for (uint32_t i = 0; i < 12; i++) {
            r->line[c][i].position = 0;
            r->line[c][i].lowpass = 0;
        }
}

static float reverberate(lnd_effect *s, uint32_t channel, float input) {
    lnd_effect_reverb *r = s->state;
    float sum = 0;
    for (uint32_t i = 0; i < 12; i++) {
        lnd_reverb_line *line = &r->line[channel][i];
        float *cell = s->memory + line->offset + line->position;
        float delayed = *cell;
        if (i < 8) {
            line->lowpass = lnd_effect_clean(delayed + (line->lowpass - delayed) * r->damping);
            *cell = lnd_effect_clean(input + line->lowpass * r->feedback);
            sum += delayed;
        } else {
            *cell = lnd_effect_clean(sum + delayed * 0.5f);
            sum = delayed - sum;
        }
        if (++line->position == line->length) line->position = 0;
    }
    return sum;
}

static void process(lnd_effect *s, float *pcm, uint32_t frames) {
    lnd_effect_reverb *r = s->state;
    for (uint32_t f = 0; f < frames; f++) {
        float *out = pcm + (size_t)f * s->channels;
        for (uint32_t c = 0; c < s->channels; c += 2) {
            bool left = !!(s->mask & (UINT32_C(1) << c));
            bool right = c + 1 < s->channels && !!(s->mask & (UINT32_C(1) << (c + 1)));
            if (!left && !right) continue;
            float input = ((left ? out[c] : 0) + (right ? out[c + 1] : 0)) * r->input_gain;
            float a = left ? reverberate(s, c, input) : 0;
            float b = right ? reverberate(s, c + 1, input) : 0;
            if (left) out[c] = out[c] * FX(s, DRY) + (right ? a * r->wet_direct + b * r->wet_cross : a * FX(s, WET));
            if (right) out[c + 1] = out[c + 1] * FX(s, DRY) + (left ? b * r->wet_direct + a * r->wet_cross : b * FX(s, WET));
        }
    }
}

static uint64_t tail(const lnd_effect *s) {
    if (!FX(s, WET)) return 0;
    const lnd_effect_reverb *r = s->state;
    return LND_MIN(s->tail_limit, lnd_effect_decay(s, r->longest, r->feedback) + s->rate / 2);
}

const lnd_effect_ops lnd_effect_reverb_ops = {.init = init, .update = update, .reset = reset, .process = process, .tail = tail};
