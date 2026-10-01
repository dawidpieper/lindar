#include "capture.h"
#include "lindar_devices.h"
#include "playback/graph/sound.h"
#include "playback/graph/context.h"
#include "src/error.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/spinlock.h"
#include "engine.h"
#include "pcm/audio/channels.h"
#include "pcm/audio/convert.h"
#include "src/format.h"

#include <string.h>

#define LND_CAPTURE_STALL_MS 2000
#define LND_CAPTURE_WAIT_MS 100

lnd_capture *lnd_capture_new(uint32_t channels, uint32_t sample_rate_hz, uint32_t frames) {
    if (!channels || channels > LND_MAX_CHANNELS || !sample_rate_hz) return nullptr;
    uint32_t capacity = lnd_next_pow2_u32(frames < 1024 ? 1024 : frames);
    if (!capacity || capacity > SIZE_MAX / channels / sizeof(float)) return nullptr;
    lnd_capture *c = lnd_alloc_zero(sizeof *c);
    if (!c) return nullptr;
    c->channels = c->input_channels = channels;
    c->sample_rate_hz = sample_rate_hz;
    c->cap = capacity;
    c->mask = c->cap - 1;
    lnd_store_relaxed(&c->starved, true);
    c->scratch_frames = 4096;
    c->ring = lnd_alloc_aligned((size_t)c->cap * channels * sizeof(float), LND_CACHE_LINE);
    c->scratch = lnd_alloc_aligned((size_t)c->scratch_frames * channels * sizeof(float), LND_CACHE_LINE);
    if (!c->ring || !c->scratch || lnd_event_init(&c->event) != LND_OK) {
        lnd_free_aligned(c->ring);
        lnd_free_aligned(c->scratch);
        lnd_free(c);
        return nullptr;
    }
    memset(c->ring, 0, (size_t)c->cap * channels * sizeof(float));
    return c;
}

void lnd_capture_free(lnd_capture *c) {
    if (!c) return;
    lnd_event_free(&c->event);
    lnd_free(c->input_matrix);
    lnd_free_aligned(c->input_scratch);
    lnd_free_aligned(c->ring);
    lnd_free_aligned(c->scratch);
    lnd_free(c);
}

void lnd_capture_end(lnd_capture *c) {
    lnd_store(&c->ended, 1);
    lnd_event_signal(&c->event);
}

static void lnd_capture_write(lnd_capture *c, const float *src, uint32_t frames) {
    uint32_t ch = c->channels;
    uint64_t head = lnd_load_relaxed(&c->head);
    uint64_t used = head - lnd_load(&c->tail);
    uint32_t space = used < c->cap ? c->cap - (uint32_t)used : 0;
    if (frames > space) {
        lnd_add(&c->overruns, frames - space);
        frames = space;
    }
    if (!frames) return;
    uint32_t idx = (uint32_t)(head & c->mask);
    uint32_t first = LND_MIN(frames, c->cap - idx);
    memcpy(c->ring + (size_t)idx * ch, src, (size_t)first * ch * sizeof(float));
    if (frames > first) memcpy(c->ring, src + (size_t)first * ch, (size_t)(frames - first) * ch * sizeof(float));
    lnd_store(&c->head, head + frames);
}

int32_t lnd_capture_set_input_channels(lnd_capture *c, uint32_t channels) {
    if (!channels || channels > LND_MAX_CHANNELS) return LND_ERR_FORMAT;
    if (channels == c->input_channels) return LND_OK;
    float *matrix = nullptr, *scratch = nullptr;
    if (channels != c->channels) {
        matrix = lnd_alloc((size_t)channels * c->channels * sizeof(float));
        scratch = lnd_alloc_aligned((size_t)channels * c->scratch_frames * sizeof(float), LND_CACHE_LINE);
        if (!matrix || !scratch) {
            lnd_free(matrix);
            lnd_free_aligned(scratch);
            return LND_ERR_OUT_OF_MEMORY;
        }
        lnd_channels_matrix(channels, c->channels, (int32_t)lnd_cfg_u32(LND_CFG_AUDIO_CHANNEL_MIX), matrix);
    }
    lnd_free(c->input_matrix);
    lnd_free_aligned(c->input_scratch);
    c->input_matrix = matrix;
    c->input_scratch = scratch;
    c->input_channels = channels;
    return LND_OK;
}

