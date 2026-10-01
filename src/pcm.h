#pragma once

#include "lindar.h"
#include "platform.h"

size_t lnd_pcm_stride(const LND_PCM *pcm);
uint8_t *lnd_pcm_at(const LND_PCM *pcm, uint32_t channel, size_t frame);
int64_t lnd_pcm_load_integer(const uint8_t *p, int32_t format);
double lnd_pcm_load_sample(const uint8_t *p, int32_t format);
void lnd_pcm_store_integer(uint8_t *p, int32_t format, int64_t value);
void lnd_pcm_store_sample(uint8_t *p, int32_t format, double value);
bool lnd_pcm_range(const LND_PCM *pcm, size_t offset, size_t frames);
bool lnd_pcm_writable(const LND_PCM *pcm, size_t offset, size_t frames);

bool lnd_pcm_convert_float(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames);
void lnd_pcm_gain_float(const LND_PCM *pcm, size_t offset, size_t frames, uint32_t gain);
bool lnd_pcm_memory_overlaps(const LND_PCM *pcm, const void *memory, size_t bytes);

int32_t lnd_pcm_convert(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames);
int32_t lnd_pcm_silence(const LND_PCM *pcm, size_t offset, size_t frames);
