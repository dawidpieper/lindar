#include "io.h"

int32_t lnd_tag_id3_size(LND_IO *input, uint64_t *length) {
    *length = 0;
    uint8_t h[10];
    if (LND_IoGetSizeBytes(input) < 3) return LND_OK;
    int32_t r = lnd_tag_read_at(input, 0, h, 3);
    if (r || memcmp(h, "ID3", 3)) return r;
    r = lnd_tag_read_at(input, 0, h, sizeof h);
    uint32_t size;
    if (r) return r;
    if (h[3] < 2 || h[3] > 4 || !lnd_tag_syncsafe(h + 6, &size)) return LND_ERR_FORMAT;
    *length = (uint64_t)size + 10 + (h[3] == 4 && (h[5] & 16) ? 10 : 0);
    return *length <= LND_IoGetSizeBytes(input) ? LND_OK : LND_ERR_FORMAT;
}

int32_t lnd_tag_id3_write(const LND_METADATA *m, LND_IO *input, LND_IO *output, const LND_METADATA_WRITE_OPTIONS *options,
                         bool suffix, lnd_tag_create_proc create) {
    void *tag = nullptr;
    size_t bytes = 0;
    uint64_t start = 0, end = LND_IoGetSizeBytes(input);
    uint8_t signature[3] = {0};
    int32_t r = LND_OK;
    if (end >= 12) {
        uint8_t h[12];
        r = lnd_tag_read_at(input, 0, h, sizeof h);
        if (!r && (!memcmp(h, "OggS", 4) || !memcmp(h + 8, "WAVE", 4))) r = LND_ERR_UNSUPPORTED;
    }
    if (!r) r = lnd_tag_id3_size(input, &start);
    if (!r && end >= 128) {
        r = lnd_tag_read_at(input, end - 128, signature, 3);
        if (!r && !memcmp(signature, "TAG", 3)) end -= 128;
    }
    if (!r && end < start) r = LND_ERR_FORMAT;
    if (!r) r = create(m, options, &tag, &bytes);
    if (!r && !suffix) r = lnd_tag_write(output, tag, bytes);
    if (!r) r = lnd_tag_copy(input, output, start, end - start);
    if (!r && suffix) r = lnd_tag_write(output, tag, bytes);
    lnd_free(tag);
    return r;
}
