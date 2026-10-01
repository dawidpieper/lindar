#include "render.h"
#include "lnd_modules.h"
#if LND_MODULE_MONITOR
#include "processing/monitor/monitor.h"
#endif
#include "native.h"
#include "src/notify.h"
#include "gain.h"
#include "pcm/audio/simd.h"
#include "playback/slide/slide.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/error.h"
#include "src/thread.h"
#include "src/pcm.h"
#include "src/sample.h"

#include <math.h>
#include <string.h>

void lnd_graph_pcm_view(lnd_graph_pcm_buffer *b, uint32_t channels) {
    b->pcm.channels = channels;
    for (uint32_t c = 0; c < channels; c++)
        b->planes[c] = (uint8_t *)b->pcm.data + c * b->pcm.frames * LND_PcmGetSampleBytes(b->pcm.format);
}

bool lnd_graph_pcm_init_format(lnd_graph_pcm_buffer *b, uint32_t channels, size_t frames, int32_t format, int32_t layout) {
    b->pcm = (LND_PCM){.planes = b->planes, .frames = frames, .channels = channels, .format = format, .layout = layout};
    size_t width = channels * LND_PcmGetSampleBytes(b->pcm.format);
    if (!channels || channels > LND_MAX_CHANNELS || !width || !frames || frames > SIZE_MAX / width ||
        (layout != LND_LAYOUT_INTERLEAVED && layout != LND_LAYOUT_PLANAR))
        return false;
    b->pcm.data = lnd_alloc_aligned(frames * width, LND_CACHE_LINE);
    if (!b->pcm.data) return false;
    lnd_graph_pcm_view(b, channels);
    return true;
}

bool lnd_graph_pcm_init(lnd_graph_pcm_buffer *b, uint32_t channels, size_t frames) {
    return lnd_graph_pcm_init_format(b, channels, frames, (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT), (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_LAYOUT));
}

int32_t lnd_node_native_init(lnd_node *n) {
    uint32_t layout = lnd_cfg_u32(LND_CFG_INTERNAL_LAYOUT);
    uint32_t layouts = n->vt->render_pcm ? (n->vt->pcm_layouts ? n->vt->pcm_layouts : LND_LAYOUT_MASK_ALL) : LND_LAYOUT_MASK_INTERLEAVED;
    if (!(layouts & (1u << layout))) layout = layouts & 1 ? LND_LAYOUT_INTERLEAVED : LND_LAYOUT_PLANAR;
    if (lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT) == LND_FORMAT_F32 && layout == LND_LAYOUT_INTERLEAVED) return LND_OK;
    n->native = lnd_alloc_zero(sizeof *n->native);
    if (!n->native) return LND_ERR_OUT_OF_MEMORY;
    struct lnd_native_node *p = n->native;
    bool mix = n->type == LND_NODE_BUS || n->type == LND_NODE_DESTINATION || n->type == LND_NODE_MIXER || n->type == LND_NODE_SPLITTER ||
               n->type == LND_NODE_CHANNEL_SPLITTER || n->type == LND_NODE_CHANNEL_MERGER || (n->type == LND_NODE_PROCESSOR && n->vt->render_pcm);
    bool input = mix || n->type == LND_NODE_CHANNEL;
    if (!lnd_graph_pcm_init(&p->stage, n->channels, n->block) || (input && !lnd_graph_pcm_init(&p->input, n->channels, n->block)))
        return LND_ERR_OUT_OF_MEMORY;
    p->stage.pcm.layout = (int32_t)layout;
    if (input) p->input.pcm.layout = (int32_t)layout;
    if (n->block > SIZE_MAX / n->channels / sizeof(lnd_graph_accumulator)) return LND_ERR_OUT_OF_MEMORY;
    if (mix) p->mix = lnd_alloc_aligned((size_t)n->block * n->channels * sizeof *p->mix, LND_CACHE_LINE);
    if (!n->vt->render_pcm) p->floating = lnd_alloc_aligned((size_t)n->block * n->channels * sizeof(float), LND_CACHE_LINE);
    return (!mix || p->mix) && (n->vt->render_pcm || p->floating) ? LND_OK : LND_ERR_OUT_OF_MEMORY;
}

