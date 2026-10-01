#pragma once

#include "src/platform.h"
#include "src/atomic.h"
#include "lindar.h"

typedef struct LND_SOURCE_PROCS LND_SOURCE_PROCS;
typedef struct LND_BUFFER LND_BUFFER;

typedef struct lnd_source lnd_source;

typedef struct lnd_source_vt {
    uint64_t (*read)(lnd_source *s, float *dst, uint64_t frames);
    int32_t (*seek)(lnd_source *s, uint64_t frame);
    uint64_t (*length)(lnd_source *s);
    void (*free)(lnd_source *s);
    uint64_t (*read_sync)(lnd_source *s, float *dst, uint64_t frames);
    void (*sync)(lnd_source *s);
    uint64_t (*available)(lnd_source *s);
    int64_t (*read_pcm)(lnd_source *s, const LND_PCM *pcm, size_t offset, size_t frames);
    int64_t (*read_pcm_sync)(lnd_source *s, const LND_PCM *pcm, size_t offset, size_t frames);
    int32_t (*end)(lnd_source *s);
} lnd_source_vt;

struct lnd_source {
    const lnd_source_vt *vt;
    uint32_t channels;
    uint32_t sample_rate_hz;
    lnd_atomic_u64 pos;
    lnd_atomic_i32 status;
    lnd_atomic_u32 end_requested;
    int32_t deferred_error;
    bool live;
    bool length_known;
    bool length_estimated;
};

LND_INLINE int32_t lnd_source_status(const lnd_source *s) { return lnd_load(&s->status); }

LND_INLINE uint64_t lnd_source_limit(lnd_source *s) { return !s->length_estimated && s->vt->length ? s->vt->length(s) : 0; }

LND_INLINE int64_t lnd_source_result(lnd_source *s, int64_t got, uint64_t frames) {
    if (!frames) return got;
    if (got == LND_READ_EOF) {
        lnd_store(&s->status, LND_SOURCE_EOF);
        return 0;
    }
    if (got < 0 || (uint64_t)got > frames) {
        int32_t error = got < 0 && got >= LND_ERR_CYCLE ? (int32_t)got : LND_ERR_IO;
        lnd_store(&s->status, error);
        return error;
    }
    if (got > 0 && lnd_source_status(s) < 0) s->deferred_error = lnd_source_status(s);
    if (lnd_source_status(s) == LND_SOURCE_EOF || lnd_source_status(s) < 0) return got;
    uint64_t length = lnd_source_limit(s);
    bool eof = (!s->live && (uint64_t)got < frames) || (!got && lnd_load(&s->end_requested)) ||
               (length && s->pos >= length && (got || lnd_source_status(s) != LND_SOURCE_WAITING));
    lnd_store(&s->status, eof ? LND_SOURCE_EOF : ((uint64_t)got < frames ? LND_SOURCE_WAITING : LND_SOURCE_READY));
    return got;
}

LND_INLINE uint64_t lnd_source_read(lnd_source *s, float *dst, uint64_t frames) {
    if (lnd_source_status(s) == LND_SOURCE_EOF) return 0;
    if (s->deferred_error) {
        lnd_store(&s->status, s->deferred_error);
        s->deferred_error = LND_OK;
        return 0;
    }
    if (lnd_source_status(s) < 0) lnd_store(&s->status, LND_SOURCE_READY);
    int64_t got = lnd_source_result(s, (int64_t)s->vt->read(s, dst, frames), frames);
    return got > 0 ? (uint64_t)got : 0;
}

LND_INLINE uint64_t lnd_source_read_sync(lnd_source *s, float *dst, uint64_t frames) {
    if (lnd_source_status(s) == LND_SOURCE_EOF) return 0;
    if (s->deferred_error) {
        lnd_store(&s->status, s->deferred_error);
        s->deferred_error = LND_OK;
        return 0;
    }
    if (lnd_source_status(s) < 0) lnd_store(&s->status, LND_SOURCE_READY);
    int64_t got = lnd_source_result(s, (int64_t)(s->vt->read_sync ? s->vt->read_sync(s, dst, frames) : s->vt->read(s, dst, frames)), frames);
    return got > 0 ? (uint64_t)got : 0;
}

LND_INLINE void lnd_source_sync(lnd_source *s) {
    if (s->vt->sync) s->vt->sync(s);
}

LND_INLINE uint64_t lnd_source_available(lnd_source *s) { return s->vt->available ? s->vt->available(s) : UINT64_MAX; }

LND_INLINE int32_t lnd_source_seek(lnd_source *s, uint64_t frame) {
    int32_t result = s->vt->seek ? s->vt->seek(s, frame) : LND_ERR_UNSUPPORTED;
    if (result == LND_OK) {
        s->deferred_error = LND_OK;
        lnd_store(&s->status, LND_SOURCE_READY);
        lnd_store(&s->end_requested, 0);
    }
    return result;
}

LND_INLINE int32_t lnd_source_end(lnd_source *s) {
    if (s->vt->end) return s->vt->end(s);
    if (!s->live) return LND_ERR_UNSUPPORTED;
    lnd_store(&s->end_requested, 1);
    return LND_OK;
}

LND_INLINE bool lnd_source_can_seek(const lnd_source *s) { return s->vt->seek != nullptr; }

LND_INLINE uint64_t lnd_source_length(lnd_source *s) { return s->vt->length ? s->vt->length(s) : 0; }

LND_INLINE void lnd_source_free(lnd_source *s) {
    if (s) s->vt->free(s);
}

lnd_source *lnd_buffer_source_create(LND_BUFFER *b);
lnd_source *lnd_proc_source_create(const LND_SOURCE_PROCS *procs, void *user, int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint32_t max_frames);
void lnd_proc_source_set_close(lnd_source *source, LND_RENDER_CLOSE_PROC close);
lnd_source *lnd_stream_source_create(lnd_source *inner, bool owns_inner, uint32_t chunk_frames, uint32_t chunk_count);
lnd_source *lnd_resample_source_create(lnd_source *inner, bool owns_inner, uint32_t out_rate, uint32_t scratch_frames, uint32_t quality);
lnd_source *lnd_resample_source_create_variable(lnd_source *inner, uint32_t scratch_frames, uint32_t quality);
void lnd_resample_source_set_ratio(lnd_source *resampler, double ratio);
void lnd_resample_source_set_live(lnd_source *resampler, bool live);
void lnd_resample_source_reset(lnd_source *source);
bool lnd_resample_source_drained(const lnd_source *resampler, uint64_t frames_after_end);

void lnd_stream_worker_init(void);
void lnd_stream_worker_shutdown(void);

int64_t lnd_source_read_pcm(lnd_source *source, const LND_PCM *pcm, size_t offset, size_t frames, bool sync);
