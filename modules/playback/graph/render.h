#pragma once

#include "lindar.h"
#include "node.h"
#include "src/thread.h"

void lnd_renderers_detach_node(lnd_node *node);

#if LND_THREADS
extern thread_local uint32_t lnd_render_depth;
extern thread_local uint64_t lnd_render_timestamp;

LND_INLINE uint64_t lnd_render_begin(void) {
    if (!lnd_render_depth++) lnd_render_timestamp = lnd_time_ns();
    return lnd_render_timestamp;
}
LND_INLINE void lnd_render_end(void) { lnd_render_depth--; }
#else
LND_INLINE uint64_t lnd_render_begin(void) { return 0; }
LND_INLINE void lnd_render_end(void) {}
#endif