int32_t lnd_node_reserve_input(lnd_node *n, uint32_t channels, bool mapping) {
    bool grow = channels > n->input_channels;
    mapping = mapping && n->scratch && !n->scratch_map;
    if (!grow && !mapping) return LND_OK;
    float *scratch = nullptr, *map = nullptr;
    lnd_graph_pcm_buffer input = {0};
    bool ok = true;
    if (grow && n->scratch) {
        scratch = lnd_alloc_aligned((size_t)n->block * channels * sizeof(float), LND_CACHE_LINE);
        ok = scratch != nullptr;
    }
    if (ok && grow && n->native && n->native->input.pcm.data) {
        const LND_PCM *pcm = &n->native->input.pcm;
        ok = lnd_graph_pcm_init_format(&input, channels, n->block, pcm->format, pcm->layout);
    }
    if (ok && mapping) {
        map = lnd_alloc_aligned((size_t)n->block * n->channels * sizeof(float), LND_CACHE_LINE);
        ok = map != nullptr;
    }
    if (ok) {
        lnd_spinlock_lock(&n->lock);
        if (scratch) {
            float *old = n->scratch;
            n->scratch = scratch;
            scratch = old;
        }
        if (input.pcm.data) {
            void *old = n->native->input.pcm.data;
            n->native->input = input;
            n->native->input.pcm.planes = n->native->input.planes;
            input.pcm.data = old;
        }
        if (map) n->scratch_map = map;
        if (grow) n->input_channels = channels;
        lnd_spinlock_unlock(&n->lock);
    } else
        lnd_free_aligned(map);
    lnd_free_aligned(scratch);
    lnd_free_aligned(input.pcm.data);
    return ok ? LND_OK : LND_ERR_OUT_OF_MEMORY;
}

void lnd_node_native_free(lnd_node *n) {
    struct lnd_native_node *p = n->native;
    if (!p) return;
    lnd_free_aligned(p->cache.pcm.data);
    lnd_free_aligned(p->stage.pcm.data);
    lnd_free_aligned(p->input.pcm.data);
    lnd_free_aligned(p->mix);
    lnd_free_aligned(p->floating);
    lnd_free(p);
    n->native = nullptr;
}

int32_t lnd_node_native_ring(lnd_node *n) {
    lnd_graph_pcm_buffer *b = &n->native->cache;
    if (b->pcm.data) return LND_OK;
    if (!lnd_graph_pcm_init(b, n->channels, n->out_cap)) return LND_ERR_OUT_OF_MEMORY;
    b->pcm.layout = n->native->stage.pcm.layout;
    lnd_pcm_silence(&b->pcm, 0, b->pcm.frames);
    lnd_spinlock_lock(&n->lock);
    n->out = b->pcm.data;
    lnd_spinlock_unlock(&n->lock);
    return LND_OK;
}

void lnd_graph_pcm_gain(const LND_PCM *pcm, size_t offset, size_t frames, float gain) {
    if (gain == 1.0f) return;
    if (gain == 0.0f) {
        lnd_pcm_silence(pcm, offset, frames);
        return;
    }
    size_t stride = lnd_pcm_stride(pcm);
    if (pcm->format == LND_FORMAT_F32 && pcm->layout == LND_LAYOUT_INTERLEAVED && stride == pcm->channels * sizeof(float) &&
        (uintptr_t)lnd_pcm_at(pcm, 0, offset) % alignof(float) == 0) {
        lnd_simd.scale((float *)lnd_pcm_at(pcm, 0, offset), gain, frames * pcm->channels);
        return;
    }
#if LND_MODULE_SIMD
    if (lnd_simd_pcm_scale(pcm, offset, frames, gain)) return;
#endif
    if (pcm->format <= LND_FORMAT_S32 && gain <= 65535.0f) {
        int64_t q = (int64_t)llround(LND_MIN((double)gain, 65535.0) * 65536.0);
        for (uint32_t c = 0; c < pcm->channels; c++) {
            uint8_t *data = lnd_pcm_at(pcm, c, offset);
            for (size_t f = 0; f < frames; f++, data += stride) {
                int64_t value = lnd_pcm_load_integer(data, pcm->format) * q;
                value = value >= 0 ? (value + 32768) / 65536 : -((-value + 32768) / 65536);
                lnd_pcm_store_integer(data, pcm->format, value);
            }
        }
    } else {
        for (uint32_t c = 0; c < pcm->channels; c++) {
            uint8_t *data = lnd_pcm_at(pcm, c, offset);
            for (size_t f = 0; f < frames; f++, data += stride)
                lnd_pcm_store_sample(data, pcm->format, lnd_pcm_load_sample(data, pcm->format) * gain);
        }
    }
}

