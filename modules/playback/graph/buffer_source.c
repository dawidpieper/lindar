#include "src/alloc.h"
#include "pcm/audio/convert.h"
#include "pcm/buffers/buffer.h"
#include "pcm/audio/source.h"

typedef struct lnd_buffer_source {
    lnd_source base;
    lnd_buffer *buffer;
} lnd_buffer_source;

static uint64_t lnd_buffer_source_read(lnd_source *src, float *dst, uint64_t frames) {
    lnd_buffer_source *s = (lnd_buffer_source *)src;
    lnd_buffer *b = s->buffer;
    uint64_t pos = lnd_load_relaxed(&src->pos);
    if (pos >= b->frames) return 0;
    uint64_t n = LND_MIN(frames, b->frames - pos);
    const uint8_t *p = (const uint8_t *)b->data + pos * lnd_buffer_frame_bytes(b);
    lnd_pcm_to_f32(b->format, p, dst, (size_t)(n * b->channels));
    lnd_store_relaxed(&src->pos, pos + n);
    return n;
}

static int64_t lnd_buffer_source_read_pcm(lnd_source *src, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_buffer *b = ((lnd_buffer_source *)src)->buffer;
    uint64_t pos = lnd_load_relaxed(&src->pos);
    if (pos >= b->frames) return 0;
    size_t n = (size_t)LND_MIN(frames, b->frames - pos);
    LND_PCM input = {.data = b->data, .frames = (size_t)b->frames, .channels = b->channels, .format = b->format};
    int32_t result = LND_PcmConvert(pcm, offset, &input, (size_t)pos, n);
    if (result != LND_OK) return result;
    lnd_store_relaxed(&src->pos, pos + n);
    return (int64_t)n;
}

static int32_t lnd_buffer_source_seek(lnd_source *src, uint64_t frame) {
    lnd_buffer_source *s = (lnd_buffer_source *)src;
    src->pos = LND_MIN(frame, s->buffer->frames);
    return LND_OK;
}

static uint64_t lnd_buffer_source_length(lnd_source *src) { return ((lnd_buffer_source *)src)->buffer->frames; }

static void lnd_buffer_source_free(lnd_source *src) {
    lnd_buffer_source *s = (lnd_buffer_source *)src;
    lnd_buffer_unref(s->buffer);
    lnd_free(s);
}

static const lnd_source_vt lnd_buffer_source_vt = {
    .read = lnd_buffer_source_read,
    .read_pcm = lnd_buffer_source_read_pcm,
    .seek = lnd_buffer_source_seek,
    .length = lnd_buffer_source_length,
    .free = lnd_buffer_source_free,
};

lnd_source *lnd_buffer_source_create(LND_BUFFER *b) {
    lnd_buffer_source *s = lnd_alloc_zero(sizeof *s);
    if (!s) return nullptr;
    s->base.vt = &lnd_buffer_source_vt;
    s->base.channels = b->channels;
    s->base.sample_rate_hz = b->sample_rate_hz;
    s->base.length_known = true;
    s->buffer = b;
    lnd_buffer_ref(b);
    return &s->base;
}
