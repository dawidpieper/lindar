#include "lindar_graph.h"
#include "src/alloc.h"
#include "src/callback.h"
#include "pcm/audio/convert.h"
#include "src/format.h"
#include "src/pcm.h"
#include "src/error.h"
#include "pcm/audio/source.h"

typedef struct lnd_proc_source {
    lnd_source base;
    LND_SOURCE_PROCS procs;
    void *user;
    int32_t format;
    uint32_t max_frames;
    void *scratch;
} lnd_proc_source;

void lnd_proc_source_set_close(lnd_source *source, LND_RENDER_CLOSE_PROC close) { ((lnd_proc_source *)source)->procs.close = close; }

static int64_t lnd_proc_source_read_pcm(lnd_source *src, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_proc_source *s = (lnd_proc_source *)src;
    size_t width = src->channels * lnd_format_bytes(s->format), done = 0;
    bool direct = pcm->format == s->format && pcm->layout == LND_LAYOUT_INTERLEAVED && lnd_pcm_stride(pcm) == width &&
                  (uintptr_t)lnd_pcm_at(pcm, 0, offset) % lnd_format_bytes(s->format) == 0;
    LND_PCM input = {.data = s->scratch, .frames = s->max_frames, .channels = src->channels, .format = s->format};
    while (done < frames) {
        size_t n = LND_MIN(frames - done, s->max_frames);
        if (src->length_known) n = (size_t)LND_MIN(n, s->procs.length_frames - LND_MIN(src->pos, s->procs.length_frames));
        if (!n) {
            lnd_store(&src->status, LND_SOURCE_EOF);
            break;
        }
        void *data = direct ? lnd_pcm_at(pcm, 0, offset + done) : s->scratch;
        lnd_callback_enter();
        int64_t got = s->procs.read(s->user, data, n);
        lnd_callback_leave();
        if (got == LND_READ_EOF) {
            lnd_store(&src->status, LND_SOURCE_EOF);
            break;
        }
        if (got < 0 || (uint64_t)got > n) {
            int32_t error = got < 0 && got >= LND_ERR_CYCLE ? (int32_t)got : LND_ERR_IO;
            lnd_store(&src->status, error);
            return done ? (int64_t)done : lnd_error(error);
        }
        if (!direct) {
            int32_t result = LND_PcmConvert(pcm, offset + done, &input, 0, (size_t)got);
            if (result != LND_OK) return result;
        }
        lnd_store_relaxed(&src->pos, lnd_load_relaxed(&src->pos) + (uint64_t)got);
        done += (size_t)got;
        if ((size_t)got < n) break;
    }
    return (int64_t)done;
}

static uint64_t lnd_proc_source_read(lnd_source *src, float *dst, uint64_t frames) {
    LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = src->channels, .format = LND_FORMAT_F32};
    int64_t got = lnd_proc_source_read_pcm(src, &pcm, 0, (size_t)frames);
    return got > 0 ? (uint64_t)got : 0;
}

static int32_t lnd_proc_source_seek(lnd_source *src, uint64_t frame) {
    lnd_proc_source *s = (lnd_proc_source *)src;
    if (!s->procs.seek) return LND_ERR_UNSUPPORTED;
    lnd_callback_enter();
    int32_t r = s->procs.seek(s->user, frame);
    lnd_callback_leave();
    if (r == LND_OK) src->pos = frame;
    return r;
}

static uint64_t lnd_proc_source_length(lnd_source *src) { return ((lnd_proc_source *)src)->procs.length_frames; }

static void lnd_proc_source_free(lnd_source *src) {
    lnd_proc_source *s = (lnd_proc_source *)src;
    if (s->procs.close) {
        lnd_callback_enter();
        s->procs.close(s->user);
        lnd_callback_leave();
    }
    lnd_free_aligned(s->scratch);
    lnd_free(s);
}

static const lnd_source_vt lnd_proc_source_vt = {
    .read = lnd_proc_source_read,
    .read_pcm = lnd_proc_source_read_pcm,
    .seek = lnd_proc_source_seek,
    .length = lnd_proc_source_length,
    .free = lnd_proc_source_free,
};

static const lnd_source_vt lnd_proc_source_vt_noseek = {
    .read = lnd_proc_source_read,
    .read_pcm = lnd_proc_source_read_pcm,
    .seek = nullptr,
    .length = lnd_proc_source_length,
    .free = lnd_proc_source_free,
};

lnd_source *lnd_proc_source_create(const LND_SOURCE_PROCS *procs, void *user, int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint32_t max_frames) {
    lnd_proc_source *s = lnd_alloc_zero(sizeof *s);
    if (!s) return nullptr;
    s->base.vt = procs->seek ? &lnd_proc_source_vt : &lnd_proc_source_vt_noseek;
    s->base.channels = channels;
    s->base.sample_rate_hz = sample_rate_hz;
    s->procs = *procs;
    s->base.length_known = procs->length_known || procs->length_frames != 0;
    s->user = user;
    s->format = format;
    s->max_frames = max_frames;
    size_t width = (size_t)channels * lnd_format_bytes(format);
    if (!max_frames || max_frames > SIZE_MAX / width) {
        lnd_free(s);
        return nullptr;
    }
    {
        s->scratch = lnd_alloc_aligned((size_t)max_frames * channels * lnd_format_bytes(format), LND_CACHE_LINE);
        if (!s->scratch) {
            lnd_free(s);
            return nullptr;
        }
    }
    return &s->base;
}
