#include "metadata/metadata_io/io.h"
#include "lindar_metadata_wave.h"
#include "formats/riff/read.h"

typedef struct riff_info {
    lnd_riff riff;
    uint64_t metadata_size;
    uint32_t sample_rate_hz;
    uint8_t header[12];
} riff_info;

static int32_t riff_header(LND_IO *input, riff_info *info) {
    int32_t r = lnd_tag_read_at(input, 0, info->header, 12);
    if (r) return r;
    r = lnd_riff_open(info->header, LND_IoGetSizeBytes(input), 0, &info->riff);
    if (r || !info->riff.rf64) return r;
    uint8_t ds[36];
    r = lnd_tag_read_at(input, 12, ds, sizeof ds);
    if (r) return r;
    uint64_t pos = 12;
    lnd_riff_chunk chunk;
    r = lnd_riff_next(&info->riff, ds, &pos, &chunk);
    return r ? r : lnd_riff_ds64(&info->riff, ds + 8, sizeof ds - 8, chunk.size);
}

static int32_t riff_chunk(LND_IO *input, const riff_info *info, uint64_t pos, uint8_t h[12], uint64_t *size, bool *metadata) {
    if (pos > info->riff.end || info->riff.end - pos < 8) return LND_ERR_FORMAT;
    int32_t r = lnd_tag_read_at(input, pos, h, 8);
    if (r) return r;
    lnd_riff_chunk chunk;
    uint64_t next = pos;
    r = lnd_riff_next(&info->riff, h, &next, &chunk);
    if (r) return r;
    *size = chunk.size;
    *metadata = !memcmp(h, "cue ", 4) || !memcmp(h, "CSET", 4) || !memcmp(h, "bext", 4) || !memcmp(h, "iXML", 4) || !memcmp(h, "axml", 4) ||
                !memcmp(h, "cart", 4) || !memcmp(h, "id3 ", 4) || !memcmp(h, "ID3 ", 4);
    if (!memcmp(h, "LIST", 4)) {
        if (*size < 4) return LND_ERR_FORMAT;
        r = lnd_tag_read_at(input, pos + 8, h + 8, 4);
        *metadata = !memcmp(h + 8, "INFO", 4) || !memcmp(h + 8, "adtl", 4);
    }
    return r;
}

static int32_t lnd_tag_riff_read(LND_METADATA *m, LND_IO *input) {
    riff_info info = {0};
    int32_t r = riff_header(input, &info);
    if (r) return r;
    lnd_tag_buffer b = {.limit = m->limits.bytes};
    const uint8_t header[12] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
    lnd_tag_append(&b, header, sizeof header);
    for (uint64_t pos = 12; !r && pos < info.riff.end;) {
        uint8_t h[12];
        uint64_t size;
        bool metadata;
        r = riff_chunk(input, &info, pos, h, &size, &metadata);
        if (r) break;
        if (metadata || !memcmp(h, "fmt ", 4)) {
            uint64_t count = size + 8 + (size & 1);
            if (count > b.limit - b.size || count > SIZE_MAX) {
                r = LND_METADATA_ERR_LIMIT;
                break;
            }
            size_t start = b.size;
            uint8_t scratch[4096];
            for (uint64_t offset = 0; !r && offset < count;) {
                size_t n = count - offset < sizeof scratch ? (size_t)(count - offset) : sizeof scratch;
                r = lnd_tag_read_at(input, pos + offset, scratch, n);
                if (!r) lnd_tag_append(&b, scratch, n);
                if (b.error) r = b.error;
                offset += n;
            }
            if (!r && b.size - start != count) r = LND_ERR_IO;
        }
        pos += 8 + size + (size & 1);
    }
    if (!r) r = b.error;
    if (!r && b.size - 8 > UINT32_MAX) r = LND_METADATA_ERR_LIMIT;
    if (!r) {
        lnd_tag_put32(b.data + 4, (uint32_t)(b.size - 8), false);
        r = LND_MetadataWaveRead(m, b.data, b.size);
        if (!r && info.riff.rf64) m->version = 64;
    }
    lnd_free(b.data);
    return r;
}

static int32_t lnd_tag_riff_write(const LND_METADATA *m, LND_IO *input, LND_IO *output, uint32_t flags) {
    riff_info info = {0};
    int32_t r = riff_header(input, &info);
    if (r) return r;
    for (uint64_t pos = 12; !r && pos < info.riff.end;) {
        uint8_t h[12];
        uint64_t size;
        bool metadata;
        r = riff_chunk(input, &info, pos, h, &size, &metadata);
        if (r) break;
        if (metadata) info.metadata_size += 8 + size + (size & 1);
        if (!memcmp(h, "fmt ", 4)) {
            uint8_t fmt[16];
            if (size < sizeof fmt) {
                r = LND_ERR_FORMAT;
                break;
            }
            r = lnd_tag_read_at(input, pos + 8, fmt, sizeof fmt);
            if (!r) info.sample_rate_hz = lnd_tag_get32(fmt + 4, false);
        }
        pos += 8 + size + (size & 1);
    }
    void *chunks = nullptr;
    size_t bytes = 0;
    if (!r && !info.sample_rate_hz) r = LND_ERR_FORMAT;
    if (!r) r = LND_MetadataWaveCreateBuffer(m, info.sample_rate_hz, flags, &chunks, &bytes);
    uint64_t end = info.riff.end - info.metadata_size;
    if (!r && bytes > UINT64_MAX - end) r = LND_METADATA_ERR_LIMIT;
    end += bytes;
    if (!r && !info.riff.rf64 && end - 8 > UINT32_MAX) r = LND_ERR_UNSUPPORTED;
    if (!r) {
        if (!info.riff.rf64) lnd_tag_put32(info.header + 4, (uint32_t)(end - 8), false);
        r = lnd_tag_write(output, info.header, 12);
    }
    if (!r && !info.riff.rf64) r = lnd_tag_write(output, chunks, bytes);
    for (uint64_t pos = 12; !r && pos < info.riff.end;) {
        uint8_t h[12];
        uint64_t size;
        bool metadata;
        r = riff_chunk(input, &info, pos, h, &size, &metadata);
        if (r) break;
        if (pos == 12 && info.riff.rf64) {
            uint8_t ds[16];
            r = lnd_tag_read_at(input, pos, ds, sizeof ds);
            if (!r) {
                lnd_tag_put32(ds + 8, (uint32_t)(end - 8), false);
                lnd_tag_put32(ds + 12, (uint32_t)((end - 8) >> 32), false);
                r = lnd_tag_write(output, ds, sizeof ds);
            }
            if (!r) r = lnd_tag_copy(input, output, pos + sizeof ds, size + 8 + (size & 1) - sizeof ds);
            if (!r) r = lnd_tag_write(output, chunks, bytes);
        } else if (!metadata) r = lnd_tag_copy(input, output, pos, size + 8 + (size & 1));
        pos += 8 + size + (size & 1);
    }
    if (!r && info.riff.end < LND_IoGetSizeBytes(input)) r = lnd_tag_copy(input, output, info.riff.end, LND_IoGetSizeBytes(input) - info.riff.end);
    lnd_free(chunks);
    return r;
}

static int32_t write_wave(const LND_METADATA *m, LND_IO *input, LND_IO *output, const LND_METADATA_WRITE_OPTIONS *options) {
    return lnd_tag_riff_write(m, input, output, options->flags);
}

const lnd_metadata_format lnd_metadata_wave_io = {.format = LND_METADATA_WAVE, .read = lnd_tag_riff_read, .write = write_wave};
