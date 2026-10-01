#include "io.h"
#include "lnd_metadata_formats.h"

int32_t lnd_tag_read_at(LND_IO *io, uint64_t offset, void *data, size_t size) {
    if (offset > LND_IoGetSizeBytes(io) || size > LND_IoGetSizeBytes(io) - offset) return LND_ERR_FORMAT;
    int32_t r = LND_IoSeekBytes(io, offset);
    if (r) return r;
    size_t done = 0;
    while (done < size) {
        int64_t n = LND_IoRead(io, (uint8_t *)data + done, size - done);
        if (n <= 0 || (uint64_t)n > size - done) return LND_ERR_IO;
        done += n;
    }
    return LND_OK;
}

int32_t lnd_tag_write(LND_IO *io, const void *data, size_t size) {
    size_t done = 0;
    while (done < size) {
        size_t n = LND_IoWrite(io, (const uint8_t *)data + done, size - done);
        if (!n || n > size - done) return LND_ERR_IO;
        done += n;
    }
    return LND_OK;
}

int32_t lnd_tag_copy(LND_IO *input, LND_IO *output, uint64_t offset, uint64_t size) {
    if (offset > LND_IoGetSizeBytes(input) || size > LND_IoGetSizeBytes(input) - offset) return LND_ERR_FORMAT;
    if (!size) return LND_OK;
    size_t capacity = size < 65536 ? (size_t)size : 65536;
    uint8_t *buffer = lnd_alloc(capacity);
    if (!buffer) return LND_ERR_OUT_OF_MEMORY;
    int32_t r = LND_OK;
    while (!r && size) {
        size_t n = size < capacity ? (size_t)size : capacity;
        r = lnd_tag_read_at(input, offset, buffer, n);
        if (!r) r = lnd_tag_write(output, buffer, n);
        offset += n;
        size -= n;
    }
    lnd_free(buffer);
    return r;
}

static int32_t detect(LND_IO *input) {
    uint8_t h[12] = {0};
    size_t n = LND_IoGetSizeBytes(input) < sizeof h ? (size_t)LND_IoGetSizeBytes(input) : sizeof h;
    int32_t r = lnd_tag_read_at(input, 0, h, n);
    if (r) return r;
    if (n >= 3 && !memcmp(h, "ID3", 3)) return LND_METADATA_ID3V2;
    if (n >= 4 && !memcmp(h, "fLaC", 4)) return LND_METADATA_FLAC;
    if (n >= 4 && !memcmp(h, "OggS", 4)) {
        uint8_t count, signature[8];
        r = lnd_tag_read_at(input, 26, &count, 1);
        if (!r) r = lnd_tag_read_at(input, 27 + count, signature, sizeof signature);
        if (r) return r;
        if (!memcmp(signature, "OpusHead", 8)) return LND_METADATA_OPUS;
        if (!memcmp(signature, "\1vorbis", 7)) return LND_METADATA_VORBIS;
        return LND_METADATA_ERR_NOT_FOUND;
    }
    if (n >= 12 && !memcmp(h + 8, "WAVE", 4)) return LND_METADATA_WAVE;
    if (LND_IoGetSizeBytes(input) >= 128) {
        r = lnd_tag_read_at(input, LND_IoGetSizeBytes(input) - 128, h, 3);
        if (r) return r;
        if (!memcmp(h, "TAG", 3)) return LND_METADATA_ID3V1;
    }
    return LND_METADATA_ERR_NOT_FOUND;
}

static const lnd_metadata_format *lnd_metadata_find_format(int32_t format) {
    for (size_t i = 0; lnd_metadata_formats[i]; i++)
        if (lnd_metadata_formats[i]->format == format) return lnd_metadata_formats[i];
    return nullptr;
}

bool LND_MetadataFormatCanRead(int32_t format) {
    const lnd_metadata_format *impl = lnd_metadata_find_format(format);
    return impl && impl->read;
}

bool LND_MetadataFormatCanWrite(int32_t format) {
    const lnd_metadata_format *impl = lnd_metadata_find_format(format);
    return impl && impl->write;
}

static int32_t read_metadata(LND_METADATA *m, LND_IO *input, int32_t format) {
    if (format == LND_METADATA_AUTO) format = detect(input);
    if (format < 0) return format;
    const lnd_metadata_format *impl = lnd_metadata_find_format(format);
    return impl && impl->read ? impl->read(m, input) : LND_ERR_UNSUPPORTED;
}

static int32_t restore(LND_IO *input, uint64_t position, int32_t status, int32_t result) {
    int32_t r = LND_IoSeekBytes(input, position);
    if (!r) input->status = status;
    return r ? r : result;
}

int32_t LND_MetadataReadIo(LND_METADATA *m, LND_IO *input, int32_t format) {
    if (!m || !input || format < 0 || format > LND_METADATA_FLAC) return LND_ERR_INVALID_ARG;
    if (!LND_IoCanSeek(input) || LND_IoGetSizeBytes(input) == LND_IO_SIZE_UNKNOWN) return LND_ERR_UNSUPPORTED;
    uint64_t position = LND_IoGetPositionBytes(input);
    int32_t status = LND_IoGetStatus(input);
    LND_METADATA *tmp = LND_MetadataCreate(&m->limits);
    if (!tmp) return LND_ERR_OUT_OF_MEMORY;
    int32_t r = read_metadata(tmp, input, format);
    r = restore(input, position, status, r);
    return lnd_tag_commit(m, tmp, r);
}

int32_t LND_MetadataReadMemory(LND_METADATA *m, const void *data, size_t size, int32_t format) {
    if (!m || (!data && size)) return LND_ERR_INVALID_ARG;
    LND_IO *io = lnd_io_open_memory(data, size);
    if (!io) return LND_ERR_OUT_OF_MEMORY;
    int32_t r = LND_MetadataReadIo(m, io, format);
    LND_IoFree(io);
    return r;
}

int32_t LND_MetadataWriteIo(const LND_METADATA *m, LND_IO *input, LND_IO *output, const LND_METADATA_WRITE_OPTIONS *options) {
    if (!m || !input || !output || input == output || LND_IoGetPositionBytes(output) || LND_IoGetSizeBytes(output) || !output->writable)
        return LND_ERR_INVALID_ARG;
    if (!LND_IoCanSeek(input) || LND_IoGetSizeBytes(input) == LND_IO_SIZE_UNKNOWN) return LND_ERR_UNSUPPORTED;
    uint32_t flags = options ? options->flags : 0;
    int32_t format = options ? options->format : LND_METADATA_AUTO;
    if (format < 0 || format > LND_METADATA_FLAC || (flags & ~LND_METADATA_DROP_UNSUPPORTED)) return LND_ERR_INVALID_ARG;
    uint64_t position = LND_IoGetPositionBytes(input);
    int32_t status = LND_IoGetStatus(input);
    if (!format) format = detect(input);
    LND_METADATA_WRITE_OPTIONS config = {.format = format, .version = options ? options->version : 0, .flags = flags};
    const lnd_metadata_format *impl = lnd_metadata_find_format(format);
    int32_t r = format < 0 ? format : impl && impl->write ? impl->write(m, input, output, &config) : LND_ERR_UNSUPPORTED;
    return restore(input, position, status, r);
}
