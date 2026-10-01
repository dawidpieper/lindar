#include "split.h"
#include "native.h"
#include "src/alloc.h"
#include "src/error.h"
#include "src/pcm.h"

#include <string.h>

static void lnd_split_arm(lnd_split_node *b) {
    if (b->armed || !b->splitter) return;
    b->cursor = b->splitter->produced;
    b->armed = true;
}

static void lnd_split_finish(lnd_split_node *b, uint64_t before, uint32_t got) {
    lnd_add(&b->pos, got);
    if (b->cursor > before + got) lnd_add(&b->dropped, b->cursor - before - got);
    int32_t status = lnd_node_status(b->splitter, &b->cursor);
    lnd_store(&b->base.status, status);
    int32_t state = lnd_load(&b->state);
    if (state == LND_SOUND_PLAYING || state == LND_SOUND_STALLED)
        lnd_store(&b->state, status == LND_SOURCE_EOF || status < 0 ? LND_SOUND_STOPPED : status == LND_SOURCE_WAITING ? LND_SOUND_STALLED : LND_SOUND_PLAYING);
}

static void lnd_split_render(lnd_node *n, float *dst, uint32_t frames) {
    lnd_split_node *b = (lnd_split_node *)n;
    n->rendered = 0;
    if (!lnd_load_relaxed(&n->active) || !b->splitter) {
        lnd_store(&n->status, b->splitter ? LND_SOURCE_WAITING : LND_SOURCE_EOF);
        memset(dst, 0, (size_t)frames * n->channels * sizeof(float));
        return;
    }
    lnd_split_arm(b);
    uint64_t before = b->cursor;
    n->rendered = lnd_node_pull(b->splitter, &b->cursor, dst, frames);
    lnd_split_finish(b, before, n->rendered);
}

static void lnd_split_command(lnd_node *n, const lnd_cmd *c, bool immediate) {
    LND_UNUSED(immediate);
    lnd_split_node *b = (lnd_split_node *)n;
    switch (c->op) {
    case LND_OP_START:
    case LND_OP_RESUME:
        lnd_split_arm(b);
        lnd_store(&n->active, 1);
        break;
    case LND_OP_PAUSE:
        lnd_store(&n->active, 0);
        break;
    case LND_OP_STOP:
        lnd_store(&n->active, 0);
        b->armed = false;
        lnd_store(&b->pos, 0);
        break;
    default:
        break;
    }
}

static void lnd_split_destroy(lnd_node *n) {
    lnd_split_node *b = (lnd_split_node *)n;
    if (b->splitter) {
        lnd_splitter_node *s = (lnd_splitter_node *)b->splitter;
        if (b->index < s->count && s->branches[b->index] == n) s->branches[b->index] = nullptr;
    }
}

static void lnd_split_render_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_split_node *b = (lnd_split_node *)n;
    n->rendered = 0;
    if (!lnd_load_relaxed(&n->active) || !b->splitter) {
        lnd_store(&n->status, b->splitter ? LND_SOURCE_WAITING : LND_SOURCE_EOF);
        lnd_pcm_silence(pcm, offset, frames);
        return;
    }
    lnd_split_arm(b);
    uint64_t before = b->cursor;
    n->rendered = lnd_node_pull_pcm_valid(b->splitter, &b->cursor, pcm, offset, frames);
    lnd_split_finish(b, before, n->rendered);
}

static const lnd_node_vt lnd_split_vt = {
    .render = lnd_split_render,
    .render_pcm = lnd_split_render_pcm,
    .command = lnd_split_command,
    .destroy = lnd_split_destroy,
};

