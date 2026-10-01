#pragma once

#include "src/atomic.h"
#include "src/platform.h"
#include "src/thread.h"
#include "src/spinlock.h"
#include "pcm/audio/source.h"

typedef struct LND_DEVICE_INSTANCE lnd_instance;

enum {
    LND_CAPTURE_READ_STREAM,
    LND_CAPTURE_READ_WAIT,
    LND_CAPTURE_READ_PARTIAL,
};

typedef struct lnd_capture {
    lnd_instance *instance;
    lnd_spinlock control;
    lnd_atomic_u32 paused;
    float *ring;
    uint32_t cap;
    uint32_t mask;
    uint32_t channels;
    uint32_t input_channels;
    float *input_matrix;
    float *input_scratch;
    lnd_atomic_u32 sample_rate_hz;
    lnd_atomic_u32 prefill;
    float *scratch;
    uint32_t scratch_frames;
    lnd_atomic_u64 head;
    lnd_atomic_u64 tail;
    lnd_atomic_u64 overruns;
    lnd_atomic_u32 ended;
    lnd_atomic_u32 generation;
    lnd_event event;
    lnd_atomic_u32 starved;
} lnd_capture;

lnd_capture *lnd_capture_new(uint32_t channels, uint32_t sample_rate_hz, uint32_t frames);
void lnd_capture_free(lnd_capture *c);
int32_t lnd_capture_set_input_channels(lnd_capture *c, uint32_t channels);
void lnd_capture_push(lnd_capture *c, const void *data, int32_t format, uint64_t frames);
uint64_t lnd_capture_available(const lnd_capture *c);
uint64_t lnd_capture_read(lnd_capture *c, float *dst, uint64_t frames, int32_t mode);
void lnd_capture_end(lnd_capture *c);
lnd_source *lnd_capture_source_create(lnd_capture *c, uint32_t channels, uint32_t sample_rate_hz, uint32_t flags);
