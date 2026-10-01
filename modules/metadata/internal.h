#pragma once

#include "lindar_metadata.h"
#include "src/alloc.h"
#include "src/atomic.h"
#include <string.h>
#include <limits.h>

struct LND_METADATA {
    lnd_atomic_u32 references;
    LND_METADATA_LIMITS limits;
    size_t bytes;
    LND_METADATA_FIELD *fields;
    LND_METADATA_CHAPTER *chapters;
    LND_METADATA_BLOB *blobs;
    uint32_t field_count, field_capacity, chapter_count, chapter_capacity, blob_count, blob_capacity;
    int32_t format;
    uint32_t version;
    char *vendor;
};

typedef struct lnd_tag_buffer {
    uint8_t *data;
    size_t size, capacity, limit;
    int32_t error;
} lnd_tag_buffer;

typedef struct lnd_tag_map {
    const char *key, *id, *old_id;
} lnd_tag_map;

bool lnd_tag_equal(const char *a, const char *b);
bool lnd_tag_utf8(const void *data, size_t size);
int32_t lnd_tag_decode(const uint8_t *data, size_t size, unsigned encoding, char **text);
size_t lnd_tag_terminator(const uint8_t *data, size_t size, unsigned encoding);
void lnd_tag_text(lnd_tag_buffer *buffer, const char *text, unsigned encoding, bool terminate);
void lnd_tag_append(lnd_tag_buffer *buffer, const void *data, size_t size);
void lnd_tag_byte(lnd_tag_buffer *buffer, uint8_t value);
void lnd_tag_u32(lnd_tag_buffer *buffer, uint32_t value, bool be);
int32_t lnd_tag_finish(lnd_tag_buffer *buffer, void **data, size_t *size);
LND_METADATA *lnd_tag_ref(LND_METADATA *metadata);
int32_t lnd_tag_assign(LND_METADATA *dst, const LND_METADATA *source);
int32_t lnd_tag_commit(LND_METADATA *dst, LND_METADATA *parsed, int32_t result);
int32_t lnd_tag_add_text(LND_METADATA *metadata, const char *key, const char *value, const char *language, const char *description);
int32_t lnd_tag_uint(const char *text, uint64_t *value);
int32_t lnd_tag_timestamp(const char *text, uint64_t *us);
void lnd_tag_time_string(uint64_t us, char text[40]);
const char *lnd_tag_key(const lnd_tag_map *map, size_t count, const char *id);
const char *lnd_tag_id(const lnd_tag_map *map, size_t count, const char *key);

static inline uint16_t lnd_tag_get16(const uint8_t *p, bool be) { return be ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t lnd_tag_get32(const uint8_t *p, bool be) {
    return be ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]
              : p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint64_t lnd_tag_get64(const uint8_t *p) { return lnd_tag_get32(p, false) | ((uint64_t)lnd_tag_get32(p + 4, false) << 32); }
static inline void lnd_tag_put32(uint8_t *p, uint32_t value, bool be) {
    for (unsigned i = 0; i < 4; ++i)
        p[be ? 3 - i : i] = (uint8_t)(value >> (8 * i));
}
static inline bool lnd_tag_syncsafe(const uint8_t *p, uint32_t *value) {
    if ((p[0] | p[1] | p[2] | p[3]) & 0x80) return false;
    *value = ((uint32_t)p[0] << 21) | ((uint32_t)p[1] << 14) | ((uint32_t)p[2] << 7) | p[3];
    return true;
}
static inline void lnd_tag_put_syncsafe(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        p[3 - i] = (uint8_t)((value >> (7 * i)) & 127);
}

uint64_t lnd_tag_next_revision(void);
