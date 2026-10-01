#pragma once

#include "src/platform.h"
#include "lindar.h"

void lnd_graph_pcm_gain(const LND_PCM *pcm, size_t offset, size_t frames, float gain);
void lnd_graph_pcm_gains(const LND_PCM *pcm, size_t offset, uint32_t frames, const float *gains);
void lnd_graph_pcm_ramp_active(const LND_PCM *pcm, size_t offset, uint32_t frames, float *gain, float step, float target, uint32_t *remaining);
LND_INLINE void lnd_graph_pcm_ramp(const LND_PCM *pcm, size_t offset, uint32_t frames, float *gain, float step, float target, uint32_t *remaining) {
    if (*remaining) lnd_graph_pcm_ramp_active(pcm, offset, frames, gain, step, target, remaining);
    else if (*gain != 1.0f) lnd_graph_pcm_gain(pcm, offset, frames, *gain);
}
