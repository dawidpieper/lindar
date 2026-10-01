#pragma once
#include "lnd_modules.h"
#include "lindar_metadata.h"
#include "src/alloc.h"
#include <string.h>
#if LND_MODULE_METADATA
#include "metadata/internal.h"
#endif
#if LND_MODULE_METADATA_COMMENTS
#include "lindar_metadata_comments.h"
#endif
#if LND_MODULE_METADATA_ID3V2
#include "lindar_metadata_id3v2.h"
#endif

typedef struct lnd_decode_tags {
    LND_METADATA *metadata;
    uint8_t *data;
    size_t size;
    size_t filled;
    uint64_t revision;
    int32_t status;
    int32_t format;
} lnd_decode_tags;

static inline void lnd_decode_tags_read(lnd_decode_tags *t, const void *data, size_t size, int32_t format) {
#if LND_MODULE_METADATA
    if (!t->metadata) t->metadata = LND_MetadataCreate(nullptr);
    t->status = t->metadata ? LND_ERR_UNSUPPORTED : LND_ERR_OUT_OF_MEMORY;
#if LND_MODULE_METADATA_COMMENTS
    if (t->metadata && (format == LND_METADATA_OPUS || format == LND_METADATA_VORBIS || format == LND_METADATA_FLAC))
        t->status = LND_MetadataCommentsRead(t->metadata, data, size, format);
#endif
#if LND_MODULE_METADATA_ID3V2
    if (t->metadata && format == LND_METADATA_ID3V2) t->status = LND_MetadataId3v2Read(t->metadata, data, size, nullptr);
#endif
    t->revision = lnd_tag_next_revision();
#endif
}

static inline void lnd_decode_tags_start(lnd_decode_tags *t, size_t size, int32_t format) {
#if LND_MODULE_METADATA
    lnd_free(t->data);
    t->data = nullptr;
    t->size = size;
    t->filled = 0;
    t->format = format;
    t->status = size > 32u * 1024 * 1024 ? LND_METADATA_ERR_LIMIT : LND_OK;
    if (!t->status) {
        t->data = lnd_alloc(size ? size : 1);
        if (!t->data) t->status = LND_ERR_OUT_OF_MEMORY;
    }
    if (t->status) t->revision = lnd_tag_next_revision();
#endif
}

static inline void lnd_decode_tags_append(lnd_decode_tags *t, const void *data, size_t size) {
    if (!t->data || size > t->size - t->filled) return;
    memcpy(t->data + t->filled, data, size);
    t->filled += size;
    if (t->filled != t->size) return;
    lnd_decode_tags_read(t, t->data, t->size, t->format);
    lnd_free(t->data);
    t->data = nullptr;
}

static inline int32_t lnd_decode_tags_get(const lnd_decode_tags *t, LND_METADATA *m, uint64_t *revision) {
    if (revision) *revision = t->revision;
#if LND_MODULE_METADATA
    if (t->status) return t->status;
    if (!t->metadata) return LND_METADATA_ERR_NOT_FOUND;
    return m ? lnd_tag_assign(m, t->metadata) : LND_OK;
#else
    return LND_ERR_UNSUPPORTED;
#endif
}

static inline void lnd_decode_tags_clear(lnd_decode_tags *t) {
#if LND_MODULE_METADATA
    LND_MetadataFree(t->metadata);
#endif
    lnd_free(t->data);
}
