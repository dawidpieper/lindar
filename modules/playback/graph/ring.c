#include "ring.h"
#include "src/alloc.h"
#include "lindar.h"

int32_t lnd_ring_init(lnd_ring *r, uint32_t capacity) {
    capacity = lnd_next_pow2_u32(capacity < 2 ? 2 : capacity);
    if (!capacity) return LND_ERR_INVALID_ARG;
#if SIZE_MAX <= UINT32_MAX
    if (capacity > SIZE_MAX / sizeof(lnd_cmd)) return LND_ERR_INVALID_ARG;
#endif
    r->items = nullptr;
    r->mask = capacity - 1;
    lnd_store_relaxed(&r->head, 0);
    lnd_store_relaxed(&r->tail, 0);
    return LND_OK;
}

void lnd_ring_free(lnd_ring *r) {
    lnd_free(r->items);
    r->items = nullptr;
    r->mask = 0;
}

int32_t lnd_ring_push(lnd_ring *r, const lnd_cmd *c) {
    uint32_t tail = lnd_load_relaxed(&r->tail);
    uint32_t head = lnd_load(&r->head);
    if (tail - head > r->mask) return LND_ERR_BUSY;
    if (!r->items) {
        r->items = lnd_alloc(sizeof(lnd_cmd) * ((size_t)r->mask + 1));
        if (!r->items) return LND_ERR_OUT_OF_MEMORY;
    }
    r->items[tail & r->mask] = *c;
    lnd_store(&r->tail, tail + 1);
    return LND_OK;
}

bool lnd_ring_pop(lnd_ring *r, lnd_cmd *out) {
    uint32_t head = lnd_load_relaxed(&r->head);
    uint32_t tail = lnd_load(&r->tail);
    if (head == tail) return false;
    *out = r->items[head & r->mask];
    lnd_store(&r->head, head + 1);
    return true;
}

uint32_t lnd_ring_count(const lnd_ring *r) { return lnd_load(&r->tail) - lnd_load(&r->head); }
