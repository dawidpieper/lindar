#include "context.h"
#include "node.h"
#include "render.h"
#include "sound.h"
#include "src/config.h"

lnd_graph_context lnd_graph_ctx;

void lnd_context_gc(void) { lnd_nodes_gc(); }

void lnd_graph_free(void) {
    lnd_sources_free_all();
    lnd_nodes_free_all();
}

void lnd_graph_update(void) {
    bool manual = lnd_cfg_u32(LND_CFG_RUN_MODE) != LND_MODE_REALTIME;
    lnd_nodes_maintain(manual, lnd_graph_ctx.pending_count);
}
