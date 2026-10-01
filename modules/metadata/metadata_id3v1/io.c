#include "metadata/metadata_io/io.h"
#include "lindar_metadata_id3v1.h"

static int32_t read_tag(LND_METADATA *m, LND_IO *input) {
    uint8_t tag[128];
    if (LND_IoGetSizeBytes(input) < sizeof tag) return LND_METADATA_ERR_NOT_FOUND;
    int32_t r = lnd_tag_read_at(input, LND_IoGetSizeBytes(input) - sizeof tag, tag, sizeof tag);
    if (!r) r = memcmp(tag, "TAG", 3) ? LND_METADATA_ERR_NOT_FOUND : LND_MetadataId3v1Read(m, tag, sizeof tag);
    return r;
}

static int32_t create_tag(const LND_METADATA *m, const LND_METADATA_WRITE_OPTIONS *options, void **data, size_t *size) {
    return LND_MetadataId3v1CreateBuffer(m, options->flags, data, size);
}

static int32_t write_tag(const LND_METADATA *m, LND_IO *input, LND_IO *output, const LND_METADATA_WRITE_OPTIONS *options) {
    return lnd_tag_id3_write(m, input, output, options, true, create_tag);
}

const lnd_metadata_format lnd_metadata_id3v1_io = {.format = LND_METADATA_ID3V1, .read = read_tag, .write = write_tag};