static void lnd_capture_push_locked(lnd_capture *c, const void *data, int32_t format, uint64_t frames) {
    uint32_t ch = c->channels;
    if (c->input_matrix) {
        size_t frame_bytes = (size_t)c->input_channels * lnd_format_bytes(format);
        for (uint64_t done = 0; done < frames;) {
            uint32_t n = (uint32_t)LND_MIN(frames - done, c->scratch_frames);
            const uint8_t *input = (const uint8_t *)data + done * frame_bytes;
            const float *src = (const float *)input;
            if (format != LND_FORMAT_F32) {
                lnd_pcm_to_f32(format, input, c->input_scratch, (size_t)n * c->input_channels);
                src = c->input_scratch;
            }
            lnd_channels_apply(c->input_matrix, src, c->input_channels, c->scratch, ch, n);
            lnd_capture_write(c, c->scratch, n);
            done += n;
        }
    } else if (format == LND_FORMAT_F32) {
        const float *src = data;
        for (uint64_t done = 0; done < frames;) {
            uint32_t n = (uint32_t)LND_MIN(frames - done, (uint64_t)c->cap / 2);
            lnd_capture_write(c, src + done * ch, n);
            done += n;
        }
    } else {
        const uint8_t *src = data;
        size_t frame_bytes = (size_t)ch * lnd_format_bytes(format);
        for (uint64_t done = 0; done < frames;) {
            uint32_t n = (uint32_t)LND_MIN(frames - done, (uint64_t)LND_MIN(c->scratch_frames, c->cap / 2));
            lnd_pcm_to_f32(format, src + done * frame_bytes, c->scratch, (size_t)n * ch);
            lnd_capture_write(c, c->scratch, n);
            done += n;
        }
    }
    lnd_event_signal(&c->event);
}

void lnd_capture_push(lnd_capture *c, const void *data, int32_t format, uint64_t frames) {
    lnd_spinlock_lock(&c->control);
    if (!lnd_load(&c->paused)) lnd_capture_push_locked(c, data, format, frames);
    lnd_spinlock_unlock(&c->control);
}

uint64_t lnd_capture_available(const lnd_capture *c) {
    uint64_t tail = lnd_load(&c->tail);
    uint64_t avail = lnd_load(&c->head) - tail;
    return avail > c->cap ? c->cap : avail;
}

uint64_t lnd_capture_read(lnd_capture *c, float *dst, uint64_t frames, int32_t mode) {
    uint32_t ch = c->channels;
    uint64_t got = 0;
    uint32_t waited = 0;
    if (mode != LND_CAPTURE_READ_STREAM) lnd_store_relaxed(&c->starved, true);
    if (mode == LND_CAPTURE_READ_STREAM && !lnd_load(&c->ended)) {
        uint64_t head = lnd_load(&c->head);
        uint64_t tail = lnd_load_relaxed(&c->tail);
        uint64_t avail = head - tail;
        if (avail > c->cap) {
            uint64_t keep = c->cap / 2;
            lnd_add(&c->overruns, avail - keep);
            lnd_store(&c->tail, head - keep);
            avail = keep;
        }
        uint64_t twice = frames > c->cap / 4 ? c->cap / 2 : frames * 2;
        uint64_t need = lnd_load_relaxed(&c->starved) ? LND_MIN((uint64_t)c->cap / 2, LND_MAX(twice, lnd_load(&c->prefill))) : frames;
        if (avail < need) {
            lnd_store_relaxed(&c->starved, true);
            return 0;
        }
        lnd_store_relaxed(&c->starved, false);
    }
    while (got < frames) {
        uint64_t head = lnd_load(&c->head);
        uint64_t tail = lnd_load_relaxed(&c->tail);
        uint64_t avail = head - tail;
        if (avail > c->cap) {
            uint64_t keep = c->cap / 2;
            lnd_add(&c->overruns, avail - keep);
            tail = head - keep;
            avail = keep;
        }
        if (avail == 0) {
            if (lnd_load(&c->ended) || mode != LND_CAPTURE_READ_WAIT) break;
            if (waited >= LND_CAPTURE_STALL_MS) break;
            lnd_event_wait(&c->event, LND_CAPTURE_WAIT_MS);
            waited += LND_CAPTURE_WAIT_MS;
            continue;
        }
        waited = 0;
        uint32_t n = (uint32_t)LND_MIN(avail, frames - got);
        uint32_t idx = (uint32_t)(tail & c->mask);
        uint32_t first = LND_MIN(n, c->cap - idx);
        memcpy(dst + got * ch, c->ring + (size_t)idx * ch, (size_t)first * ch * sizeof(float));
        if (n > first) memcpy(dst + (got + first) * ch, c->ring, (size_t)(n - first) * ch * sizeof(float));
        lnd_store(&c->tail, tail + n);
        got += n;
    }
    return got;
}

typedef struct lnd_capture_tap {
    lnd_source base;
    lnd_capture *c;
    int32_t mode;
} lnd_capture_tap;

typedef struct lnd_capture_source {
    lnd_source base;
    lnd_capture *c;
    lnd_capture_tap tap;
    lnd_source *resampler;
    float *matrix;
    float *scratch;
    uint32_t scratch_frames;
    uint32_t built_rate;
    uint32_t built_channels;
    uint32_t built_generation;
    uint32_t flags;
    bool built;
    lnd_spinlock lock;
} lnd_capture_source;

