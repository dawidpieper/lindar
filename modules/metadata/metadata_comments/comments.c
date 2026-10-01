#include "lindar_metadata_comments.h"
#include "metadata/internal.h"
#include <stdio.h>

static bool comment_key(const char *key) {
    if (!*key) return false;
    for (const unsigned char *p = (const unsigned char *)key; *p; ++p)
        if (*p < 0x20 || *p > 0x7d || *p == '=') return false;
    return true;
}

static int32_t comment(LND_METADATA *m, const uint8_t *p, size_t n) {
    const uint8_t *equals = memchr(p, '=', n);
    if (!equals || equals == p || !lnd_tag_utf8(p, n)) return LND_ERR_FORMAT;
    size_t key_size = (size_t)(equals - p);
    for (size_t i = 0; i < key_size; ++i)
        if (p[i] < 0x20 || p[i] > 0x7d) return LND_ERR_FORMAT;
    char *text = lnd_alloc(n + 1);
    if (!text) return LND_ERR_OUT_OF_MEMORY;
    memcpy(text, p, n);
    text[n] = 0;
    text[key_size] = 0;
    int32_t r = lnd_tag_add_text(m, text, text + key_size + 1, nullptr, nullptr);
    lnd_free(text);
    return r;
}

static bool chapter_key(const char *key) {
    return strlen(key) == 10 && !memcmp(key, "CHAPTER", 7) && key[7] >= '0' && key[7] <= '9' && key[8] >= '0' && key[8] <= '9' && key[9] >= '0' &&
           key[9] <= '9';
}

static int32_t chapters(LND_METADATA *m) {
    for (uint32_t i = 0; i < m->field_count; ++i) {
        const LND_METADATA_FIELD *f = m->fields + i;
        if (!chapter_key(f->key)) continue;
        if (LND_MetadataGetValue(m, f->key, 1)) return LND_ERR_FORMAT;
        uint64_t us;
        int32_t r = lnd_tag_timestamp(f->value, &us);
        if (r) return r;
        char key[16];
        snprintf(key, sizeof key, "%sNAME", f->key);
        const char *title = LND_MetadataGetValue(m, key, 0);
        if (LND_MetadataGetValue(m, key, 1)) return LND_ERR_FORMAT;
        snprintf(key, sizeof key, "%sURL", f->key);
        const char *url = LND_MetadataGetValue(m, key, 0);
        if (LND_MetadataGetValue(m, key, 1)) return LND_ERR_FORMAT;
        LND_METADATA_CHAPTER c = {.id = f->key + 7,
                                  .title = title,
                                  .url = url,
                                  .start_us = us,
                                  .end_us = LND_METADATA_UNKNOWN,
                                  .start_offset_bytes = LND_METADATA_UNKNOWN,
                                  .end_offset_bytes = LND_METADATA_UNKNOWN};
        r = LND_MetadataSetChapter(m, &c);
        if (r) return r;
    }
    for (uint32_t i = m->field_count; i-- > 0;) {
        const char *key = m->fields[i].key;
        for (uint32_t j = 0; j < m->chapter_count; ++j) {
            char prefix[16];
            snprintf(prefix, sizeof prefix, "CHAPTER%s", m->chapters[j].id);
            if (!strncmp(key, prefix, 10) && (!key[10] || !strcmp(key + 10, "NAME") || !strcmp(key + 10, "URL"))) {
                LND_MetadataRemoveField(m, i);
                break;
            }
        }
    }
    return LND_OK;
}

int32_t LND_MetadataCommentsRead(LND_METADATA *m, const void *packet, size_t size, int32_t format) {
    if (!m || !packet) return LND_ERR_INVALID_ARG;
    const uint8_t *p = packet;
    size_t prefix = format == LND_METADATA_OPUS ? 8 : format == LND_METADATA_VORBIS ? 7 : 0;
    if (format != LND_METADATA_OPUS && format != LND_METADATA_VORBIS && format != LND_METADATA_FLAC) return LND_ERR_INVALID_ARG;
    if (size < prefix + 8 || (prefix && memcmp(p, format == LND_METADATA_OPUS ? "OpusTags" : "\3vorbis", prefix))) return LND_ERR_FORMAT;
    p += prefix;
    size -= prefix;
    if (size > m->limits.bytes) return LND_METADATA_ERR_LIMIT;
    uint32_t vendor_size = lnd_tag_get32(p, false);
    if (vendor_size > size - 8 || !lnd_tag_utf8(p + 4, vendor_size)) return LND_ERR_FORMAT;
    LND_METADATA *tmp = LND_MetadataCreate(&m->limits);
    if (!tmp) return LND_ERR_OUT_OF_MEMORY;
    tmp->format = format;
    tmp->version = 1;
    char *vendor = lnd_alloc((size_t)vendor_size + 1);
    int32_t r = vendor ? LND_OK : LND_ERR_OUT_OF_MEMORY;
    if (vendor) {
        memcpy(vendor, p + 4, vendor_size);
        vendor[vendor_size] = 0;
        r = LND_MetadataSetVendor(tmp, vendor);
        lnd_free(vendor);
    }
    size_t pos = (size_t)vendor_size + 4;
    uint32_t count = lnd_tag_get32(p + pos, false);
    pos += 4;
    if (count > m->limits.fields) r = LND_METADATA_ERR_LIMIT;
    for (uint32_t i = 0; !r && i < count; ++i) {
        if (size - pos < 4) {
            r = LND_ERR_FORMAT;
            break;
        }
        uint32_t n = lnd_tag_get32(p + pos, false);
        pos += 4;
        if (n > size - pos) {
            r = LND_ERR_FORMAT;
            break;
        }
        r = comment(tmp, p + pos, n);
        pos += n;
    }
    if (!r && format == LND_METADATA_VORBIS && (pos >= size || p[pos] != 1)) r = LND_ERR_FORMAT;
    if (!r && format == LND_METADATA_FLAC && pos != size) r = LND_ERR_FORMAT;
    if (!r && format == LND_METADATA_OPUS && pos < size && (p[pos] & 1)) {
        LND_METADATA_BLOB blob = {.format = LND_METADATA_OPUS, .key = "TRAILER", .data = p + pos, .size = size - pos};
        r = LND_MetadataAddBlob(tmp, &blob);
    }
    if (!r) r = chapters(tmp);
    return lnd_tag_commit(m, tmp, r);
}

