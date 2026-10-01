#include "read.h"

bool lnd_mp4_box_bounds(const uint8_t *data, size_t bytes, uint64_t remaining, lnd_mp4_header *header) {
    if (bytes < 8 || remaining < 8) return false;
    uint64_t size = lnd_mp4_u32(data);
    uint32_t width = size == 1 ? 16 : 8;
    if (bytes < width || remaining < width) return false;
    if (size == 1) size = lnd_mp4_u64(data + 8);
    else if (!size) size = remaining;
    if (size < width || size > remaining) return false;
    *header = (lnd_mp4_header){.size = size, .type = lnd_mp4_u32(data + 4), .bytes = width};
    return true;
}

bool lnd_mp4_next(const uint8_t *data, size_t bytes, size_t *at, lnd_mp4_box *box) {
    lnd_mp4_header header;
    if (*at > bytes || !lnd_mp4_box_bounds(data + *at, bytes - *at, bytes - *at, &header)) return false;
    *box = (lnd_mp4_box){
        .data = data + *at + header.bytes, .bytes = (size_t)header.size - header.bytes, .offset = *at, .end = *at + (size_t)header.size, .type = header.type};
    *at = box->end;
    return true;
}

bool lnd_mp4_child(const lnd_mp4_box *parent, uint32_t type, lnd_mp4_box *out) {
    size_t at = 0;
    lnd_mp4_box box;
    while (at < parent->bytes) {
        if (!lnd_mp4_next(parent->data, parent->bytes, &at, &box)) return false;
        if (box.type == type) {
            *out = box;
            return true;
        }
    }
    return false;
}

bool lnd_mp4_descriptors(const uint8_t *data, size_t bytes, unsigned depth, lnd_mp4_config *config) {
    if (depth > 8) return false;
    size_t at = 0;
    while (at < bytes) {
        uint8_t tag = data[at++];
        uint32_t length = 0;
        unsigned count = 0;
        uint8_t byte;
        do {
            if (at == bytes || count++ == 4) return false;
            byte = data[at++];
            length = length << 7 | (byte & 127);
        } while (byte & 128);
        if (length > bytes - at) return false;
        size_t header = 0;
        if (tag == 3) {
            if (length < 3) return false;
            header = 3;
            uint8_t flags = data[at + 2];
            if (flags & 128) header += 2;
            if (flags & 64) {
                if (header >= length) return false;
                header += 1 + data[at + header];
            }
            if (flags & 32) header += 2;
        } else if (tag == 4) {
            if (length < 13) return false;
            config->object_type = data[at];
            header = 13;
        } else if (tag == 5) {
            config->data = data + at;
            config->size = length;
        }
        if (header && (header > length || !lnd_mp4_descriptors(data + at + header, length - header, depth + 1, config))) return false;
        at += length;
    }
    return true;
}

bool lnd_mp4_table_view(lnd_mp4_table *table, const uint8_t *data, size_t bytes, uint32_t count, uint32_t width) {
    if (!width || count > bytes / width) return false;
    *table = (lnd_mp4_table){.data = data, .count = count, .width = width};
    return true;
}

int32_t lnd_mp4_locations(const lnd_mp4_table *chunks, const lnd_mp4_table *runs, const lnd_mp4_table *sizes, uint32_t fixed, uint32_t count,
                          uint64_t packet_limit, lnd_mp4_store_location store, void *user) {
    if (!count || !chunks->count || !runs->count || runs->width != 12 || (chunks->width != 4 && chunks->width != 8) ||
        (!fixed && (sizes->width != 4 || sizes->count != count)))
        return LND_ERR_FORMAT;
    for (uint32_t i = 0; i < runs->count; i++) {
        const uint8_t *entry = runs->data + (size_t)i * 12;
        uint32_t first = lnd_mp4_u32(entry);
        if ((!i && first != 1) || (i && first <= lnd_mp4_u32(entry - 12)) || first > chunks->count || !lnd_mp4_u32(entry + 4) || lnd_mp4_u32(entry + 8) != 1)
            return LND_ERR_UNSUPPORTED;
    }
    uint32_t index = 0, run = 0;
    for (uint32_t chunk = 0; chunk < chunks->count; chunk++) {
        while (run + 1 < runs->count && lnd_mp4_u32(runs->data + (size_t)(run + 1) * 12) <= chunk + 1) run++;
        uint32_t n = lnd_mp4_u32(runs->data + 4 + (size_t)run * 12);
        if (n > count - index) return LND_ERR_FORMAT;
        const uint8_t *entry = chunks->data + (size_t)chunk * chunks->width;
        uint64_t offset = chunks->width == 8 ? lnd_mp4_u64(entry) : lnd_mp4_u32(entry);
        for (uint32_t j = 0; j < n; j++, index++) {
            uint32_t bytes = fixed ? fixed : lnd_mp4_u32(sizes->data + (size_t)index * 4);
            if (!bytes || bytes > packet_limit || offset > UINT64_MAX - bytes) return LND_ERR_FORMAT;
            store(user, index, offset, bytes);
            offset += bytes;
        }
    }
    return index == count ? LND_OK : LND_ERR_FORMAT;
}
