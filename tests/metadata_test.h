#pragma once
#include "codec_test.h"
#include "lindar_metadata.h"
#include "lindar_metadata_id3v1.h"
#include "lindar_metadata_id3v2.h"
#include "lindar_metadata_comments.h"
#include "lindar_metadata_wave.h"
#include "lindar_metadata_io.h"

typedef struct tag_bytes {
    uint8_t *data;
    size_t size, capacity, pos;
} tag_bytes;
static void tag_append(tag_bytes *b, const void *p, size_t n) {
    if (b->size + n > b->capacity) {
        b->capacity = (b->size + n) * 2 + 256;
        b->data = realloc(b->data, b->capacity);
        if (!b->data) abort();
    }
    if (n) memcpy(b->data + b->size, p, n);
    b->size += n;
}
static void tag_byte(tag_bytes *b, uint8_t v) { tag_append(b, &v, 1); }
static void tag_u32(tag_bytes *b, uint32_t n, bool be) {
    uint8_t p[4];
    for (unsigned i = 0; i < 4; ++i) p[be ? 3 - i : i] = (uint8_t)(n >> (i * 8));
    tag_append(b, p, 4);
}
static void tag_sync(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) p[3 - i] = (uint8_t)((n >> (7 * i)) & 127);
}
static uint32_t tag_get32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static size_t tag_write(void *user, const void *p, size_t n) {
    tag_bytes *b = user;
    if (b->pos == b->size)
        tag_append(b, p, n);
    else {
        if (n > b->size - b->pos) return 0;
        memcpy(b->data + b->pos, p, n);
    }
    b->pos += n;
    return n;
}
static int32_t tag_seek(void *user, uint64_t pos) {
    tag_bytes *b = user;
    if (pos > b->size) return LND_ERR_IO;
    b->pos = (size_t)pos;
    return LND_OK;
}
static const LND_IO_OUTPUT_PROCS tag_output_procs = {tag_write, tag_seek};
static void tag_frame(tag_bytes *b, const char *id, unsigned version, const void *p, size_t n) {
    uint8_t h[10] = {0};
    size_t header = version == 2 ? 6 : 10;
    memcpy(h, id, version == 2 ? 3 : 4);
    if (version == 2) {
        h[3] = (uint8_t)(n >> 16);
        h[4] = (uint8_t)(n >> 8);
        h[5] = (uint8_t)n;
    } else if (version == 4)
        tag_sync(h + 4, (uint32_t)n);
    else
        for (unsigned i = 0; i < 4; ++i) h[7 - i] = (uint8_t)(n >> (i * 8));
    tag_append(b, h, header);
    tag_append(b, p, n);
}
static tag_bytes tag_id3(const tag_bytes *frames, unsigned version, uint8_t flags) {
    tag_bytes b = {0};
    uint8_t header[10] = {'I', 'D', '3', (uint8_t)version, 0, flags};
    tag_sync(header + 6, (uint32_t)frames->size);
    tag_append(&b, header, 10);
    tag_append(&b, frames->data, frames->size);
    return b;
}
static void tag_riff_chunk(tag_bytes *b, const char *id, const void *p, size_t n) {
    tag_append(b, id, 4);
    tag_u32(b, (uint32_t)n, false);
    tag_append(b, p, n);
    if (n & 1) tag_byte(b, 0);
}
static tag_bytes tag_wave(const void *chunks, size_t n) {
    tag_bytes b = {0};
    uint8_t fmt[16] = {1, 0, 1, 0, 0x80, 0xbb, 0, 0, 0, 0x77, 1, 0, 2, 0, 16, 0};
    tag_append(&b, "RIFF", 4);
    tag_u32(&b, (uint32_t)(n + 36 + 256), false);
    tag_append(&b, "WAVE", 4);
    tag_riff_chunk(&b, "fmt ", fmt, 16);
    tag_append(&b, chunks, n);
    uint8_t pcm[256];
    for (unsigned i = 0; i < sizeof pcm; ++i) pcm[i] = (uint8_t)(i * 17);
    tag_riff_chunk(&b, "data", pcm, sizeof pcm);
    return b;
}
static void tag_expect(const LND_METADATA *m, const char *key, uint32_t index, const char *expected) {
    const char *value = LND_MetadataGetValue(m, key, index);
    if (!value || strcmp(value, expected)) printf("tag %s[%u]: got %s, expected %s\n", key, index, value ? value : "(null)", expected);
    CHECK(value && !strcmp(value, expected));
}
