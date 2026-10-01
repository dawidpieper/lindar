#pragma once
#include "metadata/internal.h"
#include "lindar_metadata_io.h"
#include "io/io.h"
int32_t lnd_tag_read_at(LND_IO *io, uint64_t offset, void *data, size_t size);
int32_t lnd_tag_write(LND_IO *io, const void *data, size_t size);
int32_t lnd_tag_copy(LND_IO *input, LND_IO *output, uint64_t offset, uint64_t size);
typedef struct lnd_metadata_format {
    int32_t format;
    int32_t (*read)(LND_METADATA *metadata, LND_IO *input);
    int32_t (*write)(const LND_METADATA *metadata, LND_IO *input, LND_IO *output, const LND_METADATA_WRITE_OPTIONS *options);
} lnd_metadata_format;

typedef int32_t (*lnd_tag_create_proc)(const LND_METADATA *metadata, const LND_METADATA_WRITE_OPTIONS *options, void **data, size_t *size);
int32_t lnd_tag_id3_size(LND_IO *input, uint64_t *length);
int32_t lnd_tag_id3_write(const LND_METADATA *metadata, LND_IO *input, LND_IO *output, const LND_METADATA_WRITE_OPTIONS *options,
                        bool suffix, lnd_tag_create_proc create);
