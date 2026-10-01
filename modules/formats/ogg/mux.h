#pragma once

#include <ogg/ogg.h>

#include "io/io.h"

typedef struct lnd_ogg_mux {
    ogg_stream_state os;
    lnd_io *io;
    bool ready;
} lnd_ogg_mux;

int32_t lnd_ogg_init(lnd_ogg_mux *m, lnd_io *io, int serial);
int32_t lnd_ogg_packet(lnd_ogg_mux *m, ogg_packet *op);
int32_t lnd_ogg_pages(lnd_ogg_mux *m, bool flush);
void lnd_ogg_clear(lnd_ogg_mux *m);
