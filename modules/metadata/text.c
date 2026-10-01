#include "internal.h"
#include "utility/text/text.h"
#include <stdio.h>

bool lnd_tag_utf8(const void *data, size_t size) {
    if (!data) return !size;
    const uint8_t *p = data, *end = p + size;
    uint32_t c;
    while (p < end)
        if (!lnd_text_utf8_next(&p, end, &c)) return false;
    return true;
}

static void utf8_code(lnd_tag_buffer *b, uint32_t code) {
    uint8_t data[4];
    size_t size = lnd_text_utf8_code(data, code);
    if (!size) b->error = LND_ERR_FORMAT;
    else lnd_tag_append(b, data, size);
}

int32_t lnd_tag_decode(const uint8_t *data, size_t size, unsigned encoding, char **text) {
    if (size > (SIZE_MAX - 1) / 3) return LND_METADATA_ERR_LIMIT;
    lnd_text_reader reader;
    int32_t result = lnd_text_open(&reader, data, size, encoding);
    if (result) return result;
    lnd_tag_buffer b = {.limit = size * 3 + 1};
    uint32_t code;
    while (!b.error && (result = lnd_text_next(&reader, &code)) > 0) utf8_code(&b, code);
    if (!b.error && result < 0) b.error = result;
    lnd_tag_byte(&b, 0);
    size_t ignored;
    return lnd_tag_finish(&b, (void **)text, &ignored);
}

size_t lnd_tag_terminator(const uint8_t *data, size_t size, unsigned encoding) { return lnd_text_terminator(data, size, encoding); }

void lnd_tag_text(lnd_tag_buffer *b, const char *text, unsigned encoding, bool terminate) {
    if (!text) text = "";
    const uint8_t *p = (const uint8_t *)text, *end = p + strlen(text);
    if (encoding == 1) {
        lnd_tag_byte(b, 0xff);
        lnd_tag_byte(b, 0xfe);
    }
    while (!b->error && p < end) {
        uint32_t c;
        if (!lnd_text_utf8_next(&p, end, &c)) {
            b->error = LND_ERR_INVALID_ARG;
            break;
        }
        if (encoding == 0) {
            if (c > 255) {
                b->error = LND_ERR_UNSUPPORTED;
                break;
            }
            lnd_tag_byte(b, (uint8_t)c);
        } else if (encoding == 3)
            utf8_code(b, c);
        else {
            uint16_t words[2];
            unsigned n = 1;
            if (c < 0x10000)
                words[0] = (uint16_t)c;
            else {
                c -= 0x10000;
                words[0] = (uint16_t)(0xd800 | (c >> 10));
                words[1] = (uint16_t)(0xdc00 | (c & 1023));
                n = 2;
            }
            for (unsigned i = 0; i < n; ++i) {
                lnd_tag_byte(b, (uint8_t)(encoding == 2 ? words[i] >> 8 : words[i]));
                lnd_tag_byte(b, (uint8_t)(encoding == 2 ? words[i] : words[i] >> 8));
            }
        }
    }
    if (terminate) {
        lnd_tag_byte(b, 0);
        if (encoding == 1 || encoding == 2) lnd_tag_byte(b, 0);
    }
}

int32_t lnd_tag_uint(const char *text, uint64_t *value) {
    if (!text || !*text) return LND_ERR_FORMAT;
    uint64_t n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9' || n > (UINT64_MAX - (*p - '0')) / 10) return LND_ERR_FORMAT;
        n = n * 10 + *p - '0';
    }
    *value = n;
    return LND_OK;
}

int32_t lnd_tag_timestamp(const char *text, uint64_t *us) {
    uint64_t units[3] = {0};
    const char *p = text;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (units[i] > (UINT64_MAX - 9) / 10) return LND_ERR_FORMAT;
            units[i] = units[i] * 10 + (unsigned)(*p++ - '0');
            ++digits;
        }
        if (!digits || (i && units[i] >= 60) || (i < 2 && *p++ != ':')) return LND_ERR_FORMAT;
    }
    if (units[0] > (UINT64_MAX / 1000000 - 3599) / 3600) return LND_ERR_FORMAT;
    uint64_t fraction = 0, scale = 100000;
    if (*p == '.') {
        ++p;
        if (*p < '0' || *p > '9') return LND_ERR_FORMAT;
        while (*p >= '0' && *p <= '9') {
            fraction += (unsigned)(*p++ - '0') * scale;
            scale /= 10;
        }
    }
    if (*p) return LND_ERR_FORMAT;
    *us = (units[0] * 3600 + units[1] * 60 + units[2]) * 1000000 + fraction;
    return LND_OK;
}

void lnd_tag_time_string(uint64_t us, char text[40]) {
    uint64_t seconds = us / 1000000;
    snprintf(text, 40, "%02llu:%02u:%02u.%06u", (unsigned long long)(seconds / 3600), (unsigned)(seconds / 60 % 60), (unsigned)(seconds % 60),
             (unsigned)(us % 1000000));
}
