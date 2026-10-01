#include "source.h"
#include "registry.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/native.h"
#if LND_MODULE_METADATA_IO
#include "lindar_metadata_io.h"
#endif

#include <string.h>

typedef struct lnd_decode_source {
    const LND_CODEC *codec;
    void *state;
    lnd_io *io;
    LND_PCM scratch;
    bool scratch_owned;
} lnd_decode_source;

static void lnd_decode_close(void *user) {
    lnd_decode_source *s = user;
    s->codec->close(s->state);
    lnd_io_close(s->io);
    if (s->scratch_owned) lnd_free(s->scratch.data);
    lnd_free(s);
}

static int32_t lnd_decode_seek(void *user, uint64_t frame) {
    lnd_decode_source *s = user;
    return s->codec->seek(s->state, frame);
}

static int64_t lnd_decode_read(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_decode_source *s = user;
    size_t sample_bytes = LND_PcmGetSampleBytes(s->scratch.format);
    size_t width = sample_bytes * s->scratch.channels;
    size_t alignment = sample_bytes == 3 ? 1 : sample_bytes;
    if (pcm->format == s->scratch.format && pcm->layout == LND_LAYOUT_INTERLEAVED && (!pcm->stride_bytes || pcm->stride_bytes == width) &&
        ((uintptr_t)pcm->data + offset * width) % alignment == 0) {
        uint64_t got = s->codec->read(s->state, (uint8_t *)pcm->data + offset * width, frames);
        return got <= frames ? (int64_t)got : LND_ERR_IO;
    }
    size_t done = 0;
    while (done < frames) {
        size_t count = LND_MIN(frames - done, s->scratch.frames);
        uint64_t got = s->codec->read(s->state, s->scratch.data, count);
        if (got > count) return LND_ERR_IO;
        int32_t r = LND_PcmConvert(pcm, offset + done, &s->scratch, 0, (size_t)got);
        if (r != LND_OK) return r;
        done += (size_t)got;
        if (got < count) break;
    }
    return (int64_t)done;
}

