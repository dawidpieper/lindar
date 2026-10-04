#pragma once

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static size_t page_size(const uint8_t *p) {
    size_t size = 27 + p[26];
    for (unsigned i = 0; i < p[26]; i++) size += p[27 + i];
    return size;
}

static void checksum(uint8_t *p, size_t size) {
    put32(p + 22, 0);
    uint32_t crc = 0;
    for (size_t i = 0; i < size; i++) {
        crc ^= (uint32_t)p[i] << 24;
        for (unsigned bit = 0; bit < 8; bit++) crc = crc << 1 ^ (crc >> 31 ? UINT32_C(0x04c11db7) : 0);
    }
    put32(p + 22, crc);
}

static uint8_t *long_fixture(size_t *bytes) {
    size_t size;
    uint8_t *tone = read_file("audiosamples/tone.opus", &size);
    if (!tone) return nullptr;
    size_t head = page_size(tone);
    head += page_size(tone + head);
    size_t page = page_size(tone + head);
    *bytes = head + page * 120;
    uint8_t *data = malloc(*bytes);
    CHECK(data != nullptr);
    if (data) {
        memcpy(data, tone, head);
        for (unsigned i = 0; i < 120; i++) {
            uint8_t *p = data + head + i * page;
            memcpy(p, tone + head, page);
            p[5] = i == 119 ? 4 : 0;
            uint64_t granule = (uint64_t)(i + 1) * 48000;
            for (unsigned j = 0; j < 8; j++) p[6 + j] = (uint8_t)(granule >> (j * 8));
            put32(p + 18, i + 2);
            checksum(p, page);
        }
    }
    free(tone);
    return data;
}

static uint8_t *chained_fixture(size_t *bytes) {
    size_t size;
    uint8_t *tone = read_file("audiosamples/tone.opus", &size);
    uint8_t *data = tone ? malloc(size * 6) : nullptr;
    CHECK(data != nullptr);
    if (data) {
        *bytes = size * 6;
        for (unsigned link = 0; link < 6; link++) {
            uint8_t *start = data + link * size;
            memcpy(start, tone, size);
            size_t packet = 27 + start[26];
            start[packet + 10] = 0xc0;
            start[packet + 11] = 0x03;
            for (size_t at = 0; at < size;) {
                uint8_t *page = start + at;
                size_t count = page_size(page);
                put32(page + 14, 1234 + link);
                uint64_t granule = 0;
                for (unsigned i = 0; i < 8; i++) granule |= (uint64_t)page[6 + i] << (i * 8);
                if (granule && granule != UINT64_MAX) {
                    granule += 48000;
                    for (unsigned i = 0; i < 8; i++) page[6 + i] = (uint8_t)(granule >> (i * 8));
                }
                checksum(page, count);
                at += count;
            }
        }
    }
    free(tone);
    return data;
}
