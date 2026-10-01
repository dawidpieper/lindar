#include "src/alloc.h"
#include "src/atomic.h"
#include "src/thread.h"
#include "pcm/audio/source.h"
#include "src/config.h"
#include "src/error.h"
#include "src/pcm.h"

#include <string.h>

typedef struct lnd_stream_source {
    lnd_source base;
    lnd_source *inner;
    bool owns_inner;
    LND_PCM pcm;
    uint64_t *chunk_pos;
    uint32_t *chunk_len;
    uint32_t chunk_frames;
    uint32_t chunk_count;
    uint32_t mask;
    uint32_t read_off;
    alignas(LND_CACHE_LINE) lnd_atomic_u32 head;
    alignas(LND_CACHE_LINE) lnd_atomic_u32 tail;
    alignas(LND_CACHE_LINE) lnd_atomic_u32 seek_request;
    lnd_atomic_u32 seek_done;
    lnd_atomic_u64 seek_target;
    lnd_atomic_u32 reading;
    lnd_atomic_u32 ended;
    lnd_atomic_u32 underruns;
    struct lnd_stream_source *next;
} lnd_stream_source;

enum { LND_WORKER_IDLE, LND_WORKER_REQUESTED, LND_WORKER_ACTIVE };

static struct {
    lnd_thread thread;
    lnd_event event;
    lnd_mutex mutex;
    lnd_stream_source *list;
    lnd_atomic_u32 stop;
    lnd_atomic_u32 wake;
    bool running;
} lnd_worker;

static bool lnd_stream_seeking(lnd_stream_source *s) { return lnd_load_seq(&s->seek_request) != lnd_load_seq(&s->seek_done); }

static void lnd_worker_wake(void) {
    if (lnd_exchange(&lnd_worker.wake, LND_WORKER_REQUESTED) == LND_WORKER_IDLE) lnd_event_signal(&lnd_worker.event);
}

static bool lnd_stream_fill(lnd_stream_source *s, uint32_t chunks) {
    bool progress = false;
    uint32_t request = lnd_load_seq(&s->seek_request);
    if (request != lnd_load_seq(&s->seek_done)) {
        while (lnd_load_seq(&s->reading)) {
        }
        lnd_source_seek(s->inner, lnd_load(&s->seek_target));
        lnd_store_relaxed(&s->head, 0);
        lnd_store_relaxed(&s->tail, 0);
        s->read_off = 0;
        s->base.pos = s->inner->pos;
        lnd_store(&s->ended, 0);
        lnd_store_seq(&s->seek_done, request);
    }
    while (chunks-- && !lnd_load_relaxed(&s->ended)) {
        uint32_t head = lnd_load(&s->head);
        uint32_t tail = lnd_load_relaxed(&s->tail);
        if (tail - head >= s->chunk_count) break;
        uint32_t idx = tail & s->mask;
        uint64_t start = s->inner->pos;
        int64_t got = lnd_source_read_pcm(s->inner, &s->pcm, (size_t)idx * s->chunk_frames, s->chunk_frames, false);
        uint32_t n = got > 0 ? (uint32_t)got : 0;
        int32_t status = lnd_source_status(s->inner);
        if (n) {
            progress = true;
            s->chunk_pos[idx] = start;
            s->chunk_len[idx] = n;
            lnd_store(&s->tail, tail + 1);
        }
        if (status == LND_SOURCE_EOF || status < 0) lnd_store(&s->ended, 1);
        if (!n) break;
    }
    return progress;
}

static void lnd_worker_proc(void *user) {
    LND_UNUSED(user);
    lnd_thread_com_init();
    while (!lnd_load(&lnd_worker.stop)) {
        lnd_exchange(&lnd_worker.wake, LND_WORKER_ACTIVE);
        bool progress = false;
        lnd_mutex_lock(&lnd_worker.mutex);
        for (lnd_stream_source *s = lnd_worker.list; s && !lnd_load(&lnd_worker.stop); s = s->next)
            progress |= lnd_stream_fill(s, 1);
        lnd_mutex_unlock(&lnd_worker.mutex);
        if (!progress && lnd_exchange(&lnd_worker.wake, LND_WORKER_IDLE) != LND_WORKER_REQUESTED) lnd_event_wait(&lnd_worker.event, 50);
    }
    lnd_thread_com_free();
}

void lnd_stream_worker_init(void) {
    lnd_mutex_init(&lnd_worker.mutex);
    lnd_worker.list = nullptr;
    lnd_worker.running = false;
}

