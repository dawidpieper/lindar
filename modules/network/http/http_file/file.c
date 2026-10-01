#include "file.h"
#include "lindar_file_io.h"

int32_t lnd_http_file_begin(lnd_http_session *s, LND_IO **out) {
    if (s->input_end || !s->options.file.max_bytes || s->info.live || s->response.icy_interval_bytes || !lnd_decoder_can_spill(s->decoder))
        return LND_ERR_UNSUPPORTED;
    size_t bytes = LND_DecoderGetBufferedBytes(s->decoder);
    if (bytes > s->options.file.max_bytes || (s->response.length_known && s->response.content_length_bytes > s->options.file.max_bytes)) return LND_ERR_UNSUPPORTED;
    LND_IO *io = LND_IoCreateTemporary(s->options.file.directory);
    if (!io) return LND_ErrorGetLast();
    int32_t r = lnd_decoder_spill(s->decoder, io);
    if (r != LND_OK) {
        LND_IoFree(io);
        return r;
    }
    *out = io;
    s->stats.file_bytes = bytes;
    return LND_OK;
}

int32_t lnd_http_file_write(lnd_http_session *s, LND_IO *io, const void *data, size_t bytes) {
    uint64_t size = LND_IoGetSizeBytes(io);
    if (size > s->options.file.max_bytes || bytes > s->options.file.max_bytes - size) return LND_ERR_UNSUPPORTED;
    if (LND_IoWrite(io, data, bytes) != bytes) return LND_ERR_IO;
    s->stats.file_bytes = size + bytes;
    return LND_OK;
}
