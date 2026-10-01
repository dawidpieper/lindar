#include "source.h"
#include "registry.h"
#include "playback/graph/context.h"
#include "playback/graph/sound.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/error.h"
#include "src/format.h"
#include "src/pcm.h"
#include "utility/log/log.h"
#include "pcm/audio/convert.h"
#include "pcm/buffers/buffer.h"

#include <string.h>

typedef struct lnd_decoder_source {
    lnd_source base;
    const LND_CODEC *codec;
    void *state;
    lnd_io *io;
    int32_t format;
    uint32_t max_frames;
    uint64_t length;
    bool seekable;
    void *scratch;
} lnd_decoder_source;

static uint64_t lnd_decoder_read(lnd_source *src, float *dst, uint64_t frames) {
    lnd_decoder_source *s = (lnd_decoder_source *)src;
    uint32_t ch = src->channels;
    uint64_t total = 0;
    while (total < frames) {
        uint64_t n = LND_MIN(frames - total, (uint64_t)s->max_frames);
        uint64_t got;
        if (s->format == LND_FORMAT_F32) {
            lnd_callback_enter();
            got = s->codec->read(s->state, dst + total * ch, n);
            lnd_callback_leave();
        } else {
            lnd_callback_enter();
            got = s->codec->read(s->state, s->scratch, n);
            lnd_callback_leave();
            if (got > n) {
                lnd_error(LND_ERR_IO);
                break;
            }
            lnd_pcm_to_f32(s->format, s->scratch, dst + total * ch, (size_t)(got * ch));
        }
        if (got > n) {
            lnd_error(LND_ERR_IO);
            break;
        }
        total += got;
        if (got < n) break;
    }
    lnd_store_relaxed(&src->pos, lnd_load_relaxed(&src->pos) + total);
    return total;
}

static int64_t lnd_decoder_read_pcm(lnd_source *src, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_decoder_source *s = (lnd_decoder_source *)src;
    size_t width = src->channels * lnd_format_bytes(s->format), done = 0;
    bool direct = pcm->format == s->format && pcm->layout == LND_LAYOUT_INTERLEAVED && lnd_pcm_stride(pcm) == width &&
                  (uintptr_t)lnd_pcm_at(pcm, 0, offset) % lnd_format_bytes(s->format) == 0;
    LND_PCM input = {.data = s->scratch, .frames = s->max_frames, .channels = src->channels, .format = s->format};
    while (done < frames) {
        size_t n = LND_MIN(frames - done, s->max_frames);
        void *data = direct ? lnd_pcm_at(pcm, 0, offset + done) : s->scratch;
        lnd_callback_enter();
        uint64_t got = s->codec->read(s->state, data, n);
        lnd_callback_leave();
        if (got > n) return lnd_error(LND_ERR_IO);
        if (!direct) {
            int32_t result = LND_PcmConvert(pcm, offset + done, &input, 0, (size_t)got);
            if (result != LND_OK) return result;
        }
        lnd_store_relaxed(&src->pos, lnd_load_relaxed(&src->pos) + got);
        done += (size_t)got;
        if (got < n) break;
    }
    return (int64_t)done;
}

static int32_t lnd_decoder_seek(lnd_source *src, uint64_t frame) {
    lnd_decoder_source *s = (lnd_decoder_source *)src;
    if (!s->seekable || !s->codec->seek) return LND_ERR_UNSUPPORTED;
    if (!src->length_estimated && s->length && frame > s->length) frame = s->length;
    lnd_callback_enter();
    int32_t r = s->codec->seek(s->state, frame);
    lnd_callback_leave();
    if (r == LND_OK) src->pos = frame;
    return r;
}

static uint64_t lnd_decoder_length(lnd_source *src) { return ((lnd_decoder_source *)src)->length; }

static void lnd_decoder_free(lnd_source *src) {
    lnd_decoder_source *s = (lnd_decoder_source *)src;
    if (s->state) {
        lnd_callback_enter();
        s->codec->close(s->state);
        lnd_callback_leave();
    }
    lnd_io_close(s->io);
    lnd_free_aligned(s->scratch);
    lnd_free(s);
}

static const lnd_source_vt lnd_decoder_vt = {
    .read = lnd_decoder_read,
    .read_pcm = lnd_decoder_read_pcm,
    .seek = lnd_decoder_seek,
    .length = lnd_decoder_length,
    .free = lnd_decoder_free,
};

static const lnd_source_vt lnd_decoder_vt_noseek = {
    .read = lnd_decoder_read,
    .read_pcm = lnd_decoder_read_pcm,
    .seek = nullptr,
    .length = lnd_decoder_length,
    .free = lnd_decoder_free,
};

static lnd_source *lnd_decoder_source_create(const LND_CODEC *codec, void *state, lnd_io *io, const LND_CODEC_INFO *info, uint32_t max_frames) {
    lnd_decoder_source *s = lnd_alloc_zero(sizeof *s);
    if (!s) return nullptr;
    s->base.vt = info->seekable && codec->seek ? &lnd_decoder_vt : &lnd_decoder_vt_noseek;
    s->base.channels = info->channels;
    s->base.sample_rate_hz = info->sample_rate_hz;
    s->base.length_known = info->length_known || info->length_frames != 0;
    s->base.length_estimated = info->length_estimated;
    s->codec = codec;
    s->state = state;
    s->io = io;
    s->format = info->format;
    s->max_frames = max_frames;
    s->length = info->length_frames;
    s->seekable = info->seekable;
    size_t width = (size_t)info->channels * lnd_format_bytes(info->format);
    if (!max_frames || max_frames > SIZE_MAX / width) {
        lnd_free(s);
        return nullptr;
    }
    {
        s->scratch = lnd_alloc_aligned((size_t)max_frames * info->channels * lnd_format_bytes(info->format), LND_CACHE_LINE);
        if (!s->scratch) {
            lnd_free(s);
            return nullptr;
        }
    }
    return &s->base;
}

