#pragma once

#include "src/platform.h"

typedef struct lnd_id3_tag {
    const uint8_t *body;
    size_t size, total;
    unsigned version, flags;
    uint8_t *copy;
} lnd_id3_tag;

typedef struct lnd_id3_iterator {
    const uint8_t *data;
    size_t size, position;
    unsigned version;
    bool unsynchronized;
    uint8_t *copy;
} lnd_id3_iterator;

typedef struct lnd_id3_frame {
    char id[5];
    const uint8_t *data;
    size_t size;
    uint16_t flags;
    bool opaque;
} lnd_id3_frame;

bool lnd_id3_valid_id(const char *id, size_t size);
bool lnd_id3_syncsafe(const uint8_t *data, uint32_t *value);
int32_t lnd_id3_header(const uint8_t *data, size_t size, lnd_id3_tag *tag);
int32_t lnd_id3_body(lnd_id3_tag *tag);
void lnd_id3_close(lnd_id3_tag *tag);
int32_t lnd_id3_next(lnd_id3_iterator *iterator, lnd_id3_frame *frame);
void lnd_id3_iterator_close(lnd_id3_iterator *iterator);