static uint64_t lnd_capture_tap_read(lnd_source *src, float *dst, uint64_t frames) {
    lnd_capture_tap *t = (lnd_capture_tap *)src;
    uint64_t got = lnd_capture_read(t->c, dst, frames, t->mode);
    lnd_store_relaxed(&src->pos, lnd_load_relaxed(&src->pos) + got);
    if (got < frames && lnd_load(&t->c->ended)) lnd_store(&src->status, LND_SOURCE_EOF);
    return got;
}

static void lnd_capture_tap_free(lnd_source *src) { (void)src; }

static const lnd_source_vt lnd_capture_tap_vt = {
    .read = lnd_capture_tap_read,
    .free = lnd_capture_tap_free,
};

static void lnd_capture_source_teardown(lnd_capture_source *s) {
    lnd_source_free(s->resampler);
    s->resampler = nullptr;
    lnd_free(s->matrix);
    s->matrix = nullptr;
    lnd_free_aligned(s->scratch);
    s->scratch = nullptr;
    s->built = false;
}

static int32_t lnd_capture_source_prepare(lnd_capture_source *s) {
    lnd_capture *c = s->c;
    uint32_t gen = lnd_load(&c->generation);
    uint32_t sample_rate_hz = lnd_load(&c->sample_rate_hz);
    if (s->built && s->built_rate == sample_rate_hz && s->built_channels == c->channels && s->built_generation == gen) return LND_OK;
    if (s->built && s->built_generation != gen) {
        lnd_store(&c->tail, lnd_load(&c->head));
        lnd_store_relaxed(&c->starved, true);
    }
    lnd_capture_source_teardown(s);
    uint32_t sc = c->channels, dc = s->base.channels;
    s->tap.base.vt = &lnd_capture_tap_vt;
    s->tap.base.live = true;
    lnd_store(&s->tap.base.status, LND_SOURCE_READY);
    lnd_store(&s->base.status, LND_SOURCE_READY);
    s->tap.base.channels = sc;
    s->tap.base.sample_rate_hz = sample_rate_hz;
    s->tap.c = c;
    s->scratch_frames = LND_MAX(lnd_cfg_u32(LND_CFG_GRAPH_MIX_BLOCK_FRAMES), 1024u);
    s->scratch = lnd_alloc_aligned((size_t)s->scratch_frames * sc * sizeof(float), LND_CACHE_LINE);
    if (!s->scratch) return LND_ERR_OUT_OF_MEMORY;
    if (sample_rate_hz != s->base.sample_rate_hz) {
        s->resampler = lnd_resample_source_create(&s->tap.base, false, s->base.sample_rate_hz, s->scratch_frames, lnd_cfg_u32(LND_CFG_AUDIO_RESAMPLE_QUALITY));
        if (!s->resampler) return LND_ERR_OUT_OF_MEMORY;
    }
    if (sc != dc) {
        s->matrix = lnd_alloc((size_t)sc * dc * sizeof(float));
        if (!s->matrix) return LND_ERR_OUT_OF_MEMORY;
        lnd_channels_matrix(sc, dc, (int32_t)lnd_cfg_u32(LND_CFG_AUDIO_CHANNEL_MIX), s->matrix);
    }
    s->built = true;
    s->built_rate = sample_rate_hz;
    s->built_channels = c->channels;
    s->built_generation = gen;
    return LND_OK;
}

static uint64_t lnd_capture_source_pull(lnd_capture_source *s, float *dst, uint64_t frames, int32_t mode) {
    lnd_spinlock_lock(&s->lock);
    if (lnd_load(&s->c->paused) || lnd_capture_source_prepare(s) != LND_OK) {
        lnd_spinlock_unlock(&s->lock);
        return 0;
    }
    s->tap.mode = mode;
    lnd_source *stage = s->resampler ? s->resampler : &s->tap.base;
    uint32_t sc = s->c->channels, dc = s->base.channels;
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
    lnd_store_relaxed(&s->base.pos, lnd_load_relaxed(&s->base.pos) + total);
    lnd_store(&s->base.status, lnd_source_status(stage));
    lnd_spinlock_unlock(&s->lock);
    return total;
}

static uint64_t lnd_capture_source_read(lnd_source *src, float *dst, uint64_t frames) {
    return lnd_capture_source_pull((lnd_capture_source *)src, dst, frames, LND_CAPTURE_READ_STREAM);
}

static uint64_t lnd_capture_source_read_sync(lnd_source *src, float *dst, uint64_t frames) {
    lnd_capture_source *s = (lnd_capture_source *)src;
    return lnd_capture_source_pull(s, dst, frames, s->flags & LND_CAPTURE_NONBLOCKING ? LND_CAPTURE_READ_PARTIAL : LND_CAPTURE_READ_WAIT);
}

