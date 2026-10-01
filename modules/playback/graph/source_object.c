#include "lindar_graph.h"
#include "context.h"
#include "sound.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/error.h"
#include "src/format.h"
#include "src/pcm.h"
#include "pcm/buffers/buffer.h"

static void lnd_source_obj_link(lnd_graph_source *s) {
    s->prev = nullptr;
    s->next = lnd_graph_ctx.sources;
    if (s->next) s->next->prev = s;
    lnd_graph_ctx.sources = s;
}

static void lnd_source_obj_unlink(lnd_graph_source *s) {
    if (s->prev)
        s->prev->next = s->next;
    else
        lnd_graph_ctx.sources = s->next;
    if (s->next) s->next->prev = s->prev;
    s->prev = s->next = nullptr;
}

static void lnd_source_obj_destroy(lnd_graph_source *s) {
    lnd_source_obj_unlink(s);
    if (s->node) lnd_node_destroy(s->node);
    lnd_source_free(s->source);
#if LND_MODULE_METADATA
    LND_MetadataFree(s->base.metadata);
#endif
    lnd_free(s);
}

void lnd_sources_free_all(void) {
    while (lnd_graph_ctx.sources)
        lnd_source_obj_destroy(lnd_graph_ctx.sources);
}

bool lnd_source_obj_valid(const lnd_graph_source *s) {
    if (!s) return false;
    if (s->view) return lnd_context_has_node(s->node) && s->node->view == s;
    for (lnd_graph_source *p = lnd_graph_ctx.sources; p; p = p->next) {
        if (p == s) return true;
    }
    return false;
}

lnd_graph_source *lnd_source_obj_create(lnd_source *source, uint32_t flags, int32_t format) {
    if (!source) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    lnd_graph_source *s = lnd_alloc_zero(sizeof *s);
    if (!s) {
        lnd_source_free(source);
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    s->base.ops = &lnd_graph_source_ops;
    s->source = source;
    s->flags = flags;
    s->format = format;
    lnd_source_obj_link(s);
    return s;
}

lnd_source *lnd_source_stream_wrap(lnd_source *source, uint32_t flags) {
    if (!source || (flags & LND_GRAPH_SOURCE_DIRECT) || !lnd_threads_enabled()) return source;
#if LND_MODULE_STREAM
    lnd_source *stream = lnd_stream_source_create(source, true, lnd_cfg_u32(LND_CFG_GRAPH_BUFFER_FRAMES), lnd_cfg_u32(LND_CFG_GRAPH_BUFFER_COUNT));
    if (!stream) lnd_source_free(source);
    return stream;
#else
    return source;
#endif
}

lnd_graph_source *lnd_graph_source_create_buffer(LND_BUFFER *b) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!b) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_STATE);
    }
    lnd_graph_source *s = lnd_source_obj_create(lnd_buffer_source_create(b), 0, b->format);
    lnd_context_unlock();
    return s;
}

lnd_graph_source *lnd_graph_source_create_proc(const LND_SOURCE_PROCS *procs, void *user, int32_t format, uint32_t channels, uint32_t sample_rate_hz,
                                           uint32_t flags) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if ((flags & ~(uint32_t)(LND_GRAPH_SOURCE_DIRECT | LND_SOURCE_LIVE)) || !procs || !procs->read || !lnd_format_valid(format) || channels < 1 || channels > LND_MAX_CHANNELS || sample_rate_hz < 1)
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_STATE);
    }
    uint32_t chunk = lnd_cfg_u32(LND_CFG_GRAPH_BUFFER_FRAMES);
    uint32_t block = lnd_cfg_u32(LND_CFG_GRAPH_MIX_BLOCK_FRAMES);
    LND_SOURCE_PROCS borrowed = *procs;
    borrowed.close = nullptr;
    lnd_source *source = lnd_proc_source_create(&borrowed, user, format, channels, sample_rate_hz, LND_MAX(chunk, block));
    lnd_source *callback_source = source;
    if (source) source->live = (flags & LND_SOURCE_LIVE) != 0;
    source = lnd_source_stream_wrap(source, flags);
    lnd_graph_source *s = lnd_source_obj_create(source, flags, format);
    if (s) lnd_proc_source_set_close(callback_source, procs->close);
    lnd_context_unlock();
    return s;
}

int32_t lnd_graph_source_free(lnd_graph_source *s) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!s) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_context_gc();
    int32_t r = LND_OK;
    if (!lnd_source_obj_valid(s) || s->view)
        r = LND_ERR_INVALID_ARG;
    else if (lnd_sound_busy(s->node))
        r = LND_ERR_BUSY;
    else
        lnd_source_obj_destroy(s);
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t lnd_graph_source_get_format(const lnd_graph_source *s) {
    if (!s) return LND_FORMAT_NONE;
    return s->format;
}

const LND_CODEC *lnd_graph_source_get_codec(const lnd_graph_source *s) { return s ? s->codec : nullptr; }

uint32_t lnd_graph_source_get_sample_rate_hz(const lnd_graph_source *s) {
    if (!s) return 0;
    return s->view ? s->node->sample_rate_hz : s->source->sample_rate_hz;
}

