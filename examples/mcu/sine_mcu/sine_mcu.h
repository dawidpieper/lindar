#pragma once

#include "lindar.h"

typedef struct sine_mcu {
    LND_SOURCE *source;
    LND_SOUND *sound;
    LND_RENDERER *renderer;
    uint32_t phase;
    uint32_t step;
} sine_mcu;

int32_t sine_mcu_configure(void);
size_t sine_mcu_memory_size(void);
int32_t sine_mcu_init(sine_mcu *sine, void *memory, size_t bytes, uint32_t sample_rate_hz, uint32_t frequency);
int32_t sine_mcu_fill(sine_mcu *sine, int16_t *buffer, size_t frames);
int32_t sine_mcu_free(sine_mcu *sine);
