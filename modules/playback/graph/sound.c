#include "lindar_graph.h"
#include "lindar_pcm_float.h"
#include "context.h"
#include "sound.h"
#include "src/native.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/error.h"
#if LND_MODULE_DEVICES
#include "io/devices/engine.h"
#include "io/devices/context.h"
#endif
#include "pcm/audio/channels.h"
#include "pcm/audio/convert.h"
#include "src/format.h"
#include "src/pcm.h"
#include "render.h"

#include <math.h>
#include <string.h>

static int32_t lnd_graph_source_status(const LND_SOURCE *source) {
    const lnd_graph_source *s = (const lnd_graph_source *)source;
    return s->view ? LND_NodeGetStatus(s->node) : lnd_source_status(s->source);
}

static int32_t lnd_graph_source_end(LND_SOURCE *source) {
    lnd_graph_source *s = (lnd_graph_source *)source;
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = s->view ? LND_ERR_UNSUPPORTED : lnd_source_end(s->source);
    lnd_context_unlock();
    return lnd_error(result);
}

static void lnd_sound_teardown(lnd_graph_sound *s) {
    lnd_source_free(s->resampler);
    s->resampler = nullptr;
    lnd_free_aligned(s->scratch);
    s->scratch = nullptr;
    if (!s->custom_matrix) {
        lnd_free(s->matrix);
        s->matrix = nullptr;
    }
    s->conversion_ready = false;
    s->ahead = false;
}

bool lnd_sound_busy(const lnd_node *node) { return node && node->sound && node->sound->references; }

void lnd_sound_free(lnd_graph_sound *s) {
    if (!s) return;
    if (s->consumer) s->node->extra_consumers--;
    lnd_sound_teardown(s);
    lnd_free(s->matrix);
    lnd_free(s);
}

static bool lnd_sound_flags_valid(uint32_t flags) {
    return (flags & ~(uint32_t)LND_SOUND_RESAMPLE_MASK) == 0 && (flags & LND_SOUND_RESAMPLE_MASK) <= LND_SOUND_RESAMPLE_SINC32;
}

static lnd_graph_sound *lnd_node_sound(lnd_node *n, uint32_t sample_rate_hz, uint32_t channels, const float *matrix, uint32_t flags, bool configure,
                                       int32_t *err) {
    uint32_t sc = n->channels;
    uint32_t dc = channels ? channels : sc;
    size_t bytes = (size_t)sc * dc * sizeof(float);
    lnd_graph_sound *s = n->sound;
    if (s && lnd_sound_rate(s) == (sample_rate_hz ? sample_rate_hz : n->sample_rate_hz) && lnd_sound_channels(s) == dc && s->flags == flags) {
        if (!matrix && !s->custom_matrix) return s;
        if (matrix && s->custom_matrix && s->matrix_channels == sc && memcmp(s->matrix, matrix, bytes) == 0) return s;
    }
    if (s && s->references) {
        *err = LND_ERR_BUSY;
        return nullptr;
    }
    if (s && !configure) {
        *err = LND_ERR_STATE;
        return nullptr;
    }
    float *copy = nullptr;
    if (matrix) {
        copy = lnd_alloc(bytes);
        if (!copy) {
            *err = LND_ERR_OUT_OF_MEMORY;
            return nullptr;
        }
        memcpy(copy, matrix, bytes);
    }
    if (!s) {
        s = lnd_alloc_zero(sizeof *s);
        if (!s) {
            lnd_free(copy);
            *err = LND_ERR_OUT_OF_MEMORY;
            return nullptr;
        }
        s->base.ops = &lnd_graph_sound_ops;
        s->node = n;
        s->gain = n->gain_param;
        LND_SOUND *native = lnd_node_pcm_sound(n);
        if (native) s->loop = lnd_native_sound_get_loop((lnd_native_sound *)native);
        n->sound = s;
    } else {
        lnd_sound_teardown(s);
        lnd_free(s->matrix);
    }
    s->matrix = copy;
    s->custom_matrix = copy != nullptr;
    s->matrix_channels = sc;
    s->sample_rate_hz = sample_rate_hz;
    s->channels = channels;
    s->flags = flags;
    s->reset_pending = true;
    return s;
}

