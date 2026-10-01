#pragma once

#include "src/platform.h"

enum {
    LND_RIFF_PHYSICAL = 1,
    LND_RIFF_CLAMP_CONTAINER = 2,
    LND_RIFF_CLAMP_DATA = 4,
    LND_RIFF_ALLOW_MISSING_PAD = 8,
};

typedef struct lnd_riff {
    uint64_t end, physical, data_size;
    uint32_t policy;
    bool rf64, ds64;
} lnd_riff;

typedef struct lnd_riff_chunk {
    uint64_t body, size, next;
} lnd_riff_chunk;

int32_t lnd_riff_open(const uint8_t header[12], uint64_t physical, uint32_t policy, lnd_riff *riff);
int32_t lnd_riff_ds64(lnd_riff *riff, const uint8_t *data, size_t bytes, uint64_t chunk_size);
int32_t lnd_riff_next(const lnd_riff *riff, const uint8_t header[8], uint64_t *position, lnd_riff_chunk *chunk);
