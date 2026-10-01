#include "text.h"

#include <string.h>

bool lnd_text_utf8_next(const uint8_t **p, const uint8_t *end, uint32_t *code) {
    if (*p == end) return false;
    uint32_t c = *(*p)++;
    if (c < 128) {
        *code = c;
        return c != 0;
    }
    unsigned n = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
    if (!n || (size_t)(end - *p) < n) return false;
    uint32_t min = n == 1 ? 0x80 : n == 2 ? 0x800 : 0x10000;
    c &= (1u << (6 - n)) - 1;
    while (n--) {
        uint8_t b = *(*p)++;
        if ((b & 0xc0) != 0x80) return false;
        c = (c << 6) | (b & 63);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    *code = c;
    return true;
}

size_t lnd_text_utf8_code(uint8_t out[4], uint32_t code) {
    if (!code || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    size_t bytes = code < 128 ? 1 : code < 2048 ? 2 : code < 65536 ? 3 : 4;
    if (bytes == 1) out[0] = (uint8_t)code;
    else {
        for (size_t i = bytes - 1; i > 0; i--) {
            out[i] = (uint8_t)(128 | (code & 63));
            code >>= 6;
        }
        out[0] = (uint8_t)((bytes == 2 ? 0xc0 : bytes == 3 ? 0xe0 : 0xf0) | code);
    }
    return bytes;
}

int32_t lnd_text_open(lnd_text_reader *r, const uint8_t *data, size_t size, unsigned encoding) {
    if (encoding > 4 || (!data && size)) return LND_ERR_FORMAT;
    *r = (lnd_text_reader){.next = data, .end = data ? data + size : data, .encoding = encoding, .big_endian = encoding == 2};
    if (encoding == 1 && size) {
        if (size < 2 || !((data[0] == 0xff && data[1] == 0xfe) || (data[0] == 0xfe && data[1] == 0xff))) return LND_ERR_FORMAT;
        r->big_endian = data[0] == 0xfe;
        r->next += 2;
        size -= 2;
    }
    return (encoding == 1 || encoding == 2) && (size & 1) ? LND_ERR_FORMAT : LND_OK;
}

static uint16_t lnd_text_word(const uint8_t *p, bool be) { return be ? (uint16_t)(p[0] << 8 | p[1]) : (uint16_t)(p[1] << 8 | p[0]); }

int32_t lnd_text_next(lnd_text_reader *r, uint32_t *code) {
    if (r->next == r->end) return 0;
    if (r->encoding == 3) return lnd_text_utf8_next(&r->next, r->end, code) ? 1 : LND_ERR_FORMAT;
    uint32_t c;
    if (r->encoding == 0 || r->encoding == 4) {
        static const uint16_t cp1252[32] = {0x20ac, 0x81,   0x201a, 0x192,  0x201e, 0x2026, 0x2020, 0x2021, 0x2c6,  0x2030, 0x160,
                                            0x2039, 0x152,  0x8d,   0x17d,  0x8f,   0x90,   0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
                                            0x2013, 0x2014, 0x2dc,  0x2122, 0x161,  0x203a, 0x153,  0x9d,   0x17e,  0x178};
        c = *r->next++;
        if (r->encoding == 4 && c >= 0x80 && c < 0xa0) c = cp1252[c - 0x80];
    } else {
        if (r->end - r->next < 2) return LND_ERR_FORMAT;
        c = lnd_text_word(r->next, r->big_endian);
        r->next += 2;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (r->end - r->next < 2) return LND_ERR_FORMAT;
            uint32_t low = lnd_text_word(r->next, r->big_endian);
            if (low < 0xdc00 || low > 0xdfff) return LND_ERR_FORMAT;
            c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00;
            r->next += 2;
        } else if (c >= 0xdc00 && c <= 0xdfff) return LND_ERR_FORMAT;
    }
    *code = c;
    return c ? 1 : LND_ERR_FORMAT;
}

size_t lnd_text_terminator(const uint8_t *data, size_t size, unsigned encoding) {
    size_t width = encoding == 1 || encoding == 2 ? 2 : 1;
    for (size_t i = 0; i + width <= size; i += width)
        if (!data[i] && (width == 1 || !data[i + 1])) return i;
    return size;
}

int32_t lnd_text_decode(const uint8_t *data, size_t size, unsigned encoding, char *out, size_t capacity) {
    if (!out || !capacity) return LND_ERR_INVALID_ARG;
    out[0] = 0;
    lnd_text_reader reader;
    int32_t result = lnd_text_open(&reader, data, size, encoding);
    if (result) return result;
    size_t written = 0;
    bool full = false;
    uint32_t code;
    while ((result = lnd_text_next(&reader, &code)) > 0) {
        uint8_t encoded[4];
        size_t n = lnd_text_utf8_code(encoded, code);
        if (n >= capacity - written) full = true;
        if (!full) {
            memcpy(out + written, encoded, n);
            written += n;
        }
    }
    out[result < 0 ? 0 : written] = 0;
    return result;
}
