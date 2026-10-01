#include "walk.h"
#include "context.h"

static void lnd_walk_push(lnd_walk *walk, lnd_node *node, lnd_node *parent) {
    uint32_t d = walk->direction;
    node->visit[d] = walk->generation;
    node->visit_parent[d] = parent;
    node->visit_edge[d] = d ? node->outputs : node->inputs;
    node->visit_branch[d] = 0;
    walk->current = node;
}

lnd_walk lnd_walk_begin(lnd_node *root, bool inputs, bool branches) {
    uint32_t direction = inputs ? 0 : 1;
    uint64_t generation = ++lnd_graph_ctx.visit[direction];
    if (!generation) {
        for (lnd_node *n = lnd_graph_ctx.nodes; n; n = n->next) n->visit[direction] = 0;
        generation = ++lnd_graph_ctx.visit[direction];
    }
    lnd_walk walk = {.generation = generation, .direction = direction, .branches = branches};
    if (root) lnd_walk_push(&walk, root, nullptr);
    return walk;
}

static lnd_node *lnd_walk_child(lnd_walk *walk, lnd_node *node) {
    uint32_t d = walk->direction;
    if (!d) {
        if (node->type == LND_NODE_SOURCE || node->type == LND_NODE_PCM_INPUT) return nullptr;
        if (lnd_node_is_branch(node)) return node->visit_branch[d]++ ? nullptr : lnd_node_parent(node);
    } else if (walk->branches) {
        while (node->visit_branch[d] < lnd_splitter_outputs(node)) {
            lnd_node *branch = lnd_splitter_output(node, node->visit_branch[d]++);
            if (branch) return branch;
        }
    }
    lnd_edge *edge = node->visit_edge[d];
    if (!edge) return nullptr;
    node->visit_edge[d] = d ? edge->next_out : edge->next_in;
    return d ? edge->dst : edge->src;
}

lnd_node *lnd_walk_post(lnd_walk *walk) {
    while (walk->current) {
        lnd_node *node = walk->current;
        lnd_node *child = lnd_walk_child(walk, node);
        if (!child) {
            walk->current = node->visit_parent[walk->direction];
            return node;
        }
        if (child->visit[walk->direction] != walk->generation) lnd_walk_push(walk, child, node);
    }
    return nullptr;
}

lnd_node *lnd_walk_next(lnd_walk *walk) {
    if (!walk->started) {
        walk->started = true;
        return walk->current;
    }
    while (walk->current) {
        lnd_node *parent = walk->current;
        lnd_node *child = lnd_walk_child(walk, parent);
        if (!child) {
            walk->current = parent->visit_parent[walk->direction];
            continue;
        }
        if (child->visit[walk->direction] == walk->generation) continue;
        lnd_walk_push(walk, child, parent);
        return child;
    }
    return nullptr;
}
