#pragma once

#include "src/atomic.h"
#include "src/platform.h"

typedef struct lnd_cmd {
    uint32_t op;
    uint32_t u32;
    uint64_t u64;
    void *ptr;
    float f32;
} lnd_cmd;

typedef struct lnd_ring {
    lnd_cmd *items;
    uint32_t mask;
    alignas(LND_CACHE_LINE) lnd_atomic_u32 head;
    alignas(LND_CACHE_LINE) lnd_atomic_u32 tail;
} lnd_ring;

int32_t lnd_ring_init(lnd_ring *r, uint32_t capacity);
void lnd_ring_free(lnd_ring *r);
int32_t lnd_ring_push(lnd_ring *r, const lnd_cmd *c);
bool lnd_ring_pop(lnd_ring *r, lnd_cmd *out);
uint32_t lnd_ring_count(const lnd_ring *r);
