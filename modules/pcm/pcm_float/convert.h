#pragma once

#include "src/platform.h"

LND_INLINE int16_t lnd_pcm_s16(float value) {
    if (value != value) value = 0;
    value = LND_CLAMP(value, -1.0f, 32767.0f / 32768) * 32768;
    int32_t whole = (int32_t)value;
    float fraction = value - (float)whole;
    return (int16_t)(whole + (fraction >= 0.5f) - (fraction <= -0.5f));
}