static lnd_source *lnd_source_preload(const LND_CODEC *codec, void *state, lnd_io *io, const LND_CODEC_INFO *info, int32_t *error) {
    size_t frame_bytes = (size_t)info->channels * lnd_format_bytes(info->format);
    uint64_t limit = SIZE_MAX / frame_bytes;
    uint64_t cap = info->length_frames && !info->length_estimated ? info->length_frames : LND_MIN(limit, 65536);
    uint8_t *data = cap && cap <= limit ? lnd_alloc((size_t)cap * frame_bytes) : nullptr;
    uint64_t total = 0;
    *error = data ? LND_OK : LND_ERR_OUT_OF_MEMORY;
    while (*error == LND_OK) {
        if (total == cap) {
            if (info->length_frames && !info->length_estimated) break;
            if (cap > limit / 2) {
                *error = LND_ERR_OUT_OF_MEMORY;
                break;
            }
            cap *= 2;
            uint8_t *grown = lnd_realloc(data, (size_t)cap * frame_bytes);
            if (!grown) {
                *error = LND_ERR_OUT_OF_MEMORY;
                break;
            }
            data = grown;
        }
        uint64_t requested = cap - total;
        lnd_callback_enter();
        uint64_t got = codec->read(state, data + (size_t)total * frame_bytes, requested);
        lnd_callback_leave();
        if (got > requested) {
            *error = LND_ERR_IO;
            break;
        }
        if (!got) break;
        total += got;
    }
    lnd_callback_enter();
    codec->close(state);
    lnd_callback_leave();
    lnd_io_close(io);
    lnd_source *source = nullptr;
    if (*error == LND_OK && total) {
        lnd_buffer *b = lnd_buffer_new(info->format, info->channels, info->sample_rate_hz, total);
        if (b) {
            memcpy(b->data, data, (size_t)total * frame_bytes);
            source = lnd_buffer_source_create(b);
            lnd_buffer_unref(b);
        }
        if (!source) *error = LND_ERR_OUT_OF_MEMORY;
    } else if (*error == LND_OK) {
        *error = LND_ERR_FORMAT;
    }
    lnd_free(data);
    return source;
}

static lnd_graph_source *lnd_source_open(lnd_io *io, const char *extension, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options) {
    if (!io) return lnd_error_null(LND_ERR_IO);
    if (options && (options->offset_bytes || options->length_bytes) && lnd_io_window(io, options->offset_bytes, options->length_bytes) != LND_OK) {
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
    const LND_CODEC *codec = nullptr;
    LND_CODEC_INFO info = {0};
    void *state = nullptr;
    int32_t r = lnd_codec_open(io, extension, options ? options->codec_name : nullptr, &codec, &info, &state);
    if (r != LND_OK) {
        lnd_io_close(io);
        return lnd_error_null(r);
    }
    if (!lnd_format_valid(info.format) || info.channels < 1 || info.channels > LND_MAX_CHANNELS || info.sample_rate_hz < 1) {
        lnd_callback_enter();
        codec->close(state);
        lnd_callback_leave();
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_FORMAT);
    }
    lnd_source *source;
    if (flags & LND_ENCODED_SOURCE_PRELOAD) {
        source = lnd_source_preload(codec, state, io, &info, &r);
        if (!source) return lnd_error_null(r);
    } else {
        uint32_t chunk = lnd_cfg_u32(LND_CFG_GRAPH_BUFFER_FRAMES);
        uint32_t block = lnd_cfg_u32(LND_CFG_GRAPH_MIX_BLOCK_FRAMES);
        source = lnd_decoder_source_create(codec, state, io, &info, LND_MAX(chunk, block));
        if (!source) {
            lnd_callback_enter();
            codec->close(state);
            lnd_callback_leave();
            lnd_io_close(io);
        }
        source = lnd_source_stream_wrap(source, flags & LND_ENCODED_SOURCE_DIRECT ? LND_GRAPH_SOURCE_DIRECT : 0);
    }
    lnd_graph_source *s = lnd_source_obj_create(source, flags, info.format);
    if (s) {
        s->codec = codec;
        LND_LOG_D("source opened: codec %s, %u Hz, %u ch, %s, %llu frames", codec->name, info.sample_rate_hz, info.channels, lnd_format_name(info.format),
                  (unsigned long long)info.length_frames);
    }
    return s;
}

LND_SOURCE *lnd_source_open_graph(lnd_io *io, const char *extension, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) {
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_BUSY);
    }
    lnd_context_gc();
    if (!lnd_ctx.initialized) {
        lnd_io_close(io);
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_STATE);
    }
    lnd_graph_source *s = lnd_source_open(io, extension, flags, options);
    lnd_context_unlock();
    return (LND_SOURCE *)s;
}
