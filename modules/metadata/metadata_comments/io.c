#include "io.h"
#include "lindar_metadata_comments.h"
#include <ogg/ogg.h>

static int32_t flac(LND_METADATA *m, LND_IO *io) {
    uint8_t h[4];
    int32_t r = lnd_tag_read_at(io, 0, h, 4);
    if (r || memcmp(h, "fLaC", 4)) return r ? r : LND_ERR_FORMAT;
    uint64_t pos = 4;
    bool last = false;
    while (!last) {
        r = lnd_tag_read_at(io, pos, h, 4);
        if (r) return r;
        last = (h[0] & 128) != 0;
        size_t n = ((size_t)h[1] << 16) | ((size_t)h[2] << 8) | h[3];
        pos += 4;
        if (pos > LND_IoGetSizeBytes(io) || n > LND_IoGetSizeBytes(io) - pos) return LND_ERR_FORMAT;
        if ((h[0] & 127) == 4) {
            if (n > m->limits.bytes) return LND_METADATA_ERR_LIMIT;
            uint8_t *data = lnd_alloc(n ? n : 1);
            if (!data) return LND_ERR_OUT_OF_MEMORY;
            r = lnd_tag_read_at(io, pos, data, n);
            if (!r) r = LND_MetadataCommentsRead(m, data, n, LND_METADATA_FLAC);
            lnd_free(data);
            return r;
        }
        pos += n;
    }
    return LND_METADATA_ERR_NOT_FOUND;
}

static int32_t lnd_tag_comments_read(LND_METADATA *m, LND_IO *io, int32_t format) {
    if (format == LND_METADATA_FLAC) return flac(m, io);
    ogg_sync_state sync;
    ogg_stream_state stream;
    ogg_page page;
    ogg_packet packet;
    ogg_sync_init(&sync);
    bool initialized = false;
    unsigned packets = 0;
    uint64_t pos = 0;
    size_t total = 0;
    int32_t r = LND_METADATA_ERR_NOT_FOUND;
    while (pos < LND_IoGetSizeBytes(io)) {
        size_t n = (size_t)LND_MIN(LND_IoGetSizeBytes(io) - pos, 4096);
        char *buffer = ogg_sync_buffer(&sync, (long)n);
        if (!buffer) {
            r = LND_ERR_OUT_OF_MEMORY;
            break;
        }
        r = lnd_tag_read_at(io, pos, buffer, n);
        if (r) break;
        pos += n;
        ogg_sync_wrote(&sync, (long)n);
        int page_result;
        while ((page_result = ogg_sync_pageout(&sync, &page)) > 0) {
            if (!initialized) {
                if (!ogg_page_bos(&page)) {
                    r = LND_ERR_FORMAT;
                    goto done;
                }
                if (ogg_stream_init(&stream, ogg_page_serialno(&page))) {
                    r = LND_ERR_OUT_OF_MEMORY;
                    goto done;
                }
                initialized = true;
            }
            if (ogg_page_serialno(&page) != stream.serialno) continue;
            if ((size_t)page.body_len > m->limits.bytes - total) {
                r = LND_METADATA_ERR_LIMIT;
                goto done;
            }
            total += (size_t)page.body_len;
            if (ogg_stream_pagein(&stream, &page)) {
                r = LND_ERR_FORMAT;
                goto done;
            }
            int packet_result;
            while ((packet_result = ogg_stream_packetout(&stream, &packet)) > 0) {
                if (!packets++ && (packet.bytes < 7 || memcmp(packet.packet, "\1vorbis", 7))) {
                    r = LND_ERR_FORMAT;
                    goto done;
                }
                if (packets == 2) {
                    r = LND_MetadataCommentsRead(m, packet.packet, (size_t)packet.bytes, format);
                    goto done;
                }
            }
            if (packet_result < 0) {
                r = LND_ERR_FORMAT;
                goto done;
            }
        }
        if (page_result < 0) {
            r = LND_ERR_FORMAT;
            break;
        }
        r = LND_ERR_FORMAT;
    }
done:
    if (initialized) ogg_stream_clear(&stream);
    ogg_sync_clear(&sync);
    return r;
}

static int32_t read_opus(LND_METADATA *m, LND_IO *input) { return lnd_tag_ogg(m, input, nullptr, nullptr, 0); }
static int32_t read_vorbis(LND_METADATA *m, LND_IO *input) { return lnd_tag_comments_read(m, input, LND_METADATA_VORBIS); }
static int32_t read_flac(LND_METADATA *m, LND_IO *input) { return lnd_tag_comments_read(m, input, LND_METADATA_FLAC); }
static int32_t write_opus(const LND_METADATA *m, LND_IO *input, LND_IO *output, const LND_METADATA_WRITE_OPTIONS *options) {
    return lnd_tag_ogg(nullptr, input, output, m, options->flags);
}

const lnd_metadata_format lnd_metadata_opus_io = {.format = LND_METADATA_OPUS, .read = read_opus, .write = write_opus};
const lnd_metadata_format lnd_metadata_vorbis_io = {.format = LND_METADATA_VORBIS, .read = read_vorbis};
const lnd_metadata_format lnd_metadata_flac_io = {.format = LND_METADATA_FLAC, .read = read_flac};
