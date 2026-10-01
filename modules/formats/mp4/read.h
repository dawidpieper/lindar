#pragma once

#include "src/platform.h"

#define LND_MP4_TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

typedef struct lnd_mp4_header {
    uint64_t size;
    uint32_t type;
    uint32_t bytes;
} lnd_mp4_header;

typedef struct lnd_mp4_box {
    const uint8_t *data;
    size_t bytes, offset, end;
    uint32_t type;
} lnd_mp4_box;

typedef struct lnd_mp4_table {
    const uint8_t *data;
    uint32_t count, width;
} lnd_mp4_table;

typedef struct lnd_mp4_config {
    const uint8_t *data;
    size_t size;
    uint8_t object_type;
} lnd_mp4_config;

typedef void (*lnd_mp4_store_location)(void *user, uint32_t index, uint64_t offset, uint32_t bytes);

LND_INLINE uint32_t lnd_mp4_u32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
LND_INLINE uint64_t lnd_mp4_u64(const uint8_t *p) { return (uint64_t)lnd_mp4_u32(p) << 32 | lnd_mp4_u32(p + 4); }

bool lnd_mp4_box_bounds(const uint8_t *data, size_t bytes, uint64_t remaining, lnd_mp4_header *header);
bool lnd_mp4_next(const uint8_t *data, size_t bytes, size_t *at, lnd_mp4_box *box);
bool lnd_mp4_child(const lnd_mp4_box *parent, uint32_t type, lnd_mp4_box *out);
bool lnd_mp4_descriptors(const uint8_t *data, size_t bytes, unsigned depth, lnd_mp4_config *config);
bool lnd_mp4_table_view(lnd_mp4_table *table, const uint8_t *data, size_t bytes, uint32_t count, uint32_t width);
int32_t lnd_mp4_locations(const lnd_mp4_table *chunks, const lnd_mp4_table *runs, const lnd_mp4_table *sizes, uint32_t fixed, uint32_t count,
                          uint64_t packet_limit, lnd_mp4_store_location store, void *user);
