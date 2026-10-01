#pragma once

#include "src/platform.h"
#include "device.h"

enum {
    LND_BACKEND_EVENT_DEVICES = 1u << 0,
    LND_BACKEND_EVENT_DEFAULT_OUTPUT = 1u << 1,
    LND_BACKEND_EVENT_DEFAULT_INPUT = 1u << 2,
};

typedef struct lnd_stream_cfg {
    uint32_t sample_rate_hz;
    uint32_t channels;
    int32_t format;
    uint32_t period_frames;
    uint32_t periods;
    bool exclusive;
    bool loopback;
    int32_t priority;
    uint32_t buffer_frames;
    uint32_t latency_frames;
} lnd_stream_cfg;

typedef void (*lnd_stream_proc)(void *user, void *dst, uint64_t frames);

typedef struct lnd_stream lnd_stream;
typedef struct lnd_backend lnd_backend;

struct LND_DEVICE_BACKEND {
    const char *name;
    int32_t (*init)(lnd_backend *b);
    void (*free)(lnd_backend *b);
    int32_t (*enumerate)(lnd_backend *b, int32_t type, lnd_device_list *out);
    uint32_t (*poll)(lnd_backend *b);
    int32_t (*open)(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out);
    int32_t (*open_capture)(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out);
    int32_t (*start)(lnd_stream *s);
    int32_t (*stop)(lnd_stream *s);
    int32_t (*status)(lnd_stream *s);
    void (*close)(lnd_stream *s);
    uint32_t (*get_latency_frames)(lnd_stream *s);
};

typedef LND_DEVICE_BACKEND lnd_backend_vt;

struct lnd_backend {
    const lnd_backend_vt *vt;
    void *data;
    void (*wake)(void);
};

const lnd_backend_vt *lnd_backend_default(void);
