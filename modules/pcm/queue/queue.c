#include "lindar_queue.h"
#include "src/native.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/render.h"
#include "src/pcm.h"

#include <string.h>

typedef struct lnd_queue {
    LND_PCM pcm;
    lnd_atomic_u32 count;
    uint32_t head;
} lnd_queue;

static int64_t lnd_queue_read(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_queue *q = user;
    uint32_t count = lnd_load_relaxed(&q->count);
    size_t take = LND_MIN(frames, count);
    size_t first = LND_MIN(take, q->pcm.frames - q->head);
    int32_t result = LND_PcmConvert(pcm, offset, &q->pcm, q->head, first);
    if (result == LND_OK && take > first) result = LND_PcmConvert(pcm, offset + first, &q->pcm, 0, take - first);
    if (result != LND_OK) return result;
    q->head += (uint32_t)take;
    if (q->head >= q->pcm.frames) q->head -= (uint32_t)q->pcm.frames;
    lnd_store(&q->count, count - (uint32_t)take);
    return (int64_t)take;
}

static LND_SOURCE_CONFIG lnd_queue_config(uint32_t channels, uint32_t sample_rate_hz) {
    return (LND_SOURCE_CONFIG){.read = lnd_queue_read, .channels = channels, .sample_rate_hz = sample_rate_hz, .flags = LND_SOURCE_LIVE};
}

static size_t lnd_queue_offset(const LND_SOURCE_CONFIG *config) {
    size_t bytes = LND_SourceGetMemoryBytes(config);
    return bytes ? (bytes + alignof(max_align_t) - 1) / alignof(max_align_t) * alignof(max_align_t) : 0;
}

size_t LND_QueueGetMemoryBytes(uint32_t channels, uint32_t sample_rate_hz, uint32_t capacity) {
    LND_SOURCE_CONFIG config = lnd_queue_config(channels, sample_rate_hz);
    size_t base = lnd_queue_offset(&config);
    if (!base || !capacity || capacity > UINT32_MAX / 2) return 0;
    base += sizeof(lnd_queue) + channels * sizeof(void *);
    size_t width = channels * LND_PcmGetSampleBytes((int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT));
    return capacity <= (SIZE_MAX - base) / width ? base + (size_t)capacity * width : 0;
}

LND_SOURCE *LND_QueueInit(void *memory, size_t bytes, uint32_t channels, uint32_t sample_rate_hz, uint32_t capacity) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    size_t need = LND_QueueGetMemoryBytes(channels, sample_rate_hz, capacity);
    if (!need || !memory || bytes < need || (uintptr_t)memory % alignof(max_align_t)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    if (lnd_playback_overlaps(memory, need) || lnd_renderers_overlap(memory, need)) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_BUSY);
    }
    LND_SOURCE_CONFIG config = lnd_queue_config(channels, sample_rate_hz);
    lnd_queue *q = (lnd_queue *)((uint8_t *)memory + lnd_queue_offset(&config));
    config.user = q;
    LND_SOURCE *source = LND_SourceInit(memory, bytes, &config);
    if (source) {
        memset(q, 0, sizeof *q);
        q->pcm = (LND_PCM){.planes = (void **)(q + 1),
                           .data = (void **)(q + 1) + channels,
                           .channels = channels,
                           .frames = capacity,
                           .format = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT),
                           .layout = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_LAYOUT)};
        for (uint32_t c = 0; c < channels; c++)
            ((void **)(q + 1))[c] = (uint8_t *)q->pcm.data + (size_t)c * capacity * LND_PcmGetSampleBytes(q->pcm.format);
        ((lnd_native_source *)source)->bytes = need;
    }
    lnd_context_unlock();
    return source;
}

LND_SOURCE *LND_SourceCreateQueue(uint32_t channels, uint32_t sample_rate_hz, uint32_t capacity) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    size_t bytes = LND_QueueGetMemoryBytes(channels, sample_rate_hz, capacity);
    if (!bytes) return lnd_error_null(LND_ERR_INVALID_ARG);
    void *memory = lnd_alloc(bytes);
    if (!memory) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    LND_SOURCE *source = LND_QueueInit(memory, bytes, channels, sample_rate_hz, capacity);
    if (source)
        ((lnd_native_source *)source)->owned = true;
    else
        lnd_free(memory);
    return source;
}

static lnd_native_source *lnd_queue_source(const LND_SOURCE *source) {
    if (!source || source->ops) return nullptr;
    lnd_native_source *s = (lnd_native_source *)source;
    return s->config.read == lnd_queue_read ? s : nullptr;
}

int64_t LND_QueueWritePcm(LND_SOURCE *source, const LND_PCM *pcm, size_t offset, size_t frames) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    lnd_native_source *s = lnd_queue_source(source);
    if (!s || !lnd_pcm_range(pcm, offset, frames) || pcm->channels != s->config.channels) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_spinlock_try(&s->lock)) return lnd_error(LND_ERR_BUSY);
    lnd_queue *q = s->config.user;
    int64_t result = LND_ERR_STATE;
    if (!s->end_requested) {
        uint32_t count = lnd_load_relaxed(&q->count);
        size_t take = LND_MIN(frames, q->pcm.frames - count);
        uint32_t at = q->head + count;
        if (at >= q->pcm.frames) at -= (uint32_t)q->pcm.frames;
        size_t first = LND_MIN(take, q->pcm.frames - at);
        result = LND_PcmConvert(&q->pcm, at, pcm, offset, first);
        if (result == LND_OK && take > first) result = LND_PcmConvert(&q->pcm, 0, pcm, offset + first, take - first);
        if (result == LND_OK) {
            lnd_store(&q->count, count + (uint32_t)take);
            result = (int64_t)take;
        }
    }
    lnd_spinlock_unlock(&s->lock);
    return result < 0 ? lnd_error((int32_t)result) : result;
}

uint32_t LND_QueueGetBufferedFrames(const LND_SOURCE *source) {
    lnd_native_source *s = lnd_queue_source(source);
    return s ? lnd_load(&((lnd_queue *)s->config.user)->count) : 0;
}

uint32_t LND_QueueGetCapacityFrames(const LND_SOURCE *source) {
    lnd_native_source *s = lnd_queue_source(source);
    return s ? (uint32_t)((lnd_queue *)s->config.user)->pcm.frames : 0;
}

int64_t LND_QueueDiscardFrames(LND_SOURCE *source, size_t frames) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    lnd_native_source *s = lnd_queue_source(source);
    if (!s) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_spinlock_try(&s->lock)) return lnd_error(LND_ERR_BUSY);
    lnd_queue *q = s->config.user;
    uint32_t count = lnd_load_relaxed(&q->count), take = (uint32_t)LND_MIN(frames, count);
    q->head += take;
    if (q->head >= q->pcm.frames) q->head -= (uint32_t)q->pcm.frames;
    lnd_store(&q->count, count - take);
    lnd_spinlock_unlock(&s->lock);
    return take;
}

int32_t LND_QueueReset(LND_SOURCE *source) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    lnd_native_source *s = lnd_queue_source(source);
    if (!s) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_spinlock_try(&s->lock)) return lnd_error(LND_ERR_BUSY);
    lnd_queue *q = s->config.user;
    q->head = 0;
    lnd_store(&q->count, 0);
    lnd_store(&s->position, 0);
    lnd_store(&s->status, LND_SOURCE_READY);
    s->end_requested = false;
    if (s->sound) {
        s->sound->ended = false;
        lnd_store(&s->sound->state, LND_SOUND_STOPPED);
    }
    lnd_spinlock_unlock(&s->lock);
    return LND_OK;
}
