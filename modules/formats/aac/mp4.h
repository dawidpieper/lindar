#pragma once

#include "src/platform.h"
#include "io/io.h"

typedef struct lnd_mp4_mux {
    lnd_io *io;
    uint64_t mdat_pos;
    uint32_t *sizes;
    uint32_t count;
    uint32_t cap;
    uint64_t bytes;
    uint32_t sample_rate_hz;
    uint32_t channels;
    uint32_t frame_length;
    uint32_t delay;
    uint32_t bitrate_bps;
    uint8_t asc[64];
    uint32_t asc_len;
} lnd_mp4_mux;

int32_t lnd_mp4_begin(lnd_mp4_mux *m, lnd_io *io, uint32_t sample_rate_hz, uint32_t channels, uint32_t frame_length, uint32_t delay, uint32_t bitrate_bps, const uint8_t *asc,
                      uint32_t asc_len);
int32_t lnd_mp4_sample(lnd_mp4_mux *m, const void *data, uint32_t size);
int32_t lnd_mp4_finish(lnd_mp4_mux *m, uint64_t pcm_frames);
void lnd_mp4_free(lnd_mp4_mux *m);