lnd_graph_sound *lnd_graph_source_ensure_sound(lnd_graph_source *s, const LND_SOUND_CONFIG *config) {
    const LND_SOUND_CONFIG c = config ? *config : (LND_SOUND_CONFIG){0};
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!s || c.channels > LND_MAX_CHANNELS || !lnd_sound_flags_valid(c.flags)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    int32_t err = LND_ERR_INVALID_ARG;
    lnd_node *n = lnd_source_obj_valid(s) ? lnd_source_obj_node(s) : nullptr;
    if (n) err = LND_ERR_OUT_OF_MEMORY;
    lnd_graph_sound *snd = n && !config && n->sound ? n->sound
                           : n                      ? lnd_node_sound(n, c.sample_rate_hz, c.channels, c.channel_matrix, c.flags, false, &err)
                                                    : nullptr;
    lnd_context_unlock();
    return snd ? snd : lnd_error_null(err);
}

lnd_graph_source *lnd_graph_node_ensure_source(LND_NODE *n) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!n) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    lnd_graph_source *s = nullptr;
    if (lnd_context_has_node(n)) {
        if (n->type == LND_NODE_SOURCE) {
            s = lnd_source_node_owner(n);
        } else if (n->view) {
            s = n->view;
        } else {
            s = lnd_alloc_zero(sizeof *s);
            if (s) {
                s->base.ops = &lnd_graph_source_ops;
                s->node = n;
                s->view = true;
                s->format = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT);
                if (lnd_node_ensure_ring(n) != LND_OK) {
                    lnd_free(s);
                    s = nullptr;
                }
                n->view = s;
            }
        }
    }
    lnd_context_unlock();
    return s ? s : lnd_error_null(LND_ERR_OUT_OF_MEMORY);
}

lnd_graph_sound *lnd_graph_node_ensure_sound(LND_NODE *n, const LND_SOUND_CONFIG *config) {
    const LND_SOUND_CONFIG c = config ? *config : (LND_SOUND_CONFIG){0};
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!n || c.channels > LND_MAX_CHANNELS || !lnd_sound_flags_valid(c.flags)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    int32_t err = LND_ERR_INVALID_ARG;
    lnd_graph_sound *snd = nullptr;
    if (lnd_context_has_node(n)) {
        err = LND_ERR_OUT_OF_MEMORY;
        snd = !config && n->sound ? n->sound : lnd_node_sound(n, c.sample_rate_hz, c.channels, c.channel_matrix, c.flags, false, &err);
    }
    lnd_context_unlock();
    return snd ? snd : lnd_error_null(err);
}

static uint64_t lnd_sound_tap_read(lnd_source *src, float *dst, uint64_t frames) {
    lnd_graph_sound *s = ((lnd_sound_tap *)src)->sound;
    lnd_node *n = s->node;
    uint64_t got;
    if (s->transport)
        got = lnd_node_pull(n, &s->cursor, dst, (uint32_t)frames);
    else if (n->type == LND_NODE_SOURCE)
        got = lnd_source_node_read(n, dst, frames, s->loop);
    else if (n->type == LND_NODE_PCM_INPUT)
        got = lnd_pcm_input_read(n, dst, frames);
    else
        got = lnd_node_read(n, dst, LND_FORMAT_F32, frames);
    lnd_store_relaxed(&src->pos, lnd_load_relaxed(&src->pos) + got);
    LND_SOUND *native = lnd_node_pcm_sound(n);
    int32_t status = native ? LND_SourceGetStatus(LND_SoundGetSource(native)) : lnd_node_status(n, nullptr);
    lnd_store(&src->status, status);
    return got;
}

static int32_t lnd_sound_tap_seek(lnd_source *src, uint64_t frame) {
    (void)src;
    (void)frame;
    return LND_OK;
}

static void lnd_sound_tap_free(lnd_source *src) { (void)src; }

static const lnd_source_vt lnd_sound_tap_vt = {
    .read = lnd_sound_tap_read,
    .seek = lnd_sound_tap_seek,
    .free = lnd_sound_tap_free,
};

