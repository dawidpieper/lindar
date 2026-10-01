#pragma once

#include "src/platform.h"

typedef struct lnd_text_reader {
    const uint8_t *next;
    const uint8_t *end;
    unsigned encoding;
    bool big_endian;
} lnd_text_reader;

bool lnd_text_utf8_next(const uint8_t **next, const uint8_t *end, uint32_t *code);
size_t lnd_text_utf8_code(uint8_t out[4], uint32_t code);
int32_t lnd_text_open(lnd_text_reader *reader, const uint8_t *data, size_t size, unsigned encoding);
int32_t lnd_text_next(lnd_text_reader *reader, uint32_t *code);
size_t lnd_text_terminator(const uint8_t *data, size_t size, unsigned encoding);
int32_t lnd_text_decode(const uint8_t *data, size_t size, unsigned encoding, char *out, size_t capacity);
