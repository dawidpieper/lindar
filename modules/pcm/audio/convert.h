#pragma once

#include "src/platform.h"

void lnd_pcm_to_f32(int32_t format, const void *src, float *dst, size_t samples);
void lnd_pcm_from_f32(int32_t format, const float *src, void *dst, size_t samples);