static int32_t lnd_sound_prepare(lnd_graph_sound *s) {
    lnd_node *n = s->node;
    uint32_t sc = n->channels, sr = n->sample_rate_hz;
    uint32_t dc = lnd_sound_channels(s), dr = lnd_sound_rate(s);
    if (s->conversion_ready && s->prepared_source_rate_hz == sr && s->prepared_source_channels == sc) return LND_OK;
    lnd_sound_teardown(s);
    if (s->custom_matrix && s->matrix_channels != sc) {
        lnd_free(s->matrix);
        s->matrix = nullptr;
        s->custom_matrix = false;
    }
    s->tap.base.vt = &lnd_sound_tap_vt;
    s->tap.base.channels = sc;
    s->tap.base.live = true;
    s->tap.base.sample_rate_hz = sr;
    s->tap.base.pos = 0;
    lnd_store(&s->tap.base.status, LND_SOURCE_READY);
    s->tap.sound = s;
    s->scratch_frames = LND_MAX(n->block, 1024u);
    bool matrix = s->matrix || sc != dc;
    size_t samples = matrix ? (size_t)s->scratch_frames * sc : 0;
    if (!n->native || matrix || sr != dr) samples += (size_t)n->block * dc;
    s->scratch = samples ? lnd_alloc_aligned(samples * sizeof(float), LND_CACHE_LINE) : nullptr;
    if (samples && !s->scratch) return LND_ERR_OUT_OF_MEMORY;
    if (sr != dr) {
        uint32_t quality = s->flags & LND_SOUND_RESAMPLE_MASK;
        quality = quality ? quality - 1 : lnd_cfg_u32(LND_CFG_AUDIO_RESAMPLE_QUALITY);
        s->resampler = lnd_resample_source_create(&s->tap.base, false, dr, s->scratch_frames, quality);
        if (!s->resampler) return LND_ERR_OUT_OF_MEMORY;
    }
    if (!s->matrix && sc != dc) {
        s->matrix = lnd_alloc((size_t)sc * dc * sizeof(float));
        if (!s->matrix) return LND_ERR_OUT_OF_MEMORY;
        lnd_channels_matrix(sc, dc, n->channel_mix, s->matrix);
    }
    s->conversion_ready = true;
    s->prepared_source_rate_hz = sr;
    s->prepared_source_channels = sc;
    s->reset_pending = true;
    return LND_OK;
}

