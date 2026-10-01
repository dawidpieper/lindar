#pragma once

#include "node.h"

typedef struct lnd_walk {
    lnd_node *current;
    uint64_t generation;
    uint32_t direction;
    bool branches;
    bool started;
} lnd_walk;

lnd_walk lnd_walk_begin(lnd_node *root, bool inputs, bool branches);
lnd_node *lnd_walk_next(lnd_walk *walk);
lnd_node *lnd_walk_post(lnd_walk *walk);
