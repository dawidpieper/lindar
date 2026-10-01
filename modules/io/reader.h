#pragma once

#include "src/platform.h"

#include <string.h>

LND_INLINE uint16_t lnd_rd_u16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

LND_INLINE uint16_t lnd_rd_u16be(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

LND_INLINE uint32_t lnd_rd_u24le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; }

LND_INLINE uint32_t lnd_rd_u24be(const uint8_t *p) { return (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | (uint32_t)p[2]; }

LND_INLINE uint32_t lnd_rd_u32le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

LND_INLINE uint32_t lnd_rd_u32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3]; }

LND_INLINE uint64_t lnd_rd_u64le(const uint8_t *p) { return (uint64_t)lnd_rd_u32le(p) | (uint64_t)lnd_rd_u32le(p + 4) << 32; }

LND_INLINE uint64_t lnd_rd_u64be(const uint8_t *p) { return (uint64_t)lnd_rd_u32be(p) << 32 | (uint64_t)lnd_rd_u32be(p + 4); }

LND_INLINE int32_t lnd_rd_s24le(const uint8_t *p) { return (int32_t)(lnd_rd_u24le(p) << 8) >> 8; }

LND_INLINE int32_t lnd_rd_s24be(const uint8_t *p) { return (int32_t)(lnd_rd_u24be(p) << 8) >> 8; }

LND_INLINE bool lnd_tag_is(const uint8_t *p, const char *tag) { return memcmp(p, tag, 4) == 0; }

LND_INLINE void lnd_swap_block(uint8_t *data, uint32_t sample_bytes, size_t samples) {
    if (sample_bytes == 2) {
        for (size_t i = 0; i < samples; i++, data += 2) {
            uint8_t t = data[0];
            data[0] = data[1];
            data[1] = t;
        }
    } else if (sample_bytes == 3) {
        for (size_t i = 0; i < samples; i++, data += 3) {
            uint8_t t = data[0];
            data[0] = data[2];
            data[2] = t;
        }
    } else if (sample_bytes == 4) {
        for (size_t i = 0; i < samples; i++, data += 4) {
            uint8_t t0 = data[0], t1 = data[1];
            data[0] = data[3];
            data[1] = data[2];
            data[2] = t1;
            data[3] = t0;
        }
    } else if (sample_bytes == 8) {
        for (size_t i = 0; i < samples; i++, data += 8) {
            for (uint32_t k = 0; k < 4; k++) {
                uint8_t t = data[k];
                data[k] = data[7 - k];
                data[7 - k] = t;
            }
        }
    }
}

LND_INLINE double lnd_rd_f80be(const uint8_t *p) {
    uint32_t exponent = lnd_rd_u16be(p) & 0x7FFF;
    uint64_t mantissa = lnd_rd_u64be(p + 2);
    if (exponent == 0 && mantissa == 0) return 0.0;
    double value = (double)mantissa;
    int32_t shift = (int32_t)exponent - 16383 - 63;
    while (shift > 0) {
        value *= 2.0;
        shift--;
    }
    while (shift < 0) {
        value *= 0.5;
        shift++;
    }
    return p[0] & 0x80 ? -value : value;
}

typedef struct lnd_bits {
    const uint8_t *data;
    size_t size;
    size_t pos;
} lnd_bits;

LND_INLINE void lnd_bits_init(lnd_bits *b, const void *data, size_t size) {
    b->data = data;
    b->size = size;
    b->pos = 0;
}

LND_INLINE size_t lnd_bits_remaining(const lnd_bits *b) { return b->size * 8 - b->pos; }

LND_INLINE uint32_t lnd_bits_peek(const lnd_bits *b, uint32_t count) {
    uint64_t acc = 0;
    size_t byte = b->pos >> 3;
    uint32_t skip = (uint32_t)(b->pos & 7);
    uint32_t need = skip + count;
    uint32_t got = 0;
    while (got < need) {
        acc = acc << 8 | (byte < b->size ? b->data[byte] : 0);
        byte++;
        got += 8;
    }
    return (uint32_t)((acc >> (got - need)) & (count == 32 ? 0xFFFFFFFFu : ((1u << count) - 1)));
}

LND_INLINE uint32_t lnd_bits_read(lnd_bits *b, uint32_t count) {
    uint32_t v = lnd_bits_peek(b, count);
    b->pos += count;
    return v;
}

LND_INLINE int32_t lnd_bits_read_signed(lnd_bits *b, uint32_t count) {
    uint32_t v = lnd_bits_read(b, count);
    return count == 32 ? (int32_t)v : (int32_t)(v << (32 - count)) >> (32 - count);
}

LND_INLINE void lnd_bits_skip(lnd_bits *b, size_t count) { b->pos += count; }

LND_INLINE void lnd_bits_align(lnd_bits *b) { b->pos = (b->pos + 7) & ~(size_t)7; }

LND_INLINE uint32_t lnd_bits_unary(lnd_bits *b) {
    uint32_t n = 0;
    while (lnd_bits_remaining(b) && lnd_bits_read(b, 1) == 0) n++;
    return n;
}
