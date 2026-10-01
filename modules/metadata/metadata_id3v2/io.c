#include "metadata/metadata_io/io.h"
#include "lindar_metadata_id3v2.h"

static int32_t read_tag(LND_METADATA *m, LND_IO *input) {
    uint64_t size;
    int32_t r = lnd_tag_id3_size(input, &size);
    if (r) return r;
    if (!size) return LND_METADATA_ERR_NOT_FOUND;
    if (size > m->limits.bytes || size > SIZE_MAX) return LND_METADATA_ERR_LIMIT;
    void *tag = lnd_alloc((size_t)size);
    if (!tag) return LND_ERR_OUT_OF_MEMORY;
    r = lnd_tag_read_at(input, 0, tag, (size_t)size);
    if (!r) r = LND_MetadataId3v2Read(m, tag, (size_t)size, nullptr);
    lnd_free(tag);
    return r;
}

static int32_t create_tag(const LND_METADATA *m, const LND_METADATA_WRITE_OPTIONS *options, void **data, size_t *size) {
    return LND_MetadataId3v2CreateBuffer(m, options->version, options->flags, data, size);
}

static int32_t write_tag(const LND_METADATA *m, LND_IO *input, LND_IO *output, const LND_METADATA_WRITE_OPTIONS *options) {
    return lnd_tag_id3_write(m, input, output, options, false, create_tag);
}

const lnd_metadata_format lnd_metadata_id3v2_io = {.format = LND_METADATA_ID3V2, .read = read_tag, .write = write_tag};