static void lnd_channel_render(lnd_node *n, float *dst, uint32_t frames) {
    lnd_split_node *b = (lnd_split_node *)n;
    lnd_node *s = b->splitter;
    n->rendered = 0;
    if (!s) {
        memset(dst, 0, (size_t)frames * sizeof(float));
        return;
    }
    uint32_t sc = s->channels;
    for (uint32_t done = 0; done < frames;) {
        uint32_t nb = LND_MIN(n->block, frames - done);
        uint32_t got = lnd_node_pull(s, &b->cursor, n->scratch, nb);
        n->rendered += got;
        const float *in = n->scratch + b->index;
        for (uint32_t f = 0; f < nb; f++) dst[done + f] = in[(size_t)f * sc];
        done += nb;
    }
    lnd_store(&n->status, lnd_node_status(s, &b->cursor));
    lnd_store_relaxed(&n->active, lnd_load_relaxed(&s->active));
}

static void lnd_channel_render_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_split_node *b = (lnd_split_node *)n;
    lnd_node *s = b->splitter;
    n->rendered = 0;
    if (!s) {
        lnd_pcm_silence(pcm, offset, frames);
        return;
    }
    lnd_graph_pcm_buffer *input = &n->native->input;
    lnd_graph_pcm_view(input, s->channels);
    n->rendered = lnd_node_pull_pcm_valid(s, &b->cursor, &input->pcm, 0, frames);
    size_t bytes = LND_PcmGetSampleBytes(pcm->format), source_stride = lnd_pcm_stride(&input->pcm), target_stride = lnd_pcm_stride(pcm);
    const uint8_t *from = lnd_pcm_at(&input->pcm, b->index, 0);
    uint8_t *to = lnd_pcm_at(pcm, 0, offset);
    for (uint32_t f = 0; f < frames; f++, from += source_stride, to += target_stride) memcpy(to, from, bytes);
    lnd_store(&n->status, lnd_node_status(s, &b->cursor));
    lnd_store_relaxed(&n->active, lnd_load_relaxed(&s->active));
}

static const lnd_node_vt lnd_channel_vt = {
    .render = lnd_channel_render,
    .render_pcm = lnd_channel_render_pcm,
    .destroy = lnd_split_destroy,
};

static void lnd_splitter_destroy(lnd_node *n) {
    lnd_splitter_node *s = (lnd_splitter_node *)n;
    for (uint32_t i = 0; i < s->count; i++) {
        if (s->branches[i]) ((lnd_split_node *)s->branches[i])->splitter = nullptr;
    }
    lnd_free(s->branches);
}

static const lnd_node_vt lnd_splitter_vt = {
    .render = lnd_bus_render,
    .render_pcm = lnd_bus_render_pcm,
    .destroy = lnd_splitter_destroy,
};

static lnd_node *lnd_splitter_make(uint32_t channels, uint32_t sample_rate_hz, uint32_t outputs, int32_t type) {
    bool stream = type == LND_NODE_SPLITTER;
    lnd_splitter_node *s = (lnd_splitter_node *)lnd_node_alloc(sizeof *s, &lnd_splitter_vt, type, channels, sample_rate_hz);
    if (!s) return nullptr;
    s->branches = lnd_alloc_zero(sizeof(lnd_node *) * outputs);
    if (!s->branches || lnd_node_ensure_ring(&s->base) != LND_OK) {
        lnd_node_destroy(&s->base);
        return nullptr;
    }
    s->count = outputs;
    s->base.extra_consumers = outputs;
    for (uint32_t i = 0; i < outputs; i++) {
        lnd_split_node *b = (lnd_split_node *)lnd_node_alloc(sizeof *b, stream ? &lnd_split_vt : &lnd_channel_vt, stream ? LND_NODE_SPLIT : LND_NODE_CHANNEL,
                                                             stream ? channels : 1, sample_rate_hz);
        if (!b || (!stream && lnd_node_reserve_input(&b->base, channels, false) != LND_OK)) {
            if (b) lnd_node_destroy(&b->base);
            for (uint32_t k = 0; k < i; k++) lnd_node_destroy(s->branches[k]);
            lnd_node_destroy(&s->base);
            return nullptr;
        }
        b->splitter = &s->base;
        b->index = i;
        b->armed = true;
        lnd_store_relaxed(&b->base.active, stream ? 0 : 1);
        lnd_store_relaxed(&b->state, LND_SOUND_STOPPED);
        s->branches[i] = &b->base;
    }
    return &s->base;
}