int64_t lnd_graph_sound_read_f32(lnd_graph_sound *s, float *dst, uint64_t frames) {
    if (lnd_callback_active()) {
        return lnd_error(LND_ERR_BUSY);
    }

    if (!s || (!dst && frames) || frames > INT64_MAX || frames > SIZE_MAX / sizeof(float) / lnd_sound_channels(s)) {
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    if (!lnd_context_enter()) {
        return lnd_error(LND_ERR_BUSY);
    }
    lnd_context_gc();
    if (!lnd_sound_valid(s)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    lnd_spinlock_lock(&s->lock);
    int32_t r = lnd_sound_sync_read(s);
    if (r == LND_OK) r = lnd_sound_prepare(s);
    if (r != LND_OK) {
        lnd_spinlock_unlock(&s->lock);
        lnd_context_unlock();
        return lnd_error(r);
    }
    lnd_node *n = s->node;
    uint32_t sc = n->channels, dc = lnd_sound_channels(s);
    lnd_source *origin = lnd_source_node_origin(n);
    if (origin) {
        lnd_spinlock_lock(&n->lock);
        lnd_node_process(n, !lnd_load_relaxed(&n->active));
        lnd_source_sync(origin);
        uint64_t length = lnd_source_limit(origin);
        uint64_t expected = s->source_base_frames + s->tap.base.pos;
        if (s->loop && length && expected >= length) expected %= length;
        if (s->reset_pending || expected != origin->pos) {
            if (s->resampler) lnd_source_seek(s->resampler, 0);
            s->tap.base.pos = 0;
            lnd_store(&s->tap.base.status, LND_SOURCE_READY);
            s->source_base_frames = origin->pos;
            s->delivered_frames = 0;
        }
    } else if (lnd_node_pcm_sound(n)) {
        lnd_spinlock_lock(&n->lock);
        LND_SOUND *native = lnd_node_pcm_sound(n);
        uint64_t length = lnd_native_source_limit(((lnd_native_sound *)native)->source), pos = lnd_native_sound_get_position_frames((lnd_native_sound *)native);
        uint64_t expected = s->source_base_frames + s->tap.base.pos;
        if (s->loop && length && expected >= length) expected %= length;
        if (s->reset_pending || expected != pos) {
            if (s->resampler) lnd_source_seek(s->resampler, 0);
            s->tap.base.pos = 0;
            lnd_store(&s->tap.base.status, LND_SOURCE_READY);
            s->source_base_frames = pos;
            s->delivered_frames = 0;
        }
    } else if (s->reset_pending && s->resampler) {
        lnd_source_seek(s->resampler, 0);
    }
    s->reset_pending = false;
    lnd_source *stage = s->resampler ? s->resampler : &s->tap.base;
    uint64_t total = 0;
    while (total < frames) {
        uint32_t nb = (uint32_t)LND_MIN(frames - total, (uint64_t)s->scratch_frames);
        float *out = dst + total * dc;
        uint64_t got;
        if (s->matrix) {
            got = lnd_source_read(stage, s->scratch, nb);
            lnd_channels_apply(s->matrix, s->scratch, sc, out, dc, (size_t)got);
        } else {
            got = lnd_source_read(stage, out, nb);
        }
        total += got;
        if (got < nb) break;
    }
    if (origin) {
        if (s->resampler) {
            s->delivered_frames += total;
            uint64_t logical = s->source_base_frames + lnd_scale_frames(s->delivered_frames, lnd_sound_rate(s), n->sample_rate_hz);
            uint64_t length = lnd_source_limit(origin);
            if (length && logical > length) logical = s->loop ? logical % length : length;
            lnd_source_node_set_pos(n, logical);
            s->ahead = true;
        }
        lnd_spinlock_unlock(&n->lock);
    } else if (lnd_node_pcm_sound(n)) {
        s->delivered_frames += total;
        s->ahead = s->resampler != nullptr;
        lnd_spinlock_unlock(&n->lock);
    }
    int32_t status = lnd_source_status(stage);
    lnd_spinlock_unlock(&s->lock);
    lnd_context_unlock();
    return total || status >= 0 ? (int64_t)total : lnd_error(status);
}

LND_SOURCE *LND_SourceCreateBuffer(LND_BUFFER *b) { return (LND_SOURCE *)lnd_graph_source_create_buffer(b); }
LND_SOURCE *LND_SourceCreateProc(const LND_SOURCE_PROCS *procs, void *user, int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint32_t flags) {
    return (LND_SOURCE *)lnd_graph_source_create_proc(procs, user, format, channels, sample_rate_hz, flags);
}
static int64_t lnd_dispatch_source_read_pcm(LND_SOURCE *s, const LND_PCM *pcm, size_t offset, size_t frames) {
    return lnd_graph_source_read_pcm((lnd_graph_source *)s, pcm, offset, frames);
}
static int32_t lnd_dispatch_source_free(LND_SOURCE *s) { return lnd_graph_source_free((lnd_graph_source *)s); }
static int32_t lnd_dispatch_source_get_format(const LND_SOURCE *s) { return lnd_graph_source_get_format((const lnd_graph_source *)s); }
static const LND_CODEC *lnd_dispatch_source_get_codec(const LND_SOURCE *s) {
    return (const LND_CODEC *)lnd_graph_source_get_codec((const lnd_graph_source *)s);
}
static uint32_t lnd_dispatch_source_get_sample_rate_hz(const LND_SOURCE *s) { return lnd_graph_source_get_sample_rate_hz((const lnd_graph_source *)s); }
static uint32_t lnd_dispatch_source_get_channels(const LND_SOURCE *s) { return lnd_graph_source_get_channels((const lnd_graph_source *)s); }
static uint64_t lnd_dispatch_source_get_length_frames(const LND_SOURCE *s) { return lnd_graph_source_get_length_frames((const lnd_graph_source *)s); }
static uint64_t lnd_dispatch_source_get_position_frames(const LND_SOURCE *s) { return lnd_graph_source_get_position_frames((const lnd_graph_source *)s); }
static int32_t lnd_dispatch_source_seek_frames(LND_SOURCE *s, uint64_t frame) { return lnd_graph_source_seek_frames((lnd_graph_source *)s, frame); }
static int64_t lnd_dispatch_source_read(LND_SOURCE *s, void *dst, int32_t format, uint64_t frames) {
    return lnd_graph_source_read((lnd_graph_source *)s, dst, format, frames);
}
static int32_t lnd_dispatch_source_get_info(const LND_SOURCE *source, LND_SOURCE_INFO *info) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    const lnd_graph_source *s = (const lnd_graph_source *)source;
    if (!lnd_source_obj_valid(s)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    if (s->node) lnd_spinlock_lock(&s->node->lock);
    *info = (LND_SOURCE_INFO){.length_frames = lnd_graph_source_get_length_frames(s),
                              .position_frames = lnd_graph_source_get_position_frames(s),
                              .sample_rate_hz = lnd_graph_source_get_sample_rate_hz(s),
                              .channels = lnd_graph_source_get_channels(s),
                              .format = lnd_graph_source_get_format(s),
                              .status = s->view ? lnd_node_status_locked(s->node) : lnd_source_status(s->source),
                              .length_kind = LND_LENGTH_UNKNOWN};
    if (!s->view && (s->source->length_known || s->source->length_estimated || info->length_frames))
        info->length_kind = s->source->length_estimated ? LND_LENGTH_ESTIMATED : LND_LENGTH_EXACT;
    if (s->node) lnd_spinlock_unlock(&s->node->lock);
    lnd_context_unlock();
    return LND_OK;
}

static LND_NODE *lnd_dispatch_source_get_node(const LND_SOURCE *source) { return ((const lnd_graph_source *)source)->node; }
static LND_SOUND *lnd_dispatch_source_get_sound(const LND_SOURCE *source) {
    const lnd_node *node = ((const lnd_graph_source *)source)->node;
    return node ? (LND_SOUND *)node->sound : nullptr;
}
static int32_t lnd_graph_sound_configure(LND_SOUND *sound, const LND_SOUND_CONFIG *config) {
    const LND_SOUND_CONFIG c = config ? *config : (LND_SOUND_CONFIG){0};
    if (c.channels > LND_MAX_CHANNELS || !lnd_sound_flags_valid(c.flags)) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_graph_sound *s = (lnd_graph_sound *)sound;
    int32_t result = LND_ERR_INVALID_ARG;
    if (lnd_sound_valid(s)) {
        result = LND_OK;
        lnd_spinlock_lock(&s->lock);
        lnd_node_sound(s->node, c.sample_rate_hz, c.channels, c.channel_matrix, c.flags, true, &result);
        lnd_spinlock_unlock(&s->lock);
    }
    lnd_context_unlock();
    return lnd_error(result);
}

LND_SOURCE *LND_NodeGetSource(const LND_NODE *node) {
    if (!node) return nullptr;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    LND_SOURCE *source = nullptr;
    if (lnd_context_has_node(node)) {
        LND_SOUND *native = lnd_node_pcm_sound(node);
        if (native)
            source = LND_SoundGetSource(native);
        else if (node->type == LND_NODE_SOURCE)
            source = (LND_SOURCE *)lnd_source_node_owner(node);
        else
            source = (LND_SOURCE *)node->view;
    }
    lnd_context_unlock();
    return source;
}

LND_SOUND *LND_NodeGetSound(const LND_NODE *node) {
    if (!node) return nullptr;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    LND_SOUND *sound = lnd_context_has_node(node) ? (lnd_node_pcm_sound(node) ? lnd_node_pcm_sound(node) : (LND_SOUND *)node->sound) : nullptr;
    lnd_context_unlock();
    return sound;
}

static LND_NODE *lnd_dispatch_source_ensure_node(LND_SOURCE *s) { return (LND_NODE *)lnd_graph_source_ensure_node((lnd_graph_source *)s); }
static LND_SOUND *lnd_dispatch_source_ensure_sound(LND_SOURCE *s, const LND_SOUND_CONFIG *config) {
    return (LND_SOUND *)lnd_graph_source_ensure_sound((lnd_graph_source *)s, config);
}
LND_SOURCE *LND_NodeEnsureSource(LND_NODE *n) {
    LND_SOUND *sound = lnd_node_pcm_sound(n);
    return sound ? LND_SoundGetSource(sound) : (LND_SOURCE *)lnd_graph_node_ensure_source(n);
}
LND_SOUND *LND_NodeEnsureSound(LND_NODE *n, const LND_SOUND_CONFIG *config) {
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    if (!n || !lnd_context_has_node(n)) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
    LND_SOUND *native = lnd_node_pcm_sound(n);
    LND_SOUND *sound = native ? LND_SourceEnsureSound(LND_SoundGetSource(native), config) : (LND_SOUND *)lnd_graph_node_ensure_sound(n, config);
    lnd_context_unlock();
    return sound;
}
static LND_NODE *lnd_dispatch_sound_get_node(const LND_SOUND *s) { return (LND_NODE *)lnd_graph_sound_get_node((const lnd_graph_sound *)s); }
static LND_SOURCE *lnd_dispatch_sound_get_source(const LND_SOUND *s) { return s ? LND_NodeGetSource(((const lnd_graph_sound *)s)->node) : nullptr; }
static int32_t lnd_dispatch_sound_play(LND_SOUND *s) { return lnd_graph_sound_play((lnd_graph_sound *)s); }
static int32_t lnd_dispatch_sound_set_pause(LND_SOUND *s, bool pause) { return lnd_graph_sound_set_pause((lnd_graph_sound *)s, pause); }
static int32_t lnd_dispatch_sound_stop(LND_SOUND *s) { return lnd_graph_sound_stop((lnd_graph_sound *)s); }
static int32_t lnd_dispatch_sound_get_state(const LND_SOUND *s) { return lnd_graph_sound_get_state((const lnd_graph_sound *)s); }
static uint64_t lnd_dispatch_sound_get_position_frames(const LND_SOUND *s) { return lnd_graph_sound_get_position_frames((const lnd_graph_sound *)s); }
static uint32_t lnd_dispatch_sound_get_sample_rate_hz(const LND_SOUND *s) { return lnd_graph_sound_get_sample_rate_hz((const lnd_graph_sound *)s); }
static uint32_t lnd_dispatch_sound_get_channels(const LND_SOUND *s) { return lnd_graph_sound_get_channels((const lnd_graph_sound *)s); }
static double lnd_dispatch_sound_get_position_seconds(const LND_SOUND *s) { return lnd_graph_sound_get_position_seconds((const lnd_graph_sound *)s); }
static int32_t lnd_dispatch_sound_seek_frames(LND_SOUND *s, uint64_t frame) { return lnd_graph_sound_seek_frames((lnd_graph_sound *)s, frame); }
static int32_t lnd_dispatch_sound_seek_seconds(LND_SOUND *s, double sec) { return lnd_graph_sound_seek_seconds((lnd_graph_sound *)s, sec); }
static uint64_t lnd_dispatch_sound_get_length_frames(const LND_SOUND *s) { return lnd_graph_sound_get_length_frames((const lnd_graph_sound *)s); }
static double lnd_dispatch_sound_get_length_seconds(const LND_SOUND *s) { return lnd_graph_sound_get_length_seconds((const lnd_graph_sound *)s); }
static int32_t lnd_dispatch_sound_set_gain(LND_SOUND *s, float gain) { return lnd_graph_sound_set_gain((lnd_graph_sound *)s, gain); }
static float lnd_dispatch_sound_get_gain(const LND_SOUND *s) { return lnd_graph_sound_get_gain((const lnd_graph_sound *)s); }
static int32_t lnd_dispatch_sound_set_loop(LND_SOUND *s, bool loop) { return lnd_graph_sound_set_loop((lnd_graph_sound *)s, loop); }
static bool lnd_dispatch_sound_get_loop(const LND_SOUND *s) { return lnd_graph_sound_get_loop((const lnd_graph_sound *)s); }
static int32_t lnd_dispatch_sound_set_output(LND_SOUND *s, LND_NODE *dst) { return lnd_graph_sound_set_output((lnd_graph_sound *)s, dst); }
static LND_NODE *lnd_dispatch_sound_get_output(const LND_SOUND *s) { return (LND_NODE *)lnd_graph_sound_get_output((const lnd_graph_sound *)s); }
static int64_t lnd_dispatch_sound_read(LND_SOUND *s, float *dst, uint64_t frames) { return lnd_graph_sound_read_f32((lnd_graph_sound *)s, dst, frames); }

const lnd_source_ops lnd_graph_source_ops = {
    .SourceGetInfo = lnd_dispatch_source_get_info,
    .SourceGetStatus = lnd_graph_source_status,
    .SourceEnd = lnd_graph_source_end,
    .SourceFree = lnd_dispatch_source_free,
    .SourceReadPcm = lnd_dispatch_source_read_pcm,
    .SourceGetFormat = lnd_dispatch_source_get_format,
    .SourceGetCodec = lnd_dispatch_source_get_codec,
    .SourceGetSampleRateHz = lnd_dispatch_source_get_sample_rate_hz,
    .SourceGetChannels = lnd_dispatch_source_get_channels,
    .SourceGetLengthFrames = lnd_dispatch_source_get_length_frames,
    .SourceGetPositionFrames = lnd_dispatch_source_get_position_frames,
    .SourceSeekFrames = lnd_dispatch_source_seek_frames,
    .SourceRead = lnd_dispatch_source_read,
    .SourceGetNode = lnd_dispatch_source_get_node,
    .SourceGetSound = lnd_dispatch_source_get_sound,
    .SourceEnsureNode = lnd_dispatch_source_ensure_node,
    .SourceEnsureSound = lnd_dispatch_source_ensure_sound,
};

static int32_t lnd_graph_sound_set_gain_q16(LND_SOUND *s, uint32_t gain) { return LND_SoundSetGain(s, (float)((double)gain / 65536.0)); }
static uint32_t lnd_graph_sound_get_gain_q16(const LND_SOUND *s) {
    double gain = (double)LND_SoundGetGain(s) * 65536.0;
    return !(gain > 0) ? 0 : gain >= UINT32_MAX ? UINT32_MAX : (uint32_t)(gain + 0.5);
}
static int32_t lnd_sound_prepare_render(lnd_graph_sound *s) {
    if (!lnd_sound_valid(s)) return LND_ERR_INVALID_ARG;
    int32_t result = lnd_sound_prepare(s);
    if (result != LND_OK) return result;
    lnd_node *node = s->node;
    if (!s->consumer) {
        if (node->outputs_count + node->extra_consumers + (node->instance != nullptr)) {
            result = lnd_node_ensure_ring(node);
            if (result != LND_OK) return result;
        }
        lnd_spinlock_lock(&node->lock);
        node->extra_consumers++;
        if (!s->transport) s->cursor = node->produced;
        s->consumer = true;
        lnd_spinlock_unlock(&node->lock);
    }
    return LND_OK;
}

static void lnd_sound_release_render(lnd_graph_sound *s) {
    if (!s->consumer) return;
    lnd_spinlock_lock(&s->node->lock);
    s->node->extra_consumers--;
    s->consumer = false;
    lnd_spinlock_unlock(&s->node->lock);
}

static int32_t lnd_graph_sound_ref(LND_SOUND *sound) {
    lnd_graph_sound *s = (lnd_graph_sound *)sound;
    if (s->references == UINT32_MAX) return LND_ERR_BUSY;
    lnd_spinlock_lock(&s->lock);
    int32_t result = lnd_sound_prepare_render(s);
    if (result == LND_OK) s->references++;
    lnd_spinlock_unlock(&s->lock);
    return result;
}

static void lnd_graph_sound_unref(LND_SOUND *sound) {
    lnd_graph_sound *s = (lnd_graph_sound *)sound;
    lnd_spinlock_lock(&s->lock);
    if (!--s->references) lnd_sound_release_render(s);
    lnd_spinlock_unlock(&s->lock);
}

static int64_t lnd_graph_sound_render(LND_SOUND *sound, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_graph_sound *s = (lnd_graph_sound *)sound;
    if (!lnd_pcm_writable(pcm, offset, frames) || frames > (size_t)INT64_MAX || pcm->channels != lnd_sound_channels(s)) return lnd_error(LND_ERR_INVALID_ARG);
    bool control = !lnd_callback_active();
    if (control && !lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_spinlock_try(&s->lock)) {
        if (control) lnd_context_unlock();
        return lnd_error(LND_ERR_BUSY);
    }
    int32_t result;
    if (control)
        result = lnd_sound_prepare_render(s);
    else
        result = s->conversion_ready && s->consumer ? LND_OK : LND_ERR_STATE;
    lnd_node *n = s->node;
    size_t done = 0;
    LND_SOUND *native = lnd_node_pcm_sound(n);
    int32_t state;
    if (native)
        state = lnd_native_sound_get_state((lnd_native_sound *)native);
    else if (n->type == LND_NODE_SOURCE)
        state = lnd_source_node_state(n);
    else
        state = LND_SOUND_PLAYING;
    bool draining = lnd_sound_draining(s);
    if (result != LND_OK || s->paused || state == LND_SOUND_PAUSED || (state == LND_SOUND_STOPPED && !draining)) goto end;
    if (!s->transport || s->reset_pending) {
        if (s->resampler) lnd_resample_source_reset(s->resampler);
        s->tap.base.pos = 0;
        lnd_store(&s->tap.base.status, LND_SOURCE_READY);
        s->cursor = n->produced;
        s->source_base_frames = native ? lnd_native_sound_get_position_frames((lnd_native_sound *)native) : lnd_source_node_pos(n);
        s->delivered_frames = 0;
        s->transport = true;
        s->reset_pending = false;
    }
    lnd_source *stage = s->resampler ? s->resampler : &s->tap.base;
    while (done < frames) {
        uint32_t want = (uint32_t)LND_MIN(frames - done, n->block);
        uint64_t got;
        if (n->native && !s->resampler && !s->matrix) {
            got = lnd_node_pull_pcm_valid(n, &s->cursor, pcm, offset + done, want);
            result = lnd_node_status(n, &s->cursor);
        } else {
            float *out = s->scratch + (s->matrix ? (size_t)s->scratch_frames * n->channels : 0);
            got = lnd_source_read(stage, s->matrix ? s->scratch : out, want);
            if (s->matrix) lnd_channels_apply(s->matrix, s->scratch, n->channels, out, pcm->channels, (size_t)got);
            LND_PCM data = {.data = out, .frames = want, .channels = pcm->channels, .format = LND_FORMAT_F32};
            result = lnd_pcm_convert(pcm, offset + done, &data, 0, (size_t)got);
            if (result != LND_OK) break;
            result = lnd_source_status(stage);
        }
        done += (size_t)got;
        if (got < want || result < 0) break;
    }
    s->delivered_frames += done;
    s->ahead = s->resampler != nullptr;
end:
    if (control && !s->references) lnd_sound_release_render(s);
    lnd_spinlock_unlock(&s->lock);
    if (control) lnd_context_unlock();
    return done || result >= 0 ? (int64_t)done : lnd_error(result);
}

const lnd_sound_ops lnd_graph_sound_ops = {
    .SoundRef = lnd_graph_sound_ref,
    .SoundUnref = lnd_graph_sound_unref,
    .SoundRenderPcm = lnd_graph_sound_render,
    .SoundSetConfig = lnd_graph_sound_configure,
    .SoundSetGainQ16 = lnd_graph_sound_set_gain_q16,
    .SoundGetGainQ16 = lnd_graph_sound_get_gain_q16,

    .SoundGetNode = lnd_dispatch_sound_get_node,
    .SoundGetSource = lnd_dispatch_sound_get_source,
    .SoundPlay = lnd_dispatch_sound_play,
    .SoundSetPause = lnd_dispatch_sound_set_pause,
    .SoundStop = lnd_dispatch_sound_stop,
    .SoundGetState = lnd_dispatch_sound_get_state,
    .SoundGetPositionFrames = lnd_dispatch_sound_get_position_frames,
    .SoundGetSampleRateHz = lnd_dispatch_sound_get_sample_rate_hz,
    .SoundGetChannels = lnd_dispatch_sound_get_channels,
    .SoundGetPositionSeconds = lnd_dispatch_sound_get_position_seconds,
    .SoundSeekFrames = lnd_dispatch_sound_seek_frames,
    .SoundSeekSeconds = lnd_dispatch_sound_seek_seconds,
    .SoundGetLengthFrames = lnd_dispatch_sound_get_length_frames,
    .SoundGetLengthSeconds = lnd_dispatch_sound_get_length_seconds,
    .SoundSetGain = lnd_dispatch_sound_set_gain,
    .SoundGetGain = lnd_dispatch_sound_get_gain,
    .SoundSetLoop = lnd_dispatch_sound_set_loop,
    .SoundGetLoop = lnd_dispatch_sound_get_loop,
    .SoundSetOutput = lnd_dispatch_sound_set_output,
    .SoundGetOutput = lnd_dispatch_sound_get_output,
    .SoundRead = lnd_dispatch_sound_read,
};