static void put_comment(lnd_tag_buffer *b, const char *key, const char *value, uint32_t *count) {
    size_t a = strlen(key), n = strlen(value);
    if (!comment_key(key) || a >= UINT32_MAX || n > UINT32_MAX - a - 1 || *count == UINT32_MAX) {
        b->error = LND_ERR_UNSUPPORTED;
        return;
    }
    lnd_tag_u32(b, (uint32_t)(a + n + 1), false);
    lnd_tag_append(b, key, a);
    lnd_tag_byte(b, '=');
    lnd_tag_append(b, value, n);
    ++*count;
}

int32_t LND_MetadataCommentsCreateBuffer(const LND_METADATA *m, int32_t format, uint32_t flags, void **packet, size_t *size) {
    if (!m || !packet || !size || (flags & ~LND_METADATA_DROP_UNSUPPORTED)) return LND_ERR_INVALID_ARG;
    if (format != LND_METADATA_OPUS && format != LND_METADATA_VORBIS && format != LND_METADATA_FLAC) return LND_ERR_INVALID_ARG;
    *packet = nullptr;
    *size = 0;
    bool drop = (flags & LND_METADATA_DROP_UNSUPPORTED) != 0;
    if (m->chapter_count > 1000) return LND_ERR_UNSUPPORTED;
    lnd_tag_buffer b = {.limit = m->limits.bytes};
    const char *vendor = LND_MetadataGetVendor(m);
    size_t vendor_size = strlen(vendor);
    if (vendor_size > UINT32_MAX) return LND_METADATA_ERR_LIMIT;
    if (format == LND_METADATA_OPUS) lnd_tag_append(&b, "OpusTags", 8);
    if (format == LND_METADATA_VORBIS) lnd_tag_append(&b, "\3vorbis", 7);
    lnd_tag_u32(&b, (uint32_t)vendor_size, false);
    lnd_tag_append(&b, vendor, vendor_size);
    size_t count_pos = b.size;
    lnd_tag_u32(&b, 0, false);
    uint32_t count = 0;
    for (uint32_t i = 0; !b.error && i < m->field_count; ++i) {
        const LND_METADATA_FIELD *f = m->fields + i;
        if ((!comment_key(f->key) || *f->language || *f->description) && !drop) {
            b.error = LND_ERR_UNSUPPORTED;
            break;
        }
        if (m->chapter_count && strlen(f->key) >= 10 && !memcmp(f->key, "CHAPTER", 7)) {
            b.error = LND_ERR_INVALID_ARG;
            break;
        }
        if (comment_key(f->key)) put_comment(&b, f->key, f->value, &count);
    }
    for (uint32_t i = 0; !b.error && i < m->chapter_count; ++i) {
        const LND_METADATA_CHAPTER *c = m->chapters + i;
        if (!drop && (c->end_us != LND_METADATA_UNKNOWN || c->start_offset_bytes != LND_METADATA_UNKNOWN || c->end_offset_bytes != LND_METADATA_UNKNOWN)) {
            b.error = LND_ERR_UNSUPPORTED;
            break;
        }
        char key[16], time[40];
        snprintf(key, sizeof key, "CHAPTER%03u", i);
        lnd_tag_time_string(c->start_us, time);
        put_comment(&b, key, time, &count);
        snprintf(key, sizeof key, "CHAPTER%03uNAME", i);
        put_comment(&b, key, c->title, &count);
        if (*c->url) {
            snprintf(key, sizeof key, "CHAPTER%03uURL", i);
            put_comment(&b, key, c->url, &count);
        }
    }
    if (!b.error) lnd_tag_put32(b.data + count_pos, count, false);
    bool trailer = false;
    for (uint32_t i = 0; !b.error && i < m->blob_count; ++i) {
        const LND_METADATA_BLOB *blob = m->blobs + i;
        if (format == LND_METADATA_OPUS && blob->format == LND_METADATA_OPUS && !strcmp(blob->key, "TRAILER") && !*blob->scope && !trailer) {
            lnd_tag_append(&b, blob->data, blob->size);
            trailer = true;
        } else if (!drop)
            b.error = LND_ERR_UNSUPPORTED;
    }
    if (format == LND_METADATA_VORBIS) lnd_tag_byte(&b, 1);
    return lnd_tag_finish(&b, packet, size);
}

int32_t LND_MetadataOpusRead(LND_METADATA *m, const void *packet, size_t size) { return LND_MetadataCommentsRead(m, packet, size, LND_METADATA_OPUS); }
int32_t LND_MetadataOpusCreateBuffer(const LND_METADATA *m, uint32_t flags, void **packet, size_t *size) {
    return LND_MetadataCommentsCreateBuffer(m, LND_METADATA_OPUS, flags, packet, size);
}