lnd_node *lnd_node_create_splitter(uint32_t channels, uint32_t sample_rate_hz, uint32_t outputs) {
    return lnd_splitter_make(channels, sample_rate_hz, outputs, LND_NODE_SPLITTER);
}

lnd_node *lnd_node_create_channel_splitter(uint32_t channels, uint32_t sample_rate_hz) {
    return lnd_splitter_make(channels, sample_rate_hz, channels, LND_NODE_CHANNEL_SPLITTER);
}

lnd_node *lnd_splitter_output(lnd_node *splitter, uint32_t index) {
    lnd_splitter_node *s = (lnd_splitter_node *)splitter;
    return lnd_node_is_splitter(splitter) && index < s->count ? s->branches[index] : nullptr;
}

uint32_t lnd_splitter_outputs(const lnd_node *splitter) { return lnd_node_is_splitter(splitter) ? ((const lnd_splitter_node *)splitter)->count : 0; }

lnd_node *lnd_node_parent(const lnd_node *n) {
    return n && (n->type == LND_NODE_SPLIT || n->type == LND_NODE_CHANNEL) ? ((const lnd_split_node *)n)->splitter : nullptr;
}

bool lnd_node_is_branch(const lnd_node *n) { return n && (n->type == LND_NODE_SPLIT || n->type == LND_NODE_CHANNEL); }

bool lnd_node_is_terminal(const lnd_node *n) { return n && (lnd_node_is_splitter(n) || n->type == LND_NODE_TERMINAL); }

uint64_t lnd_split_read(lnd_node *branch, float *dst, uint64_t frames) {
    lnd_split_node *b = (lnd_split_node *)branch;
    lnd_spinlock_lock(&branch->lock);
    lnd_node_process(branch, !lnd_load_relaxed(&branch->active));
    uint64_t done = 0;
    if (b->splitter) {
        lnd_split_arm(b);
        while (done < frames) {
            uint32_t want = (uint32_t)LND_MIN(frames - done, branch->block);
            uint64_t before = b->cursor;
            uint32_t got = lnd_node_pull(b->splitter, &b->cursor, dst + done * branch->channels, want);
            lnd_split_finish(b, before, got);
            done += got;
            if (got < want) break;
        }
    }
    lnd_spinlock_unlock(&branch->lock);
    return done;
}

int32_t lnd_split_state(const lnd_node *branch) {
    return branch && branch->type == LND_NODE_SPLIT ? lnd_load(&((const lnd_split_node *)branch)->state) : LND_SOUND_STOPPED;
}

void lnd_split_set_state(lnd_node *branch, int32_t state) { lnd_store(&((lnd_split_node *)branch)->state, state); }

uint64_t lnd_split_pos(const lnd_node *branch) { return branch && branch->type == LND_NODE_SPLIT ? lnd_load(&((const lnd_split_node *)branch)->pos) : 0; }

uint64_t lnd_split_read_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_split_node *b = (lnd_split_node *)n;
    lnd_spinlock_lock(&n->lock);
    lnd_node_process(n, !lnd_load_relaxed(&n->active));
    size_t done = 0;
    if (b->splitter) {
        lnd_split_arm(b);
        while (done < frames) {
            uint32_t want = (uint32_t)LND_MIN(frames - done, n->block);
            uint64_t before = b->cursor;
            uint32_t got = lnd_node_pull_pcm_valid(b->splitter, &b->cursor, pcm, offset + done, want);
            lnd_split_finish(b, before, got);
            done += got;
            if (got < want) break;
        }
    }
    lnd_spinlock_unlock(&n->lock);
    return done;
}