uint32_t lnd_graph_source_get_channels(const lnd_graph_source *s) {
    if (!s) return 0;
    return s->view ? s->node->channels : s->source->channels;
}

uint64_t lnd_graph_source_get_length_frames(const lnd_graph_source *s) {
    if (!s || s->view) return 0;
    return lnd_source_length(s->source);
}

uint64_t lnd_graph_source_get_position_frames(const lnd_graph_source *s) {
    if (!s) return 0;
    if (s->view) return s->node->produced;
    return s->node ? lnd_source_node_pos(s->node) : s->source->pos;
}

static bool lnd_source_obj_offline(lnd_graph_source *s) {
    if (!s->node) return true;
    for (int i = 0; i < 400 && lnd_load(&s->node->active); i++) {
        if (lnd_source_node_state(s->node) != LND_SOUND_STOPPED) return false;
        lnd_node_drain(s->node, false);
        lnd_sleep_ms(1);
    }
    if (lnd_load(&s->node->active)) return false;
    lnd_node_drain(s->node, true);
    lnd_source_sync(s->source);
    return true;
}

int32_t lnd_graph_source_seek_frames(lnd_graph_source *s, uint64_t frame) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!s) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_context_gc();
    int32_t r;
    if (!lnd_source_obj_valid(s)) {
        r = LND_ERR_INVALID_ARG;
    } else if (s->view || !lnd_source_can_seek(s->source)) {
        r = LND_ERR_UNSUPPORTED;
    } else if (s->node) {
        lnd_spinlock_lock(&s->node->lock);
        r = lnd_node_post(s->node, LND_OP_SEEK, frame, 0, 0.0f);
        if (r == LND_OK) {
            lnd_source_node_set_pos(s->node, frame);
            if (!lnd_load(&s->node->active)) {
                lnd_node_process(s->node, true);
                lnd_source_sync(s->source);
                lnd_source_node_set_pos(s->node, s->source->pos);
            }
        }
        lnd_spinlock_unlock(&s->node->lock);
    } else {
        r = lnd_source_seek(s->source, frame);
        if (r == LND_OK) lnd_source_sync(s->source);
    }
    lnd_context_unlock();
    return lnd_error(r);
}

int64_t lnd_graph_source_read_pcm(lnd_graph_source *s, const LND_PCM *pcm, size_t offset, size_t frames) {
    if (!s || !lnd_pcm_writable(pcm, offset, frames) || frames > (size_t)INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_context_gc();
    int64_t got;
    if (!lnd_source_obj_valid(s) || pcm->channels != lnd_graph_source_get_channels(s)) {
        got = lnd_error(LND_ERR_INVALID_ARG);
    } else if (s->view && s->node->native) {
        got = (int64_t)lnd_node_read_pcm(s->node, pcm, offset, frames);
    } else if (s->view) {
        float data[256];
        LND_PCM scratch = {.data = data, .frames = 256 / pcm->channels, .channels = pcm->channels, .format = LND_FORMAT_F32};
        size_t done = 0;
        got = 0;
        while (done < frames) {
            size_t n = LND_MIN(frames - done, scratch.frames);
            uint64_t count = lnd_node_read(s->node, data, LND_FORMAT_F32, n);
            int32_t result = LND_PcmConvert(pcm, offset + done, &scratch, 0, (size_t)count);
            if (result != LND_OK) {
                got = result;
                break;
            }
            done += (size_t)count;
            got = (int64_t)done;
            if (count < n) break;
        }
    } else if (!lnd_source_obj_offline(s)) {
        got = lnd_error(LND_ERR_STATE);
    } else {
        if (s->node) lnd_spinlock_lock(&s->node->lock);
        got = lnd_source_read_pcm(s->source, pcm, offset, frames, true);
        if (s->node) {
            lnd_source_node_set_pos(s->node, s->source->pos);
            lnd_spinlock_unlock(&s->node->lock);
        }
    }
    if (!got && frames && s->view) {
        int32_t status = lnd_node_status(s->node, nullptr);
        if (status < 0) got = lnd_error(status);
    }
    lnd_context_unlock();
    return got;
}

int64_t lnd_graph_source_read(lnd_graph_source *s, void *dst, int32_t format, uint64_t frames) {
    if (!s || frames > SIZE_MAX || frames > INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = lnd_graph_source_get_channels(s), .format = format};
    int64_t got = lnd_graph_source_read_pcm(s, &pcm, 0, (size_t)frames);
    return got;
}

lnd_node *lnd_source_obj_node(lnd_graph_source *s) {
    if (s->view) return s->node;
    if (!s->node) s->node = lnd_node_create_source(s->source, s);
    return s->node;
}

LND_NODE *lnd_graph_source_ensure_node(lnd_graph_source *s) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!s) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    lnd_node *n = lnd_source_obj_valid(s) ? lnd_source_obj_node(s) : nullptr;
    lnd_context_unlock();
    return n ? n : lnd_error_null(LND_ERR_OUT_OF_MEMORY);
}
