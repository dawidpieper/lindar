#pragma once

#include "src/platform.h"

extern const int16_t lnd_adpcm_coefficients[7][2];
uint32_t lnd_adpcm_block_frames(uint32_t tag, uint32_t channels, uint32_t bytes);
bool lnd_adpcm_decode_block(uint32_t tag, uint32_t channels, const uint8_t *block, uint32_t frames, const int16_t (*coef)[2], uint32_t coefficients,
                            int16_t *pcm);
void lnd_adpcm_encode_block(uint32_t tag, uint32_t channels, const int16_t *pcm, uint32_t frames, uint8_t *block);
