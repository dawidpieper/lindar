#include "node.h"
#include "native.h"
#include "pcm/audio/channels.h"
#include "pcm/audio/simd.h"
#include "src/native.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/pcm.h"

#include <string.h>

uint32_t lnd_bus_pull_edge_pcm(lnd_node *n, lnd_edge *e, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    if (!e->resampler) return lnd_node_pull_pcm_valid(e->src, &e->cursor, pcm, offset, frames);
    lnd_spinlock_lock(&e->src->lock);
    lnd_node_process(e->src, false);
    lnd_native_sound *sound = (lnd_native_sound *)lnd_node_pcm_sound(e->src);
    uint64_t revision = sound ? lnd_load(&sound->source->revision) : lnd_load(&e->src->revision);
    if (revision != e->revision || (lnd_source_status(e->resampler) == LND_SOURCE_EOF && lnd_node_status(e->src, &e->cursor) != LND_SOURCE_EOF)) {
        e->revision = revision;
        e->cursor = lnd_load(&e->src->produced);
        lnd_resample_source_reset(e->resampler);
    }
    lnd_spinlock_unlock(&e->src->lock);
    int64_t read = lnd_source_read_pcm(e->resampler, pcm, offset, frames, false);
    if (read < 0) {
        lnd_store(&e->resampler->status, (int32_t)read);
        lnd_node_pcm_result(n, (int32_t)read);
    }
    uint32_t got = read > 0 ? (uint32_t)read : 0;
    if (got < frames) lnd_pcm_silence(pcm, offset + got, frames - got);
    return got;
}

uint32_t lnd_bus_pull_edge(lnd_node *n, lnd_edge *e, float *dst, uint32_t frames) {
    LND_PCM pcm = {.data = dst, .frames = frames, .channels = e->src->channels, .format = LND_FORMAT_F32};
    return lnd_bus_pull_edge_pcm(n, e, &pcm, 0, frames);
}

void lnd_bus_finish(lnd_node *n, uint32_t frames, uint32_t got) {
    bool ended = true;
    int32_t error = LND_OK;
    for (lnd_edge *e = n->inputs; e; e = e->next_in) {
        int32_t status = e->paused ? LND_SOURCE_WAITING : e->resampler ? lnd_source_status(e->resampler) : lnd_node_status(e->src, &e->cursor);
        if (status < 0) error = status;
        if (status != LND_SOURCE_EOF && status >= 0) ended = false;
    }
    bool mixer = n->type == LND_NODE_MIXER;
    bool continuous = mixer ? (n->mix_mode & LND_MIX_MODE_MASK) == LND_MIX_CONTINUOUS
                            : n->type != LND_NODE_SPLITTER && n->type != LND_NODE_CHANNEL_SPLITTER && n->type != LND_NODE_CHANNEL_MERGER;
    if (n->type == LND_NODE_PROCESSOR && (lnd_processor_procs(n)->flags & LND_PROCESSOR_BOUNDED)) continuous = false;
    bool eof = ended && (mixer ? (n->mix_mode & LND_MIX_END) != 0 : !continuous && n->inputs != nullptr);
    n->rendered = continuous && !eof ? frames : got;
    lnd_store(&n->status, error < 0 && !got ? error : eof ? LND_SOURCE_EOF : n->rendered == frames ? LND_SOURCE_READY : LND_SOURCE_WAITING);
    lnd_store_relaxed(&n->active, !eof || got != 0);
}

lnd_edge *lnd_bus_next(lnd_node *n, lnd_edge *e) {
    if (!e && n->clock) return n->clock;
    e = !e || e == n->clock ? n->inputs : e->next_in;
    return e && e == n->clock ? e->next_in : e;
}

