#include "session.h"
#include "formats/decode/stream.h"

#include <string.h>

static int32_t lnd_media_packets(lnd_http_session *source, LND_DEMUX *demux, lnd_http_bytes *out) {
    int32_t r;
    LND_DEMUX_PACKET packet;
    while ((r = LND_DemuxRead(demux, &packet)) == LND_OK) {
        r = lnd_http_packet(source, &packet, out);
        if (r != LND_OK) return r;
    }
    return r == LND_DEMUX_END ? LND_OK : r;
}

int32_t lnd_http_media(lnd_http_session *source, LND_DEMUX **demux, uint64_t offset, int64_t time_us, const uint8_t *data, size_t bytes, const uint8_t *init,
                       size_t init_bytes, lnd_http_bytes *out) {
    while (bytes >= 10 && !memcmp(data, "ID3", 3)) {
        if ((data[6] | data[7] | data[8] | data[9]) & 0x80) return LND_ERR_FORMAT;
        size_t size = 10 + ((size_t)data[6] << 21 | (size_t)data[7] << 14 | (size_t)data[8] << 7 | data[9]);
        if (data[5] & 0x10) size += 10;
        if (size > bytes) return LND_ERR_FORMAT;
        lnd_http_id3(source, data, size, time_us);
        data += size;
        bytes -= size;
    }
    bool container = bytes >= 188 && data[0] == 0x47;
    if (bytes >= 8 && (!memcmp(data + 4, "styp", 4) || !memcmp(data + 4, "moof", 4) || !memcmp(data + 4, "ftyp", 4) || !memcmp(data + 4, "sidx", 4)))
        container = true;
    if (!container) return lnd_http_bytes_append(out, data, bytes) ? LND_OK : LND_ERR_OUT_OF_MEMORY;
    if (!*demux) {
        LND_DEMUX_OPTIONS options = {.packet_bytes = source->options.buffer.segment_bytes};
        *demux = LND_DemuxCreate(&options);
        if (!*demux) return LND_ERR_OUT_OF_MEMORY;
    }
    int32_t r;
    if (init_bytes && (r = LND_DemuxSetInit(*demux, init, init_bytes)) != LND_OK) return r;
    r = LND_DemuxBeginAt(*demux, data, bytes, offset);
    if (r != LND_OK) return r;
    return lnd_media_packets(source, *demux, out);
}

int32_t lnd_http_media_end(lnd_http_session *source, LND_DEMUX *demux, lnd_http_bytes *out) {
    if (!demux) return LND_OK;
    int32_t r = LND_DemuxEnd(demux);
    return r == LND_OK ? lnd_media_packets(source, demux, out) : r;
}