bool lnd_node_pcm_result(lnd_node *n, int32_t result) {
    if (result == LND_OK) return true;
    n->rendered = 0;
    lnd_store(&n->status, result < 0 ? result : LND_ERR_IO);
    lnd_store(&n->active, 0);
    return false;
}

static void lnd_node_render_pcm_plain(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    n->rendered = frames;
#if LND_MODULE_MONITOR
    lnd_monitor_scope scope;
    lnd_monitor_begin(n, &scope);
#endif
    if (n->vt->render_pcm)
        n->vt->render_pcm(n, pcm, offset, frames);
    else {
        LND_PCM floating = {.data = n->native->floating, .frames = frames, .channels = n->channels, .format = LND_FORMAT_F32};
        n->vt->render(n, floating.data, frames);
        if (!lnd_node_pcm_result(n, lnd_pcm_convert(pcm, offset, &floating, 0, frames))) {
            lnd_pcm_silence(pcm, offset, frames);
#if LND_MODULE_MONITOR
            lnd_monitor_end(n, &scope, frames);
#endif
            return;
        }
    }
#if LND_MODULE_MONITOR
    lnd_monitor_end(n, &scope, frames);
#endif
    bool automated = false;
#if LND_MODULE_SLIDE
    if (n->slides) automated = lnd_slide_gain(n, pcm, offset, frames);
#endif
    if (!automated) lnd_graph_pcm_ramp(pcm, offset, frames, &n->gain, n->gain_step, n->gain_target, &n->gain_ramp);
    lnd_notify_node(n, n->rendered);
    int32_t clip = lnd_load_relaxed(&n->clip);
    if (clip == LND_CLIP_NONE || (clip == LND_CLIP_HARD && pcm->format <= LND_FORMAT_S32)) return;
    size_t stride = lnd_pcm_stride(pcm);
    for (uint32_t c = 0; c < pcm->channels; c++) {
        uint8_t *data = lnd_pcm_at(pcm, c, offset);
        for (uint32_t f = 0; f < frames; f++, data += stride) {
            double value = lnd_pcm_load_sample(data, pcm->format);
            if (clip == LND_CLIP_HARD)
                value = LND_CLAMP(value, -1.0, 1.0);
            else if (fabs(value) > 0.8)
                value = copysign(0.8 + 0.2 * tanh((fabs(value) - 0.8) / 0.2), value);
            lnd_pcm_store_sample(data, pcm->format, value);
        }
    }
}

static void lnd_node_render_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
#if LND_MODULE_SLIDE
    if (n->slides) {
        uint32_t done = 0;
        while (done < frames) {
            uint32_t count = lnd_slide_before(n, frames - done);
            lnd_node_render_pcm_plain(n, pcm, offset + done, count);
            lnd_slide_after(n, count);
            done += n->rendered;
            if (n->rendered < count) break;
        }
        n->rendered = done;
        return;
    }
#endif
    lnd_node_render_pcm_plain(n, pcm, offset, frames);
}

uint32_t lnd_node_pull_pcm(lnd_node *n, uint64_t *cursor, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    if (!lnd_pcm_writable(pcm, offset, frames) || pcm->channels != n->channels) {
        lnd_error(LND_ERR_INVALID_ARG);
        return 0;
    }
    return lnd_node_pull_pcm_valid(n, cursor, pcm, offset, frames);
}

