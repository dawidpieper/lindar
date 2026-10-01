#include "internal.h"

void lnd_tag_append(lnd_tag_buffer *b, const void *data, size_t size) {
    if (b->error || !size) return;
    if (!data || size > b->limit - b->size) {
        b->error = LND_METADATA_ERR_LIMIT;
        return;
    }
    size_t need = b->size + size;
    if (need > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 128;
        while (capacity < need) {
            if (capacity > b->limit / 2) {
                capacity = b->limit;
                break;
            }
            capacity *= 2;
        }
        if (capacity > b->limit) capacity = b->limit;
        void *p = lnd_realloc(b->data, capacity);
        if (!p) {
            b->error = LND_ERR_OUT_OF_MEMORY;
            return;
        }
        b->data = p;
        b->capacity = capacity;
    }
    memcpy(b->data + b->size, data, size);
    b->size += size;
}

void lnd_tag_byte(lnd_tag_buffer *b, uint8_t value) { lnd_tag_append(b, &value, 1); }

void lnd_tag_u32(lnd_tag_buffer *b, uint32_t value, bool be) {
    uint8_t bytes[4];
    lnd_tag_put32(bytes, value, be);
    lnd_tag_append(b, bytes, 4);
}

int32_t lnd_tag_finish(lnd_tag_buffer *b, void **data, size_t *size) {
    if (b->error) {
        lnd_free(b->data);
        return b->error;
    }
    *data = b->data;
    *size = b->size;
    return LND_OK;
}

const char *lnd_tag_key(const lnd_tag_map *map, size_t count, const char *id) {
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(map[i].id, id) || (map[i].old_id && !strcmp(map[i].old_id, id))) return map[i].key;
    return id;
}

const char *lnd_tag_id(const lnd_tag_map *map, size_t count, const char *key) {
    for (size_t i = 0; i < count; ++i)
        if (lnd_tag_equal(map[i].key, key)) return map[i].id;
    return nullptr;
}
