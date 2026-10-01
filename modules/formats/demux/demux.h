#pragma once

#include "lindar_demux.h"
#include "src/platform.h"
#include "src/alloc.h"
#include "src/error.h"

typedef struct lnd_demux_mp4_sample {
    uint64_t offset;
    uint32_t bytes;
    uint32_t duration;
    int64_t time;
} lnd_demux_mp4_sample;

struct LND_DEMUX {
    LND_DEMUX_OPTIONS options;
    const uint8_t *data;
    size_t bytes;
    uint64_t file_offset;
    size_t position;
    int32_t kind;
    int32_t error;
    uint8_t *packet;
    size_t packet_size;
    size_t packet_capacity;
    uint16_t pmt_pid;
    uint16_t audio_pid;
    uint8_t stream_type;
    uint8_t continuity;
    bool continuity_valid;
    bool packet_pending;
    bool ended;
    bool discontinuity;
    uint8_t section[4096];
    size_t section_size;
    uint16_t section_pid;
    uint64_t previous_pts;
    int64_t wrap;
    int64_t pts;
    uint32_t track;
    uint32_t sample_rate_hz;
    uint32_t channels;
    uint32_t timescale;
    uint32_t movie_timescale;
    uint64_t edit_start;
    uint64_t edit_end;
    bool edit;
    uint32_t default_duration;
    uint32_t default_size;
    uint8_t config[256];
    size_t config_size;
    char codec[16];
    lnd_demux_mp4_sample *samples;
    size_t sample_count;
    size_t sample_capacity;
    size_t media_start, media_end;
    size_t sample_index;
    size_t indexed_count;
    uint64_t decode_time;
};

bool lnd_demux_append(LND_DEMUX *d, const void *data, size_t bytes);
int32_t lnd_ts_read(LND_DEMUX *d, LND_DEMUX_PACKET *packet);
int32_t lnd_demux_mp4_init(LND_DEMUX *d, const uint8_t *data, size_t bytes);
int32_t lnd_demux_mp4_begin(LND_DEMUX *d);
int32_t lnd_demux_mp4_sample_info(LND_DEMUX *d, const lnd_demux_mp4_sample *sample, LND_DEMUX_PACKET *packet);
int32_t lnd_demux_mp4_read(LND_DEMUX *d, LND_DEMUX_PACKET *packet);
