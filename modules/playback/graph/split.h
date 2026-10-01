#pragma once

#include "node.h"

typedef struct lnd_splitter_node {
    lnd_node base;
    lnd_node **branches;
    uint32_t count;
} lnd_splitter_node;

typedef struct lnd_split_node {
    lnd_node base;
    lnd_node *splitter;
    uint32_t index;
    uint64_t cursor;
    lnd_atomic_i32 state;
    lnd_atomic_u64 pos;
    lnd_atomic_u64 dropped;
    bool armed;
} lnd_split_node;

LND_INLINE bool lnd_node_is_splitter(const lnd_node *n) { return n && (n->type == LND_NODE_SPLITTER || n->type == LND_NODE_CHANNEL_SPLITTER); }

uint64_t lnd_split_read_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, size_t frames);
