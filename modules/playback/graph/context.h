#pragma once

#include "src/context.h"

typedef struct lnd_graph_context {
    struct LND_NODE *nodes;
    struct LND_NODE *pending_head;
    struct LND_NODE *pending_tail;
    size_t pending_count;
    uint64_t visit[2];
    struct lnd_graph_source *sources;
} lnd_graph_context;

extern lnd_graph_context lnd_graph_ctx;

void lnd_context_gc(void);
