#include "id3.h"
#include "src/alloc.h"

#include <string.h>

static uint32_t lnd_id3_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

bool lnd_id3_syncsafe(const uint8_t *p, uint32_t *value) {
    if ((p[0] | p[1] | p[2] | p[3]) & 0x80) return false;
    *value = (uint32_t)p[0] << 21 | (uint32_t)p[1] << 14 | (uint32_t)p[2] << 7 | p[3];
    return true;
}

bool lnd_id3_valid_id(const char *id, size_t size) {
    for (size_t i = 0; i < size; i++)
        if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9'))) return false;
    return true;
}

static size_t lnd_id3_unsync(uint8_t *p, size_t size) {
    size_t out = 0;
    for (size_t i = 0; i < size; i++) {
        p[out++] = p[i];
        if (p[i] == 255 && i + 1 < size && !p[i + 1]) i++;
    }
    return out;
}

int32_t lnd_id3_header(const uint8_t *p, size_t size, lnd_id3_tag *tag) {
    *tag = (lnd_id3_tag){0};
    uint32_t bytes;
    if (size < 10 || memcmp(p, "ID3", 3) || p[3] < 2 || p[3] > 4 || p[4] == 255 || !lnd_id3_syncsafe(p + 6, &bytes)) return LND_ERR_FORMAT;
    unsigned version = p[3], flags = p[5];
    if (flags & (version == 2 ? 0x3f : version == 3 ? 0x1f : 0x0f)) return LND_ERR_FORMAT;
    if (version == 2 && (flags & 0x40)) return LND_ERR_UNSUPPORTED;
    size_t total = (size_t)bytes + 10 + (version == 4 && (flags & 16) ? 10 : 0);
    if (total > size) return LND_ERR_FORMAT;
    if (version == 4 && (flags & 16)) {
        const uint8_t *footer = p + 10 + bytes;
        if (memcmp(footer, "3DI", 3) || memcmp(footer + 3, p + 3, 7)) return LND_ERR_FORMAT;
    }
    *tag = (lnd_id3_tag){.body = p + 10, .size = bytes, .total = total, .version = version, .flags = flags};
    return LND_OK;
}

int32_t lnd_id3_body(lnd_id3_tag *tag) {
    const uint8_t *p = tag->body;
    size_t length = tag->size;
    if (tag->version < 4 && (tag->flags & 0x80)) {
        tag->copy = lnd_alloc(length ? length : 1);
        if (!tag->copy) return LND_ERR_OUT_OF_MEMORY;
        memcpy(tag->copy, p, length);
        length = lnd_id3_unsync(tag->copy, length);
        p = tag->copy;
    }
    if (tag->version >= 3 && (tag->flags & 0x40)) {
        uint32_t extended;
        if (length < 4) return LND_ERR_FORMAT;
        if (tag->version == 4) {
            if (!lnd_id3_syncsafe(p, &extended) || extended < 6 || extended > length || p[4] != 1 || (p[5] & 0x8f)) return LND_ERR_FORMAT;
        } else {
            extended = lnd_id3_be32(p);
            if (extended < 6 || extended > length - 4) return LND_ERR_FORMAT;
            extended += 4;
        }
        p += extended;
        length -= extended;
    }
    tag->body = p;
    tag->size = length;
    return LND_OK;
}

void lnd_id3_close(lnd_id3_tag *tag) {
    lnd_free(tag->copy);
    tag->copy = nullptr;
}

void lnd_id3_iterator_close(lnd_id3_iterator *it) {
    lnd_free(it->copy);
    it->copy = nullptr;
}

int32_t lnd_id3_next(lnd_id3_iterator *it, lnd_id3_frame *frame) {
    lnd_id3_iterator_close(it);
    if (it->position == it->size) return 0;
    const uint8_t *p = it->data + it->position;
    size_t remaining = it->size - it->position;
    if (!*p) {
        for (size_t i = 1; i < remaining; i++)
            if (p[i]) return LND_ERR_FORMAT;
        it->position = it->size;
        return 0;
    }
    size_t header = it->version == 2 ? 6 : 10;
    if (remaining < header) return LND_ERR_FORMAT;
    *frame = (lnd_id3_frame){0};
    memcpy(frame->id, p, it->version == 2 ? 3 : 4);
    if (!lnd_id3_valid_id(frame->id, it->version == 2 ? 3 : 4)) return LND_ERR_FORMAT;
    uint32_t bytes;
    if (it->version == 2) bytes = (uint32_t)p[3] << 16 | (uint32_t)p[4] << 8 | p[5];
    else if (it->version == 4) {
        if (!lnd_id3_syncsafe(p + 4, &bytes)) return LND_ERR_FORMAT;
    } else bytes = lnd_id3_be32(p + 4);
    uint16_t flags = it->version == 2 ? 0 : (uint16_t)(p[8] << 8 | p[9]);
    if (!bytes || bytes > remaining - header || (flags & (it->version == 4 ? 0x8fb0 : 0x1f1f))) return LND_ERR_FORMAT;
    frame->data = p + header;
    frame->size = bytes;
    it->position += header + bytes;
    if (it->version == 4 && (it->unsynchronized || (flags & 2))) {
        it->copy = lnd_alloc(bytes);
        if (!it->copy) return LND_ERR_OUT_OF_MEMORY;
        memcpy(it->copy, frame->data, bytes);
        frame->size = lnd_id3_unsync(it->copy, bytes);
        frame->data = it->copy;
        flags &= ~2u;
    }
    frame->flags = flags;
    frame->opaque = (flags & (it->version == 4 ? 0x004c : 0x00e0)) != 0;
    if (!frame->opaque && it->version == 4 && (flags & 1)) {
        uint32_t decoded;
        if (frame->size < 4 || !lnd_id3_syncsafe(frame->data, &decoded) || decoded != frame->size - 4) return LND_ERR_FORMAT;
        frame->data += 4;
        frame->size -= 4;
    }
    return 1;
}