static int32_t lnd_worker_register(lnd_stream_source *s) {
    lnd_mutex_lock(&lnd_worker.mutex);
    if (!lnd_worker.running) {
        lnd_store(&lnd_worker.stop, 0);
        lnd_store(&lnd_worker.wake, LND_WORKER_IDLE);
        if (lnd_event_init(&lnd_worker.event) != LND_OK || lnd_thread_create(&lnd_worker.thread, lnd_worker_proc, nullptr) != LND_OK) {
            lnd_event_free(&lnd_worker.event);
            lnd_mutex_unlock(&lnd_worker.mutex);
            return LND_ERR_EXTERNAL;
        }
        lnd_worker.running = true;
    }
    s->next = lnd_worker.list;
    lnd_worker.list = s;
    lnd_stream_fill(s, s->chunk_count);
    lnd_mutex_unlock(&lnd_worker.mutex);
    return LND_OK;
}

static void lnd_worker_unregister(lnd_stream_source *s) {
    lnd_mutex_lock(&lnd_worker.mutex);
    for (lnd_stream_source **p = &lnd_worker.list; *p; p = &(*p)->next) {
        if (*p == s) {
            *p = s->next;
            break;
        }
    }
    lnd_mutex_unlock(&lnd_worker.mutex);
}

void lnd_stream_worker_shutdown(void) {
    lnd_mutex_lock(&lnd_worker.mutex);
    bool running = lnd_worker.running;
    lnd_worker.running = false;
    lnd_mutex_unlock(&lnd_worker.mutex);
    if (!running) {
        lnd_mutex_free(&lnd_worker.mutex);
        return;
    }
    lnd_store(&lnd_worker.stop, 1);
    lnd_event_signal(&lnd_worker.event);
    lnd_thread_join(&lnd_worker.thread);
    lnd_event_free(&lnd_worker.event);
    lnd_mutex_free(&lnd_worker.mutex);
}

static int64_t lnd_stream_consume(lnd_stream_source *s, const LND_PCM *pcm, size_t offset, size_t frames, bool sync) {
    lnd_source *src = &s->base;

    uint64_t got = 0;
    while (got < frames) {
        uint32_t head = lnd_load_relaxed(&s->head);
        uint32_t tail = lnd_load(&s->tail);
        if (head == tail) {
            if (lnd_load(&s->ended)) {
                if (head != lnd_load(&s->tail)) continue;
                lnd_store(&src->status, lnd_source_status(s->inner));
                break;
            }
            if (sync) {
                lnd_stream_fill(s, s->chunk_count);
                if (lnd_load_relaxed(&s->head) == lnd_load(&s->tail)) break;
                continue;
            }
            lnd_add(&s->underruns, 1);
            break;
        }
        uint32_t idx = head & s->mask;
        uint32_t avail = s->chunk_len[idx] - s->read_off;
        uint32_t n = (uint32_t)LND_MIN((uint64_t)avail, frames - got);
        int32_t result = LND_PcmConvert(pcm, offset + (size_t)got, &s->pcm, (size_t)idx * s->chunk_frames + s->read_off, n);
        if (result != LND_OK) return result;
        s->read_off += n;
        got += n;
        src->pos = s->chunk_pos[idx] + s->read_off;
        if (s->read_off == s->chunk_len[idx]) {
            s->read_off = 0;
            lnd_store(&s->head, head + 1);
            if (!sync) lnd_worker_wake();
        }
    }
    return got;
}

static int64_t lnd_stream_read_pcm(lnd_source *src, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_stream_source *s = (lnd_stream_source *)src;
    lnd_store_seq(&s->reading, 1);
    if (lnd_stream_seeking(s)) {
        lnd_store(&src->status, LND_SOURCE_WAITING);
        lnd_store_seq(&s->reading, 0);
        return 0;
    }
    int64_t got = lnd_stream_consume(s, pcm, offset, frames, false);
    lnd_store_seq(&s->reading, 0);
    return got;
}

static int64_t lnd_stream_read_pcm_sync(lnd_source *src, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_stream_source *s = (lnd_stream_source *)src;
    lnd_mutex_lock(&lnd_worker.mutex);
    if (lnd_stream_seeking(s)) lnd_stream_fill(s, s->chunk_count);
    int64_t got = lnd_stream_consume(s, pcm, offset, frames, true);
    lnd_mutex_unlock(&lnd_worker.mutex);
    return got;
}

static uint64_t lnd_stream_read(lnd_source *src, float *dst, uint64_t frames) {
    LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = src->channels, .format = LND_FORMAT_F32};
    int64_t got = lnd_stream_read_pcm(src, &pcm, 0, (size_t)frames);
    return got > 0 ? (uint64_t)got : 0;
}

static uint64_t lnd_stream_read_sync(lnd_source *src, float *dst, uint64_t frames) {
    LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = src->channels, .format = LND_FORMAT_F32};
    int64_t got = lnd_stream_read_pcm_sync(src, &pcm, 0, (size_t)frames);
    return got > 0 ? (uint64_t)got : 0;
}

static void lnd_stream_sync(lnd_source *src) {
    lnd_stream_source *s = (lnd_stream_source *)src;
    lnd_mutex_lock(&lnd_worker.mutex);
    lnd_stream_fill(s, s->chunk_count);
    lnd_mutex_unlock(&lnd_worker.mutex);
}