bool lnd_bus_render_single(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_edge *e = n->inputs;
    if (!e || e->next_in || e->paused || e->matrix || e->src->channels != n->channels || n->type == LND_NODE_CHANNEL_MERGER) return false;
    uint32_t got = lnd_bus_pull_edge_pcm(n, e, pcm, offset, frames);
    size_t bytes = LND_PcmGetSampleBytes(pcm->format);
    if (pcm->format >= LND_FORMAT_F32 && pcm->layout == LND_LAYOUT_INTERLEAVED && lnd_pcm_stride(pcm) == bytes * pcm->channels) {
        uint8_t *data = lnd_pcm_at(pcm, 0, offset);
        size_t count = (size_t)frames * pcm->channels;
        if (pcm->format == LND_FORMAT_F32) {
            for (size_t f = 0; f < count; f++) {
                float value;
                memcpy(&value, data + f * sizeof value, sizeof value);
                value = 0.0f + value;
                memcpy(data + f * sizeof value, &value, sizeof value);
            }
        } else {
            for (size_t f = 0; f < count; f++) {
                double value;
                memcpy(&value, data + f * sizeof value, sizeof value);
                value = 0.0 + value;
                memcpy(data + f * sizeof value, &value, sizeof value);
            }
        }
    } else if (pcm->format >= LND_FORMAT_F32) {
        size_t stride = lnd_pcm_stride(pcm);
        for (uint32_t c = 0; c < pcm->channels; c++) {
            uint8_t *data = lnd_pcm_at(pcm, c, offset);
            if (pcm->format == LND_FORMAT_F32) {
                for (uint32_t f = 0; f < frames; f++, data += stride) {
                    float value;
                    memcpy(&value, data, sizeof value);
                    value = 0.0f + value;
                    memcpy(data, &value, sizeof value);
                }
            } else {
                for (uint32_t f = 0; f < frames; f++, data += stride) {
                    double value;
                    memcpy(&value, data, sizeof value);
                    value = 0.0 + value;
                    memcpy(data, &value, sizeof value);
                }
            }
        }
    }
    lnd_bus_finish(n, frames, got);
    if (got < frames && e == n->clock && (e->resampler ? lnd_source_status(e->resampler) : lnd_node_status(e->src, &e->cursor)) != LND_SOURCE_EOF) {
        n->rendered = LND_MIN(n->rendered, got);
        if (lnd_load(&n->status) >= 0) lnd_store(&n->status, LND_SOURCE_WAITING);
    }
    return true;
}

void lnd_bus_render(lnd_node *n, float *dst, uint32_t frames) {
    LND_PCM pcm = {.data = dst, .frames = frames, .channels = n->channels, .format = LND_FORMAT_F32};
    if (lnd_bus_render_single(n, &pcm, 0, frames)) return;
    uint32_t dc = n->channels, total = 0, limit = frames;
    memset(dst, 0, (size_t)frames * dc * sizeof(float));
    for (lnd_edge *e = lnd_bus_next(n, nullptr); e; e = lnd_bus_next(n, e)) {
        if (e->paused) continue;
        uint32_t sc = e->src->channels, got = 0;
        const float *matrix = e->matrix ? e->matrix : sc != dc ? lnd_load(&n->matrices[sc]) : nullptr;
        for (uint32_t done = 0; done < limit;) {
            uint32_t nb = LND_MIN(n->block, limit - done);
            uint32_t count = lnd_bus_pull_edge(n, e, n->scratch, nb);
            const float *mapped = n->scratch;
            if (sc != dc || e->matrix) {
                if (matrix)
                    lnd_channels_apply(matrix, n->scratch, sc, n->scratch_map, dc, nb);
                else
                    lnd_channels_map(n->scratch, sc, n->scratch_map, dc, nb);
                mapped = n->scratch_map;
            }
            lnd_simd.accumulate(dst + (size_t)done * dc, mapped, 1.0f, (size_t)nb * dc);
            got += count;
            done += nb;
            if (count < nb) break;
        }
        total = LND_MAX(total, got);
        if (e == n->clock && (e->resampler ? lnd_source_status(e->resampler) : lnd_node_status(e->src, &e->cursor)) != LND_SOURCE_EOF) limit = got;
    }
    lnd_bus_finish(n, frames, total);
    if (limit < frames) {
        n->rendered = LND_MIN(n->rendered, limit);
        if (lnd_load(&n->status) >= 0) lnd_store(&n->status, LND_SOURCE_WAITING);
    }
}

static const lnd_node_vt lnd_bus_vt = {
    .render = lnd_bus_render,
    .render_pcm = lnd_bus_render_pcm,
};