uint32_t lnd_node_pull_pcm_valid(lnd_node *n, uint64_t *cursor, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    if (!frames) return 0;
    if (!n->native) {
        bool direct = pcm->format == LND_FORMAT_F32 && pcm->layout == LND_LAYOUT_INTERLEAVED && lnd_pcm_stride(pcm) == n->channels * sizeof(float) &&
                      (uintptr_t)lnd_pcm_at(pcm, 0, offset) % alignof(float) == 0;
        if (direct) return lnd_node_pull(n, cursor, (float *)lnd_pcm_at(pcm, 0, offset), frames);
        float samples[256];
        LND_PCM scratch = {.data = samples, .frames = 256 / n->channels, .channels = n->channels, .format = LND_FORMAT_F32};
        uint32_t done = 0;
        while (done < frames) {
            uint32_t want = (uint32_t)LND_MIN(frames - done, scratch.frames);
            uint32_t got;
            if (n->vt->render_planar && lnd_node_pull_planar(n, cursor, pcm, offset + done, want, &got)) {
                done += got;
                if (got < want) break;
                continue;
            }
            got = lnd_node_pull(n, cursor, samples, want);
            int32_t result = lnd_pcm_convert(pcm, offset + done, &scratch, 0, got);
            if (result != LND_OK) {
                lnd_spinlock_lock(&n->lock);
                lnd_node_pcm_result(n, result);
                lnd_spinlock_unlock(&n->lock);
                break;
            }
            done += got;
            if (got < want) break;
        }
        if (done < frames) lnd_pcm_silence(pcm, offset + done, frames - done);
        return done;
    }
    struct lnd_native_node *p = n->native;
    lnd_spinlock_lock(&n->lock);
    lnd_store_relaxed(&n->last_pull, lnd_render_begin());
    lnd_node_process(n, false);
    bool direct = pcm->format == p->stage.pcm.format && pcm->layout == p->stage.pcm.layout;
    uint64_t at = cursor ? *cursor : n->produced;
    uint32_t done = 0;
    while (done < frames) {
        uint32_t count = LND_MIN(frames - done, n->block), got;
        if (!n->out) {
            lnd_node_render_pcm(n, direct ? pcm : &p->stage.pcm, direct ? offset + done : 0, count);
            got = n->rendered;
            if (!direct && !lnd_node_pcm_result(n, lnd_pcm_convert(pcm, offset + done, &p->stage.pcm, 0, got))) break;
            n->produced += got;
            at = n->produced;
        } else {
            count = LND_MIN(count, n->out_cap / 2);
            if (n->produced - at > n->out_cap) at = n->produced - n->out_cap;
            while (at + count > n->produced) {
                uint32_t index = (uint32_t)(n->produced & (n->out_cap - 1));
                uint32_t want = (uint32_t)LND_MIN(at + count - n->produced, n->out_cap - index);
                lnd_node_render_pcm(n, &p->stage.pcm, 0, want);
                if (!lnd_node_pcm_result(n, lnd_pcm_convert(&p->cache.pcm, index, &p->stage.pcm, 0, n->rendered))) break;
                n->produced += n->rendered;
                if (n->rendered < want) break;
            }
            got = (uint32_t)LND_MIN(count, n->produced - at);
            uint32_t index = (uint32_t)(at & (n->out_cap - 1)), first = LND_MIN(got, n->out_cap - index);
            if (!lnd_node_pcm_result(n, lnd_pcm_convert(pcm, offset + done, &p->cache.pcm, index, first))) break;
            if (got > first && !lnd_node_pcm_result(n, lnd_pcm_convert(pcm, offset + done + first, &p->cache.pcm, 0, got - first))) break;
            at += got;
        }
        done += got;
        if (got < count) break;
    }
    if (done < frames) lnd_pcm_silence(pcm, offset + done, frames - done);
    if (cursor) *cursor = at;
    lnd_render_end();
    lnd_spinlock_unlock(&n->lock);
    return done;
}

static void lnd_bus_mix_integer(lnd_graph_accumulator *out, const uint8_t *data, size_t stride, uint32_t frames, int32_t format, int64_t gain) {
#define LND_MIX_INTEGER(FORMAT) \
    case FORMAT: \
        if (gain == 65536) { \
            for (uint32_t f = 0; f < frames; f++, data += stride) out[f].integer += lnd_pcm_integer_load(data, FORMAT); \
        } else { \
            for (uint32_t f = 0; f < frames; f++, data += stride) out[f].integer += lnd_pcm_integer_load(data, FORMAT) * gain / 65536; \
        } \
        break
    switch (format) {
        LND_MIX_INTEGER(LND_FORMAT_U8);
        LND_MIX_INTEGER(LND_FORMAT_S16);
        LND_MIX_INTEGER(LND_FORMAT_S24);
        LND_MIX_INTEGER(LND_FORMAT_S32);
    }
#undef LND_MIX_INTEGER
}

