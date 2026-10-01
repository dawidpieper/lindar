#pragma once

#include "src/platform.h"

typedef void (*lnd_asio_pcm_read)(const void *, float *, uint32_t, uint32_t);
typedef void (*lnd_asio_pcm_write)(const float *, void *, uint32_t, uint32_t);

typedef struct lnd_asio_pcm {
    uint32_t bytes;
    uint32_t bits;
    bool floating_point;
    bool big_endian;
    lnd_asio_pcm_read read;
    lnd_asio_pcm_write write;
} lnd_asio_pcm;

bool lnd_asio_pcm_get(int32_t type, lnd_asio_pcm *pcm);
int32_t lnd_asio_buffer_size(uint32_t requested, int32_t minimum, int32_t maximum, int32_t preferred, int32_t granularity, uint32_t *frames);