lnd_node *lnd_node_create_bus(uint32_t channels, uint32_t sample_rate_hz, lnd_instance *instance) {
    lnd_node *n = lnd_node_alloc(sizeof *n, &lnd_bus_vt, instance ? LND_NODE_DESTINATION : LND_NODE_BUS, channels, sample_rate_hz);
    if (!n) return nullptr;
    n->instance = instance;
    if (instance) n->clip = (int32_t)lnd_cfg_u32(LND_CFG_GRAPH_CLIP_MODE);
    return n;
}

static uint32_t lnd_mixer_count(lnd_node *n, uint32_t frames) {
    if ((n->mix_mode & LND_MIX_MODE_MASK) != LND_MIX_LOCKSTEP) return frames;
    return (uint32_t)LND_MIN(frames, lnd_node_inputs_available(n, false));
}

static void lnd_mixer_render(lnd_node *n, float *dst, uint32_t frames) {
    uint32_t count = lnd_mixer_count(n, frames);
    if (count)
        lnd_bus_render(n, dst, count);
    else
        lnd_bus_finish(n, frames, 0);
    if (count < frames) memset(dst + (size_t)count * n->channels, 0, (size_t)(frames - count) * n->channels * sizeof(float));
}

static void lnd_mixer_render_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    uint32_t count = lnd_mixer_count(n, frames);
    if (count)
        lnd_bus_render_pcm(n, pcm, offset, count);
    else
        lnd_bus_finish(n, frames, 0);
    if (count < frames) LND_PcmSilence(pcm, offset + count, frames - count);
}

static const lnd_node_vt lnd_mixer_vt = {
    .render = lnd_mixer_render,
    .render_pcm = lnd_mixer_render_pcm,
};

lnd_node *lnd_node_create_mixer(uint32_t channels, uint32_t sample_rate_hz, int32_t mode) {
    lnd_node *n = lnd_node_alloc(sizeof *n, &lnd_mixer_vt, LND_NODE_MIXER, channels, sample_rate_hz);
    if (!n) return nullptr;
    n->mix_mode = mode;
    return n;
}

static void lnd_merger_render(lnd_node *n, float *dst, uint32_t frames) {
    uint32_t dc = n->channels;
    memset(dst, 0, (size_t)frames * dc * sizeof(float));
    uint32_t total = 0;
    for (lnd_edge *e = n->inputs; e; e = e->next_in) {
        lnd_node *src = e->src;
        if (e->paused) continue;
        uint32_t got = 0;
        uint32_t sc = src->channels;
        uint32_t channel = e->channel < dc ? e->channel : dc - 1;
        for (uint32_t done = 0; done < frames;) {
            uint32_t nb = LND_MIN(n->block, frames - done);
            got += lnd_bus_pull_edge(n, e, n->scratch, nb);
            const float *mono = n->scratch;
            if (sc != 1) {
                if (e->mono)
                    lnd_channels_apply(e->mono, n->scratch, sc, n->scratch_map, 1, nb);
                else
                    lnd_channels_map(n->scratch, sc, n->scratch_map, 1, nb);
                mono = n->scratch_map;
            }
            float *out = dst + (size_t)done * dc + channel;
            for (uint32_t f = 0; f < nb; f++)
                out[(size_t)f * dc] += mono[f];
            done += nb;
        }
        total = LND_MAX(total, got);
    }
    lnd_bus_finish(n, frames, total);
}

static const lnd_node_vt lnd_merger_vt = {
    .render = lnd_merger_render,
    .render_pcm = lnd_bus_render_pcm,
};

lnd_node *lnd_node_create_merger(uint32_t channels, uint32_t sample_rate_hz) {
    return lnd_node_alloc(sizeof(lnd_node), &lnd_merger_vt, LND_NODE_CHANNEL_MERGER, channels, sample_rate_hz);
}

int32_t lnd_merger_set_channel(lnd_node *merger, lnd_node *src, uint32_t channel) {
    for (lnd_edge *e = merger->inputs; e; e = e->next_in) {
        if (e->src != src) continue;
        lnd_spinlock_lock(&merger->lock);
        e->channel = channel;
        lnd_spinlock_unlock(&merger->lock);
        return LND_OK;
    }
    return LND_ERR_INVALID_ARG;
}
