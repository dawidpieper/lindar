#pragma once

#include "lindar_codecs.h"
#include "lindar.h"

typedef struct file_mcu {
    LND_SOURCE *source;
    LND_SOUND *sound;
    LND_RENDERER *renderer;
    unsigned char *memory;
    size_t capacity, used, allocations;
} file_mcu;

int32_t file_mcu_configure(file_mcu *player, void *memory, size_t bytes);
int32_t file_mcu_init(file_mcu *player, const void *file, size_t bytes);
int32_t file_mcu_fill(file_mcu *player, int16_t *buffer, size_t frames);
void file_mcu_free(file_mcu *player);
