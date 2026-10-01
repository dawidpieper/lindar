#pragma once
#include "lindar_codecs.h"
bool lnd_ff_container_probe(const uint8_t *data, size_t bytes);
void *lnd_ff_container_create(void);
void lnd_ff_container_close(void *state);
int32_t lnd_ff_container_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info);
int32_t lnd_ff_container_metadata(void *state, LND_METADATA *metadata, uint64_t *revision);