static uint64_t lnd_capture_source_available(lnd_source *src) {
    lnd_capture_source *s = (lnd_capture_source *)src;
    lnd_capture *c = s->c;
    if (lnd_load(&c->paused)) return 0;
    if (lnd_load(&c->ended)) return UINT64_MAX;
    uint64_t avail = lnd_capture_available(c);
    if (lnd_load_relaxed(&c->starved) && avail < c->cap / 2) avail = avail < lnd_load(&c->prefill) ? 0 : avail / 2;
    uint32_t sample_rate_hz = lnd_load(&c->sample_rate_hz);
    if (sample_rate_hz != src->sample_rate_hz)
        avail = avail > 128 ? (uint64_t)((double)(avail - 128) * (double)src->sample_rate_hz / (double)sample_rate_hz) : 0;
    return avail;
}

static void lnd_capture_source_free(lnd_source *src) {
    lnd_capture_source *s = (lnd_capture_source *)src;
#if LND_MODULE_DEVICES
    if (s->c->instance) lnd_instance_close(s->c->instance);
#endif
    lnd_capture_source_teardown(s);
    lnd_capture_free(s->c);
    lnd_free(s);
}

static const lnd_source_vt lnd_capture_source_vt = {
    .read = lnd_capture_source_read,
    .free = lnd_capture_source_free,
    .read_sync = lnd_capture_source_read_sync,
    .available = lnd_capture_source_available,
};

lnd_source *lnd_capture_source_create(lnd_capture *c, uint32_t channels, uint32_t sample_rate_hz, uint32_t flags) {
    lnd_capture_source *s = lnd_alloc_zero(sizeof *s);
    if (!s) return nullptr;
    s->base.vt = &lnd_capture_source_vt;
    s->base.live = true;
    s->base.channels = channels ? channels : c->channels;
    s->base.sample_rate_hz = (flags & LND_CAPTURE_RESAMPLE) && sample_rate_hz ? sample_rate_hz : c->sample_rate_hz;
    s->c = c;
    s->flags = flags;
    return &s->base;
}

static lnd_graph_source *lnd_capture_object(const LND_SOURCE *source) {
    for (lnd_graph_source *o = lnd_graph_ctx.sources; o; o = o->next)
        if (&o->base == source && o->source && o->source->vt == &lnd_capture_source_vt) return o;
    return nullptr;
}

static int32_t lnd_capture_control(LND_SOURCE *source, bool pause, bool reset, LND_CAPTURE_INFO *info) {
    if (!source) return LND_ERR_INVALID_ARG;
    if (lnd_callback_active() || !lnd_context_enter()) return LND_ERR_BUSY;
    lnd_graph_source *o = lnd_capture_object(source);
    if (!o) {
        lnd_context_unlock();
        return LND_ERR_INVALID_ARG;
    }
    lnd_capture_source *s = (lnd_capture_source *)o->source;
    lnd_capture *c = s->c;
    if (o->node) lnd_spinlock_lock(&o->node->lock);
    lnd_spinlock_lock(&s->lock);
    lnd_spinlock_lock(&c->control);
    if (info) {
        *info = (LND_CAPTURE_INFO){.dropped_frames = lnd_load(&c->overruns),
                                   .buffered_frames = (uint32_t)lnd_capture_available(c),
                                   .capacity_frames = c->cap,
                                   .paused = lnd_load(&c->paused) != 0};
    } else if (reset || (lnd_load(&c->paused) != 0) != pause) {
        if (!reset) lnd_store(&c->paused, pause);
        lnd_store(&c->tail, lnd_load(&c->head));
        lnd_store(&c->starved, 1);
        if (reset) lnd_store(&c->overruns, 0);
        lnd_capture_source_teardown(s);
        lnd_store(&s->base.status, LND_SOURCE_WAITING);
        if (o->node) {
            lnd_add(&o->node->revision, 1);
            lnd_store(&o->node->status, LND_SOURCE_WAITING);
            if (o->node->sound) o->node->sound->reset_pending = true;
        }
    }
    lnd_spinlock_unlock(&c->control);
    lnd_spinlock_unlock(&s->lock);
    if (o->node) lnd_spinlock_unlock(&o->node->lock);
    lnd_context_unlock();
    return LND_OK;
}
int32_t LND_SourceSetCapturePause(LND_SOURCE *source, bool pause) { return lnd_capture_control(source, pause, false, nullptr); }
int32_t LND_SourceResetCapture(LND_SOURCE *source) { return lnd_capture_control(source, false, true, nullptr); }
int32_t LND_SourceGetCaptureInfo(const LND_SOURCE *source, LND_CAPTURE_INFO *info) {
    return info ? lnd_capture_control((LND_SOURCE *)source, false, false, info) : LND_ERR_INVALID_ARG;
}