void lnd_bus_render_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    if (lnd_bus_render_single(n, pcm, offset, frames)) return;
    struct lnd_native_node *p = n->native;
    bool integer = pcm->format <= LND_FORMAT_S32;
    uint32_t dc = n->channels, total = 0, limit = frames;
    memset(p->mix, 0, (size_t)frames * dc * sizeof *p->mix);
    for (lnd_edge *e = lnd_bus_next(n, nullptr); e; e = lnd_bus_next(n, e)) {
        if (e->paused) continue;
        uint32_t got;
        uint32_t sc = e->src->channels;
        lnd_graph_pcm_view(&p->input, sc);
        LND_PCM *input = &p->input.pcm;
        got = lnd_bus_pull_edge_pcm(n, e, input, 0, limit);
        total = LND_MAX(total, got);
        const float *matrix = e->matrix ? e->matrix : sc != dc ? lnd_load(&n->matrices[sc]) : nullptr;
        size_t stride = lnd_pcm_stride(input);
        for (uint32_t c = 0; c < dc; c++)
            for (uint32_t k = 0; k < sc; k++) {
                double gain;
                if (n->type == LND_NODE_CHANNEL_MERGER)
                    gain = c == LND_MIN(e->channel, dc - 1) ? (e->mono ? e->mono[k] : 1.0 / sc) : 0.0;
                else
                    gain = matrix ? matrix[c * sc + k] : sc == dc ? (c == k) : (sc == 1 ? 1.0 : dc == 1 ? 1.0 / sc : c == k);
                if (!gain) continue;
                int64_t q = integer ? (int64_t)llround(gain * 65536.0) : 0;
                const uint8_t *data = lnd_pcm_at(input, k, 0);
                lnd_graph_accumulator *out = p->mix + (size_t)c * frames;
                if (integer) {
                    lnd_bus_mix_integer(out, data, stride, limit, input->format, q);
                } else if (input->format == LND_FORMAT_F32) {
                    for (uint32_t f = 0; f < limit; f++, data += stride) {
                        float value;
                        memcpy(&value, data, sizeof value);
                        out[f].real += (double)value * gain;
                    }
                } else if (input->format == LND_FORMAT_F64) {
                    for (uint32_t f = 0; f < limit; f++, data += stride) {
                        double value;
                        memcpy(&value, data, sizeof value);
                        out[f].real += value * gain;
                    }
                } else {
                    for (uint32_t f = 0; f < limit; f++, data += stride)
                        out[f].real += lnd_pcm_load_sample(data, input->format) * gain;
                }
            }
        if (e == n->clock && (e->resampler ? lnd_source_status(e->resampler) : lnd_node_status(e->src, &e->cursor)) != LND_SOURCE_EOF) limit = got;
    }
    size_t stride = lnd_pcm_stride(pcm);
    for (uint32_t c = 0; c < dc; c++) {
        uint8_t *data = lnd_pcm_at(pcm, c, offset);
        const lnd_graph_accumulator *values = p->mix + (size_t)c * frames;
        if (integer) {
            for (uint32_t f = 0; f < frames; f++, data += stride)
                lnd_pcm_store_integer(data, pcm->format, values[f].integer);
        } else if (pcm->format == LND_FORMAT_F32) {
            for (uint32_t f = 0; f < frames; f++, data += stride) {
                float value = (float)values[f].real;
                memcpy(data, &value, sizeof value);
            }
        } else {
            for (uint32_t f = 0; f < frames; f++, data += stride)
                memcpy(data, &values[f].real, sizeof(double));
        }
    }
    lnd_bus_finish(n, frames, total);
    if (limit < frames) {
        n->rendered = LND_MIN(n->rendered, limit);
        if (lnd_load(&n->status) >= 0) lnd_store(&n->status, LND_SOURCE_WAITING);
    }
}
