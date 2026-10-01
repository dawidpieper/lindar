#pragma once

#include "src/atomic.h"
#include "src/platform.h"
#include "lindar_buffers.h"
#include "src/format.h"

struct LND_BUFFER {
    int32_t format;
    uint32_t channels;
    uint32_t sample_rate_hz;
    uint64_t frames;
    size_t capacity;
    uint32_t size_class;
    lnd_atomic_u32 refcount;
    void *data;
    struct LND_BUFFER *prev;
    struct LND_BUFFER *next;
};

typedef struct LND_BUFFER lnd_buffer;

typedef struct lnd_pool {
    void *free_list[64];
    size_t bytes;
} lnd_pool;

LND_INLINE uint32_t lnd_buffer_frame_bytes(const lnd_buffer *b) { return lnd_format_bytes(b->format) * b->channels; }

lnd_buffer *lnd_buffer_new(int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint64_t frames);
void lnd_buffer_ref(lnd_buffer *b);
void lnd_buffer_unref(lnd_buffer *b);
void lnd_pool_free_all(lnd_pool *p);
void lnd_buffers_free_all(void);