static LND_SOURCE *lnd_decode_open(lnd_io *io, const char *extension, const LND_ENCODED_SOURCE_OPTIONS *options) {
    if (options && lnd_io_window(io, options->offset_bytes, options->length_bytes) != LND_OK) {
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
    const LND_CODEC *codec = nullptr;
    lnd_decode_source *s = lnd_alloc_zero(sizeof *s);
    if (!s) {
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    s->io = io;
    LND_CODEC_INFO info = {0};
    int32_t r = lnd_codec_open(io, extension, options ? options->codec_name : nullptr, &codec, &info, &s->state);
    s->codec = codec;
    if (r != LND_OK) {
        lnd_io_close(io);
        lnd_free(s);
        return lnd_error_null(r);
    }
    size_t width = LND_PcmGetSampleBytes(info.format) * info.channels;
    uint32_t block = options && options->block_frames ? options->block_frames : 256;
    if (!width || !info.sample_rate_hz || !info.channels || info.channels > LND_MAX_CHANNELS)
        r = LND_ERR_FORMAT;
    else if (block > INT32_MAX || block > SIZE_MAX / width)
        r = LND_ERR_INVALID_ARG;
#if !LND_MODULE_PCM_FLOAT
    else if (info.format > LND_FORMAT_S32 && info.format != (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT))
        r = LND_ERR_UNSUPPORTED;
#endif
    s->scratch = (LND_PCM){.format = info.format, .channels = info.channels, .frames = block};
    bool conversion = info.format != (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT) || lnd_cfg_u32(LND_CFG_INTERNAL_LAYOUT) != LND_LAYOUT_INTERLEAVED;
    if (r == LND_OK && conversion) {
        s->scratch_owned = true;
        s->scratch.data = lnd_alloc(width * block);
        if (!s->scratch.data) r = LND_ERR_OUT_OF_MEMORY;
    }
    lnd_native_source *source = nullptr;
    if (r == LND_OK) {
        LND_SOURCE_CONFIG config = {.read = lnd_decode_read,
                                    .seek = info.seekable && codec->seek ? lnd_decode_seek : nullptr,
                                    .close = lnd_decode_close,
                                    .user = s,
                                    .length_frames = info.length_frames,
                                    .length_known = info.length_known,
                                    .channels = info.channels,
                                    .sample_rate_hz = info.sample_rate_hz,
                                    .block_frames = block};
        source = lnd_native_source_create(&config);
        if (!source) r = LND_ErrorGetLast();
    }
    if (!source) {
        lnd_callback_enter();
        lnd_decode_close(s);
        lnd_callback_leave();
        return lnd_error_null(r);
    }
    if (!s->scratch_owned) s->scratch.data = source->stage.data;
    source->codec = codec;
    source->decoded_format = info.format;
    source->length_estimated = info.length_estimated;
    return &source->base;
}

static LND_SOURCE *lnd_source_open_io_raw(lnd_io *io, const char *extension, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options) {
    if (!io) return lnd_error_null(LND_ERR_IO);
    if (flags & ~(uint32_t)(LND_ENCODED_SOURCE_DIRECT | LND_ENCODED_SOURCE_PRELOAD | LND_ENCODED_SOURCE_LIGHTWEIGHT)) {
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
#if LND_MODULE_GRAPH
    if (!(flags & LND_ENCODED_SOURCE_LIGHTWEIGHT)) return lnd_source_open_graph(io, extension, flags, options);
#endif
    if (flags & ~(LND_ENCODED_SOURCE_LIGHTWEIGHT | LND_ENCODED_SOURCE_DIRECT)) {
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_UNSUPPORTED);
    }
    if (!lnd_context_enter()) {
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_BUSY);
    }
    LND_SOURCE *s = nullptr;
    if (lnd_ctx.initialized)
        s = lnd_decode_open(io, extension, options);
    else {
        lnd_io_close(io);
        lnd_error(LND_ERR_STATE);
    }
    lnd_context_unlock();
    return s;
}

static bool lnd_decode_bitrate(uint64_t bytes, uint64_t frames, uint32_t rate, uint64_t *bitrate) {
    if (!bytes || !frames || !rate) return false;
    uint64_t factor = (uint64_t)rate * 8, whole = bytes / frames, part = bytes % frames;
    if (whole > UINT64_MAX / factor) return false;
    whole *= factor;
    uint64_t fraction = 0, remainder = 0;
    if (part <= UINT64_MAX / factor) {
        uint64_t product = part * factor;
        fraction = product / frames;
        remainder = product % frames;
    } else {
        for (uint64_t bit = UINT64_C(1) << 34; bit; bit >>= 1) {
            fraction *= 2;
            if (remainder >= frames - remainder) {
                remainder -= frames - remainder;
                fraction++;
            } else remainder *= 2;
            if (!(factor & bit)) continue;
            if (remainder >= frames - part) {
                remainder -= frames - part;
                fraction++;
            } else remainder += part;
        }
    }
    fraction += remainder >= frames - remainder;
    if (fraction > UINT64_MAX - whole) return false;
    *bitrate = whole + fraction;
    return true;
}

LND_SOURCE *lnd_source_open_io(lnd_io *io, const char *extension, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options) {
    if (!io) return lnd_error_null(LND_ERR_IO);
    uint32_t metadata_flags = options ? options->metadata_flags : 0;
    if (metadata_flags & ~(LND_ENCODED_SOURCE_READ_METADATA | LND_ENCODED_SOURCE_REQUIRE_METADATA)) {
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
#if LND_MODULE_METADATA_IO
    LND_METADATA *metadata = nullptr;
    int32_t status = LND_METADATA_ERR_NOT_FOUND;
    if (metadata_flags) {
        metadata = LND_MetadataCreate(options->metadata_limits);
        if (!metadata)
            status = LND_ERR_OUT_OF_MEMORY;
        else {
            lnd_io view = *io;
            status = lnd_io_window(&view, options->offset_bytes, options->length_bytes);
            if (!status) status = LND_MetadataReadIo(metadata, &view, LND_METADATA_AUTO);
            int32_t restore = io->vt->seek ? io->vt->seek(io->state, io->base + io->pos) : LND_OK;
            if (restore) status = restore;
        }
        if (status) {
            LND_MetadataFree(metadata);
            metadata = nullptr;
        }
        if (status && (metadata_flags & LND_ENCODED_SOURCE_REQUIRE_METADATA)) {
            lnd_io_close(io);
            return lnd_error_null(status);
        }
    }
#else
    if (metadata_flags) {
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_UNSUPPORTED);
    }
#endif
    uint64_t encoded_bytes = options && options->length_bytes ? options->length_bytes : LND_IoGetSizeBytes(io);
    if (encoded_bytes != LND_IO_SIZE_UNKNOWN && options && !options->length_bytes && options->offset_bytes <= encoded_bytes)
        encoded_bytes -= options->offset_bytes;
    LND_SOURCE *source = lnd_source_open_io_raw(io, extension, flags, options);
    if (source && encoded_bytes != LND_IO_SIZE_UNKNOWN) {
        LND_SOURCE_INFO info;
        if (!LND_SourceGetInfo(source, &info))
            source->bitrate_estimated = lnd_decode_bitrate(encoded_bytes, info.length_frames, info.sample_rate_hz, &source->bitrate_bps);
    }
#if LND_MODULE_METADATA_IO
    if (source) {
        source->metadata = metadata;
        source->metadata_status = status;
    } else
        LND_MetadataFree(metadata);
#endif
    return source;
}

LND_SOURCE *LND_SourceCreateEncodedMemory(const void *data, size_t size, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!data || !size) return lnd_error_null(LND_ERR_INVALID_ARG);
    return lnd_source_open_io(lnd_io_open_memory(data, size), nullptr, flags, options);
}

LND_SOURCE *LND_SourceCreateEncodedInput(const LND_IO_INPUT_PROCS *procs, void *user, uint64_t size, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!procs || !procs->read_at || !size) return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_io *io = lnd_io_open_input(procs, user, size);
    if (!io) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    io->borrowed = true;
    LND_SOURCE *source = lnd_source_open_io(lnd_io_ref(io), nullptr, flags, options);
    io->borrowed = source == nullptr;
    lnd_io_close(io);
    return source;
}

const LND_CODEC *LND_SourceGetCodec(const LND_SOURCE *source) {
#if LND_MODULE_GRAPH
    if (source && source->ops) return source->ops->SourceGetCodec(source);
#endif
    return source ? ((const lnd_native_source *)source)->codec : nullptr;
}
