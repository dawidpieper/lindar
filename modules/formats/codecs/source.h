#pragma once

#include "io/io.h"
#include "lindar_codecs.h"

LND_SOURCE *lnd_source_open_io(lnd_io *io, const char *extension, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options);

#if LND_MODULE_GRAPH
LND_SOURCE *lnd_source_open_graph(lnd_io *io, const char *extension, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options);
#endif
