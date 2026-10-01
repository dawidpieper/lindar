#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct delay_reference {
    float *ring;
    uint32_t channels, sample_rate_hz, cap, write;
    float current, target, slew, feedback, mix;
} delay_reference;
static delay_reference delay_reference_create(uint32_t channels, uint32_t rate, float maximum, float delay, float feedback, float mix) {
    delay_reference s = {.channels = channels,
                         .sample_rate_hz = rate,
                         .cap = (uint32_t)ceil((double)maximum * rate / 1000) + 2,
                         .current = (float)((double)delay * rate / 1000),
                         .feedback = feedback,
                         .mix = mix};
    s.target = s.current;
    s.slew = 1.0f - expf(-1.0f / (0.02f * rate));
    s.ring = calloc((size_t)s.cap * channels, sizeof(float));
    return s;
}
static void delay_reference_process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t rate) {
    (void)rate;
    delay_reference *s = user;
    for (uint32_t f = 0; f < frames; f++) {
        s->current += (s->target - s->current) * s->slew;
        float delay = s->current < 0 ? 0 : s->current;
        uint32_t whole = (uint32_t)delay;
        float fraction = delay - whole;
        uint32_t a = (s->write + s->cap - whole) % s->cap;
        uint32_t b = (a + s->cap - 1) % s->cap;
        for (uint32_t c = 0; c < channels; c++) {
            float first = s->ring[(size_t)a * channels + c];
            float second = s->ring[(size_t)b * channels + c];
            float delayed = first + (second - first) * fraction;
            float input = pcm[(size_t)f * channels + c];
            s->ring[(size_t)s->write * channels + c] = input + delayed * s->feedback;
            pcm[(size_t)f * channels + c] = input * (1.0f - s->mix) + delayed * s->mix;
        }
        s->write = (s->write + 1) % s->cap;
    }
}
