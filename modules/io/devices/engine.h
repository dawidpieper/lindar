#pragma once

#include "src/atomic.h"
#include "src/platform.h"
#include "src/spinlock.h"
#include "device.h"
#include "playback/graph/node.h"
#include "backend.h"
#include "playback/graph/sound.h"

typedef struct lnd_capture lnd_capture;
typedef struct lnd_sink_node lnd_sink_node;

struct LND_DEVICE_INSTANCE {
    lnd_device *device;
    lnd_stream *stream;
    lnd_stream_cfg cfg;
    lnd_stream_cfg requested;
    lnd_atomic_u32 info[7];
    lnd_atomic_ptr public_device;
    lnd_node *master;
    lnd_capture *capture;
    lnd_spinlock sinks_lock;
    lnd_sink_node *sinks;
    uint64_t cursor;
    float *scratch;
    uint32_t scratch_frames;
    lnd_atomic_u64 position;
    lnd_atomic_u32 running;
    bool follow_default;
    bool failed_reported;
    uint64_t last_reopen;
    struct LND_DEVICE_INSTANCE *prev;
    struct LND_DEVICE_INSTANCE *next;
};

typedef struct LND_DEVICE_INSTANCE lnd_instance;

lnd_instance *lnd_instance_open(lnd_device *d, bool follow_default);
lnd_instance *lnd_instance_open_capture(lnd_device *d, uint32_t sample_rate_hz, uint32_t flags, bool follow_default);
int32_t lnd_instance_close(lnd_instance *i);
void lnd_instances_stop_all(void);
void lnd_instances_destroy_all(void);
int32_t lnd_engine_start(void);
void lnd_engine_stop(void);
void lnd_engine_wake(void);
void lnd_engine_maintain(void);
