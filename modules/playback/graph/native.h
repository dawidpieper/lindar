#pragma once

#include "node.h"
#include "gain.h"

typedef struct lnd_graph_pcm_buffer {
    LND_PCM pcm;
    void *planes[LND_MAX_CHANNELS];
} lnd_graph_pcm_buffer;

typedef union lnd_graph_accumulator {
    int64_t integer;
    double real;
} lnd_graph_accumulator;

struct lnd_native_node {
    lnd_graph_pcm_buffer cache;
    lnd_graph_pcm_buffer stage;
    lnd_graph_pcm_buffer input;
    float *floating;
    lnd_graph_accumulator *mix;
};

bool lnd_graph_pcm_init(lnd_graph_pcm_buffer *buffer, uint32_t channels, size_t frames);
bool lnd_graph_pcm_init_format(lnd_graph_pcm_buffer *buffer, uint32_t channels, size_t frames, int32_t format, int32_t layout);
bool lnd_node_pcm_result(lnd_node *node, int32_t result);
void lnd_graph_pcm_view(lnd_graph_pcm_buffer *buffer, uint32_t channels);
int32_t lnd_node_native_init(lnd_node *node);
void lnd_node_native_free(lnd_node *node);
int32_t lnd_node_native_ring(lnd_node *node);
void lnd_bus_render_pcm(lnd_node *node, const LND_PCM *pcm, size_t offset, uint32_t frames);
