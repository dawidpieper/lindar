#pragma once
#include "src/platform.h"
struct LND_NODE;
typedef struct lnd_monitor_scope {
    struct lnd_monitor_scope *parent;
    uint64_t start;
    uint64_t children;
} lnd_monitor_scope;
void lnd_monitor_begin(struct LND_NODE *node, lnd_monitor_scope *scope);
void lnd_monitor_end(struct LND_NODE *node, lnd_monitor_scope *scope, uint32_t frames);