static int32_t lnd_stream_seek(lnd_source *src, uint64_t frame) {
    lnd_stream_source *s = (lnd_stream_source *)src;
    if (!lnd_source_can_seek(s->inner)) return LND_ERR_UNSUPPORTED;
    lnd_store(&s->seek_target, frame);
    lnd_add(&s->seek_request, 1);
    lnd_worker_wake();
    return LND_OK;
}

static uint64_t lnd_stream_length(lnd_source *src) { return lnd_source_length(((lnd_stream_source *)src)->inner); }

static void lnd_stream_free(lnd_source *src) {
    lnd_stream_source *s = (lnd_stream_source *)src;
    lnd_worker_unregister(s);
    if (s->owns_inner) lnd_source_free(s->inner);
    lnd_free_aligned(s->pcm.data);
    lnd_free(s->chunk_pos);
    lnd_free(s->chunk_len);
    lnd_free_aligned(s);
}

static int32_t lnd_stream_end(lnd_source *src) { return lnd_source_end(((lnd_stream_source *)src)->inner); }

static const lnd_source_vt lnd_stream_vt = {
    .read = lnd_stream_read,
    .read_pcm = lnd_stream_read_pcm,
    .read_pcm_sync = lnd_stream_read_pcm_sync,
    .seek = lnd_stream_seek,
    .length = lnd_stream_length,
    .free = lnd_stream_free,
    .read_sync = lnd_stream_read_sync,
    .sync = lnd_stream_sync,
    .end = lnd_stream_end,
};

static const lnd_source_vt lnd_stream_vt_noseek = {
    .read = lnd_stream_read,
    .read_pcm = lnd_stream_read_pcm,
    .read_pcm_sync = lnd_stream_read_pcm_sync,
    .seek = nullptr,
    .length = lnd_stream_length,
    .free = lnd_stream_free,
    .read_sync = lnd_stream_read_sync,
    .sync = lnd_stream_sync,
    .end = lnd_stream_end,
};

lnd_source *lnd_stream_source_create(lnd_source *inner, bool owns_inner, uint32_t chunk_frames, uint32_t chunk_count) {
    int32_t format = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT);
    size_t width = inner->channels * LND_PcmGetSampleBytes(format);
    if (!chunk_frames || chunk_count > (UINT32_MAX >> 1) || chunk_frames > SIZE_MAX / width) return nullptr;
    chunk_count = lnd_next_pow2_u32(chunk_count < 2 ? 2 : chunk_count);
    if (chunk_count > SIZE_MAX / ((size_t)chunk_frames * width) || (size_t)chunk_count > SIZE_MAX / sizeof(uint64_t)) return nullptr;
    size_t bytes = sizeof(lnd_stream_source) + inner->channels * sizeof(void *);
    lnd_stream_source *s = lnd_alloc_aligned(bytes, alignof(lnd_stream_source));
    if (!s) return nullptr;
    memset(s, 0, bytes);
    s->base.vt = lnd_source_can_seek(inner) ? &lnd_stream_vt : &lnd_stream_vt_noseek;
    s->base.channels = inner->channels;
    s->base.sample_rate_hz = inner->sample_rate_hz;
    s->inner = inner;
    s->base.live = true;
    s->base.length_known = inner->length_known;
    s->base.length_estimated = inner->length_estimated;
    s->owns_inner = owns_inner;
    s->chunk_frames = chunk_frames;
    s->chunk_count = chunk_count;
    s->mask = chunk_count - 1;
    s->pcm = (LND_PCM){.frames = (size_t)chunk_count * chunk_frames,
                       .channels = inner->channels,
                       .format = format,
                       .layout = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_LAYOUT),
                       .planes = (void **)(s + 1)};
    s->pcm.data = lnd_alloc_aligned(s->pcm.frames * width, LND_CACHE_LINE);
    if (s->pcm.data)
        for (uint32_t c = 0; c < inner->channels; c++)
            ((void **)(s + 1))[c] = (uint8_t *)s->pcm.data + c * s->pcm.frames * LND_PcmGetSampleBytes(format);
    s->chunk_pos = lnd_alloc_zero(sizeof(uint64_t) * chunk_count);
    s->chunk_len = lnd_alloc_zero(sizeof(uint32_t) * chunk_count);
    if (!s->pcm.data || !s->chunk_pos || !s->chunk_len || lnd_worker_register(s) != LND_OK) {
        s->owns_inner = false;
        lnd_stream_free(&s->base);
        return nullptr;
    }
    return &s->base;
}

static bool lnd_stream_initialized;

int32_t lnd_stream_init(void) {
    lnd_stream_worker_init();
    lnd_stream_initialized = true;
    return LND_OK;
}

void lnd_stream_cleanup(void) {
    if (lnd_stream_initialized) lnd_stream_worker_shutdown();
    lnd_stream_initialized = false;
}
