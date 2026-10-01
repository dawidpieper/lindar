#pragma once

#include "src/atomic.h"
#include "src/thread.h"
#include "src/callback.h"
#include "io/devices/backend.h"
#include "lindar_asio.h"
#include "pcm.h"
#include "abi.h"

static_assert(LND_ASIO_MAX_CHANNELS == LND_MAX_CHANNELS);

enum { LND_ASIO_SLOTS = 16, LND_ASIO_QUEUE = 4 };
enum { LND_ASIO_RESET = 1u, LND_ASIO_LATENCIES = 2u, LND_ASIO_RESYNC = 4u };

typedef struct lnd_asio_backend lnd_asio_backend;
typedef struct lnd_asio_driver lnd_asio_driver;
typedef int32_t (*lnd_asio_command)(lnd_asio_backend *, void *);

typedef struct lnd_asio_job {
    lnd_asio_command proc;
    void *argument;
    int32_t result;
} lnd_asio_job;

typedef struct lnd_asio_buffer {
    lnd_asio_pcm pcm;
    uint32_t channel;
    bool input;
} lnd_asio_buffer;

typedef struct lnd_asio_block {
    uint32_t index;
    LND_ASIO_TIME time;
} lnd_asio_block;

struct lnd_stream {
    lnd_asio_driver *driver;
    lnd_stream_cfg cfg;
    lnd_stream_proc proc;
    void *user;
    lnd_atomic_u32 active;
    lnd_atomic_u32 readers;
    lnd_atomic_i32 failed;
    uint32_t direction;
};

struct lnd_asio_driver {
    lnd_asio_driver *next;
    lnd_asio_backend *owner;
    GUID clsid;
    char id[40];
    char *name;
    bool seen;
    bool available;
    LND_ASIO_CONFIG config;
    LND_ASIO_INFO info;
    int32_t clock_index;
    lnd_asio_abi *abi;
    struct cwASIOBufferInfo *buffers;
    lnd_asio_buffer *conversion;
    uint32_t buffer_count;
    float *pcm[2];
    lnd_atomic_ptr streams[2];
    bool buffers_created;
    bool started;
    bool timecode_enabled;
    int32_t slot;
    lnd_thread render_thread;
    lnd_event render_event;
    lnd_atomic_u32 render_stop;
    lnd_atomic_u32 accepting;
    lnd_atomic_u32 processing;
    lnd_atomic_u32 buffers_busy;
    lnd_atomic_u32 enqueuing;
    lnd_atomic_u32 queue_read;
    lnd_atomic_u32 queue_write;
    lnd_asio_block queue[LND_ASIO_QUEUE];
    lnd_atomic_u32 pending;
    lnd_atomic_u32 recovery_rate;
    lnd_atomic_u32 recovery_buffer;
    lnd_atomic_u32 changing;
    lnd_atomic_u64 overloads;
    lnd_atomic_i32 driver_error;
    lnd_atomic_u32 time_sequence;
    lnd_atomic_u64 time_values[6];
    lnd_atomic_u32 time_flags;
};

struct lnd_asio_backend {
    lnd_backend *backend;
    lnd_asio_driver *drivers;
    lnd_thread thread;
    HANDLE command_event;
    HANDLE done_event;
    HWND window;
    lnd_mutex commands;
    lnd_atomic_ptr job;
    lnd_atomic_u32 quit;
    int32_t startup_result;
    uint64_t next_scan;
    uint64_t registry_fingerprint;
};

typedef int32_t (*lnd_asio_registration_proc)(void *, const GUID *, const char *, const char *);
int32_t lnd_asio_registry_enumerate(lnd_asio_registration_proc proc, void *user, uint64_t *fingerprint);
int32_t lnd_asio_driver_load(const GUID *id, lnd_asio_abi **out);
void lnd_asio_driver_unload(lnd_asio_abi *driver);
int32_t lnd_asio_call(lnd_asio_backend *backend, lnd_asio_command proc, void *argument);
int32_t lnd_asio_error(lnd_asio_driver *driver, cwASIOError error);
int32_t lnd_asio_load(lnd_asio_driver *driver);
int32_t lnd_asio_query(lnd_asio_driver *driver);
void lnd_asio_unload(lnd_asio_driver *driver);
int32_t lnd_asio_prepare(lnd_asio_driver *driver, const lnd_stream_cfg *cfg);
int32_t lnd_asio_start(lnd_asio_driver *driver);
void lnd_asio_stop(lnd_asio_driver *driver);
void lnd_asio_dispose(lnd_asio_driver *driver);
void lnd_asio_invalidate(lnd_asio_driver *driver, int32_t error);
void lnd_asio_stream_quiesce(lnd_stream *stream);
void lnd_asio_request(lnd_asio_driver *driver, uint32_t flags);
int32_t lnd_asio_read_time(lnd_asio_driver *driver, LND_ASIO_TIME *time);
void lnd_asio_refresh_latencies(lnd_asio_driver *driver);
void lnd_asio_text(char *output, size_t capacity, const char *input, size_t length);
int32_t lnd_asio_config_validate(const LND_ASIO_CONFIG *config);
lnd_asio_driver *lnd_asio_device_driver(LND_DEVICE *device);
extern const lnd_backend_vt lnd_backend_asio_vt;
