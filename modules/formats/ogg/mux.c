#include "mux.h"

int32_t lnd_ogg_init(lnd_ogg_mux *m, lnd_io *io, int serial) {
    if (ogg_stream_init(&m->os, serial) != 0) return LND_ERR_OUT_OF_MEMORY;
    m->io = io;
    m->ready = true;
    return LND_OK;
}

int32_t lnd_ogg_packet(lnd_ogg_mux *m, ogg_packet *op) { return ogg_stream_packetin(&m->os, op) == 0 ? LND_OK : LND_ERR_OUT_OF_MEMORY; }

int32_t lnd_ogg_pages(lnd_ogg_mux *m, bool flush) {
    ogg_page og;
    for (;;) {
        int r = flush ? ogg_stream_flush(&m->os, &og) : ogg_stream_pageout(&m->os, &og);
        if (r == 0) return LND_OK;
        if (LND_IoWrite(m->io, og.header, (size_t)og.header_len) != (size_t)og.header_len) return LND_ERR_IO;
        if (LND_IoWrite(m->io, og.body, (size_t)og.body_len) != (size_t)og.body_len) return LND_ERR_IO;
    }
}

void lnd_ogg_clear(lnd_ogg_mux *m) {
    if (m->ready) ogg_stream_clear(&m->os);
    m->ready = false;
}
