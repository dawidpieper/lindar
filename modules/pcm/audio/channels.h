#pragma once

#include "lindar_audio.h"

#include "src/platform.h"

void lnd_channels_map(const float *src, uint32_t src_channels, float *dst, uint32_t dst_channels, size_t frames);
void lnd_channels_matrix(uint32_t src_channels, uint32_t dst_channels, int32_t mode, float *matrix);
void lnd_channels_apply(const float *matrix, const float *src, uint32_t src_channels, float *dst, uint32_t dst_channels, size_t frames);
