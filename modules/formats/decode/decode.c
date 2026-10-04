#include "stream.h"
#include "metadata.h"
#if LND_MODULE_METADATA_IO
#include "lindar_metadata_io.h"
#endif
#include "src/pcm.h"
#include "src/alloc.h"
#include "src/error.h"
#include "src/context.h"
#include "src/config.h"
#include "io/io.h"
#include "formats/codecs/registry.h"

#include <string.h>

struct LND_DECODER {
    lnd_decode_tags tags;
    const LND_CODEC *candidates[32];
    uint32_t count;
    const LND_CODEC *codec;
    void *state;
    LND_CODEC_INFO info;
    lnd_io *io;
    uint8_t *data;
    size_t capacity;
    size_t probe_at;
    size_t head;
    size_t count_bytes;
    size_t consume;
    uint64_t fed_bytes;
    uint64_t consumed_bytes;
    uint64_t metadata_revision;
    LND_PCM pending;
    size_t offset;
    void *scratch;
    uint32_t block;
    int32_t status;
    bool end;
    bool buffered;
    bool forced;
    bool allow_buffered;
    uint8_t packet_config[256];
    size_t packet_config_size;
    bool packet_mode;
    char packet_codec[32];
    uint32_t packet_rate;
    uint32_t packet_channels;
    bool packet_elementary;
    uint64_t trim_remaining;
    uint64_t trim_initial;
};

void LND_DecoderFree(LND_DECODER *d) {
    if (!d) return;
    if (d->state) {
        lnd_callback_enter();
        if (d->buffered)
            d->codec->close(d->state);
        else
            d->codec->stream->close(d->state);
        lnd_callback_leave();
    }
    lnd_io_close(d->io);
    lnd_free(d->scratch);
    lnd_free(d->data);
    lnd_decode_tags_clear(&d->tags);
    lnd_free(d);
}

LND_DECODER *LND_DecoderCreate(const LND_DECODER_OPTIONS *options) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    LND_DECODER_OPTIONS o = options ? *options : (LND_DECODER_OPTIONS){.allow_buffered = true};
    if (!o.input_bytes) o.input_bytes = 262144;
    if (!o.block_frames) o.block_frames = 1024;
    if (o.input_bytes < 4096 || (uint64_t)o.input_bytes > INT64_MAX || o.block_frames > 65536) return lnd_error_null(LND_ERR_INVALID_ARG);
    LND_DECODER *d = lnd_alloc_zero(sizeof *d);
    if (!d) return nullptr;
    d->data = lnd_alloc(o.input_bytes);
    if (!d->data) {
        lnd_free(d);
        return nullptr;
    }
    d->capacity = o.input_bytes;
    d->probe_at = 64;
    d->block = o.block_frames;
    d->forced = o.codec_name != nullptr;
    d->allow_buffered = o.allow_buffered;
    d->status = LND_SOURCE_WAITING;
    if (!lnd_context_enter()) {
        LND_DecoderFree(d);
        return lnd_error_null(LND_ERR_BUSY);
    }
    uint32_t count = LND_CodecGetCount();
    for (uint32_t i = 0; i < count && d->count < LND_COUNTOF(d->candidates); i++) {
        const LND_CODEC *c = LND_CodecGet(i);
        if ((!o.codec_name || !strcmp(o.codec_name, c->name)) && (!(c->flags & LND_CODEC_FLAG_SYSTEM) || lnd_cfg_bool(LND_CFG_CODECS_SYSTEM)))
            d->candidates[d->count++] = c;
    }
    lnd_context_unlock();
    if (!d->count) {
        LND_DecoderFree(d);
        return lnd_error_null(LND_ERR_UNSUPPORTED);
    }
    return d;
}

static void lnd_decode_consume(LND_DECODER *d) {
    if (d->consume) {
        d->head += d->consume;
        d->count_bytes -= d->consume;
        d->consumed_bytes += d->consume;
        d->consume = 0;
    }
    d->pending = (LND_PCM){0};
    d->offset = 0;
}

int64_t LND_DecoderFeed(LND_DECODER *d, const void *data, size_t bytes) {
    if (!d || (!data && bytes)) return LND_ERR_INVALID_ARG;
    if (d->end || d->status < 0) return LND_ERR_STATE;
    size_t take = LND_MIN(bytes, d->capacity - d->count_bytes);
    if (take > d->capacity - d->head - d->count_bytes) {
        if (d->pending.frames) return 0;
        memmove(d->data, d->data + d->head, d->count_bytes);
        d->head = 0;
    }
    if (take) memcpy(d->data + d->head + d->count_bytes, data, take);
    d->count_bytes += take;
    d->fed_bytes += take;
    return (int64_t)take;
}

int32_t LND_DecoderEnd(LND_DECODER *d) {
    if (!d) return LND_ERR_INVALID_ARG;
    d->end = true;
    return LND_OK;
}

static int32_t lnd_decode_open(LND_DECODER *d) {
    lnd_io *io = lnd_io_open_memory(d->data + d->head, d->count_bytes);
    if (!io) return LND_ERR_OUT_OF_MEMORY;
    const LND_CODEC *ordered[32];
    int32_t count = lnd_codec_rank(io, d->candidates, d->count, d->forced, nullptr, ordered);
    if (count < 0) {
        lnd_io_close(io);
        return count;
    }
    int32_t result = d->end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
    for (int32_t i = 0; i < count; i++) {
        const LND_CODEC *c = ordered[i];
        lnd_callback_enter();
        bool streaming = c->stream && (!c->stream->probe || c->stream->probe(d->data + d->head, d->count_bytes));
        lnd_callback_leave();
        if (streaming) {
            lnd_callback_enter();
            d->state = c->stream->create();
            lnd_callback_leave();
            if (!d->state) {
                result = LND_ERR_OUT_OF_MEMORY;
                break;
            }
            d->codec = c;
            result = LND_OK;
            break;
        }
        if (!d->allow_buffered) {
            result = LND_ERR_UNSUPPORTED;
            continue;
        }
        if (!d->end) continue;
        LND_IoSeekBytes(io, 0);
        lnd_callback_enter();
        result = c->open(io, &d->info, &d->state);
        lnd_callback_leave();
        if (result != LND_OK) {
            d->state = nullptr;
            if (d->forced || result == LND_ERR_OUT_OF_MEMORY) break;
            continue;
        }
        if (!d->info.channels || d->info.channels > LND_MAX_CHANNELS || !d->info.sample_rate_hz || !LND_PcmGetSampleBytes(d->info.format)) {
            lnd_callback_enter();
            c->close(d->state);
            lnd_callback_leave();
            d->state = nullptr;
            result = LND_ERR_FORMAT;
            break;
        }
        d->codec = c;
        d->buffered = true;
        d->io = io;
        d->scratch = lnd_alloc((size_t)d->block * d->info.channels * LND_PcmGetSampleBytes(d->info.format));
        return d->scratch ? LND_OK : LND_ERR_OUT_OF_MEMORY;
    }
    lnd_io_close(io);
    return result;
}

int32_t LND_DecoderStep(LND_DECODER *d) {
    if (!d) return LND_ERR_INVALID_ARG;
    if (d->status < 0 || d->status == LND_SOURCE_EOF) return d->status;
    if (d->offset < d->pending.frames) return LND_SOURCE_READY;
    lnd_decode_consume(d);
    if (!d->codec) {
        if (!d->end && d->count_bytes < d->probe_at) return d->status = LND_SOURCE_WAITING;
        int32_t r = lnd_decode_open(d);
        if (r == LND_SOURCE_WAITING)
            d->probe_at = d->count_bytes + LND_MIN(d->capacity - d->count_bytes, LND_MIN(LND_MAX(d->count_bytes, (size_t)64), (size_t)4096));
        if (r != LND_OK) return d->status = r == LND_SOURCE_WAITING && d->count_bytes == d->capacity ? LND_ERR_UNSUPPORTED : r;
    }
    if (d->buffered) {
        lnd_callback_enter();
        uint64_t got = d->codec->read(d->state, d->scratch, d->block);
        lnd_callback_leave();
        if (got > d->block) return d->status = LND_ERR_IO;
        d->pending =
            (LND_PCM){.data = d->scratch, .frames = (size_t)got, .channels = d->info.channels, .format = d->info.format, .layout = LND_LAYOUT_INTERLEAVED};
        return d->status = got ? LND_SOURCE_READY : LND_SOURCE_EOF;
    }
    size_t used = 0;
    lnd_callback_enter();
    int32_t r = d->codec->stream->step(d->state, d->data + d->head, d->count_bytes, d->end, &used, &d->pending, &d->info);
    lnd_callback_leave();
    if (r == LND_SOURCE_WAITING && !used && d->count_bytes == d->capacity && !(d->codec->stream->flags & LND_CODEC_STREAM_ASYNC)) r = LND_ERR_UNSUPPORTED;
    if (used > d->count_bytes || (d->pending.frames && (!d->info.sample_rate_hz || !d->info.channels || d->info.channels > LND_MAX_CHANNELS)))
        r = LND_ERR_FORMAT;
    if (r < 0) return d->status = r;
    d->consume = used;
    if (d->pending.frames) return d->status = LND_SOURCE_READY;
    lnd_decode_consume(d);
    return d->status = r;
}

int64_t LND_DecoderReadPcm(LND_DECODER *d, const LND_PCM *pcm, size_t offset, size_t frames) {
    if (!d || !lnd_pcm_writable(pcm, offset, frames) || frames > (size_t)INT64_MAX) return LND_ERR_INVALID_ARG;
    size_t done = 0;
    while (done < frames) {
        int32_t r = LND_DecoderStep(d);
        if (r < 0) return done ? (int64_t)done : r;
        if (r != LND_SOURCE_READY || !d->pending.frames) break;
        if (pcm->channels != d->info.channels) return done ? (int64_t)done : LND_ERR_INVALID_ARG;
        size_t skip = (size_t)LND_MIN(d->trim_remaining, d->pending.frames - d->offset);
        d->trim_remaining -= skip;
        d->offset += skip;
        if (d->offset == d->pending.frames) continue;
        size_t take = LND_MIN(frames - done, d->pending.frames - d->offset);
        r = LND_PcmConvert(pcm, offset + done, &d->pending, d->offset, take);
        if (r != LND_OK) return done ? (int64_t)done : r;
        d->offset += take;
        done += take;
    }
    return (int64_t)done;
}

int32_t LND_DecoderGetInfo(const LND_DECODER *d, LND_CODEC_INFO *info) {
    if (!d || !info) return LND_ERR_INVALID_ARG;
    *info = d->info;
    if (info->length_frames && d->trim_initial) info->length_frames -= LND_MIN(info->length_frames, d->trim_initial);
    bool valid = info->sample_rate_hz && info->channels && info->channels <= LND_MAX_CHANNELS && LND_PcmGetSampleBytes(info->format);
    return valid ? LND_OK : d->status < 0 ? d->status : LND_SOURCE_WAITING;
}

const LND_CODEC *LND_DecoderGetCodec(const LND_DECODER *d) { return d ? d->codec : nullptr; }
int32_t LND_DecoderGetStatus(const LND_DECODER *d) { return d ? d->status : LND_ERR_INVALID_ARG; }
size_t LND_DecoderGetBufferedBytes(const LND_DECODER *d) { return d ? d->count_bytes : 0; }

uint64_t lnd_decoder_fed(const LND_DECODER *d) { return d ? d->fed_bytes : 0; }
uint64_t lnd_decoder_consumed(const LND_DECODER *d) { return d ? d->consumed_bytes : 0; }

void lnd_decoder_reset(LND_DECODER *d) {
    if (!d) return;
    if (d->state) {
        lnd_callback_enter();
        if (d->buffered)
            d->codec->close(d->state);
        else
            d->codec->stream->close(d->state);
        lnd_callback_leave();
    }
    lnd_io_close(d->io);
    lnd_free(d->scratch);
    lnd_decode_tags_clear(&d->tags);
    d->tags = (lnd_decode_tags){0};
    d->codec = nullptr;
    d->state = nullptr;
    d->io = nullptr;
    d->scratch = nullptr;
    d->info = (LND_CODEC_INFO){0};
    d->probe_at = 64;
    d->head = d->count_bytes = d->consume = d->offset = 0;
    d->fed_bytes = d->consumed_bytes = d->metadata_revision = 0;
    d->trim_remaining = d->trim_initial = 0;
    d->pending = (LND_PCM){0};
    d->status = LND_SOURCE_WAITING;
    d->end = d->buffered = d->packet_mode = false;
}

void lnd_decoder_attach(LND_DECODER *d, const LND_CODEC *codec, void *state, const LND_CODEC_INFO *info) {
    lnd_decoder_reset(d);
    d->codec = codec;
    d->state = state;
    d->info = *info;
    d->status = LND_SOURCE_WAITING;
}

int32_t lnd_decoder_configure(LND_DECODER *d, const LND_CODEC_STREAM_CONFIG *config) {
    if (!d || !config || !config->codec || (!config->data && config->bytes) || config->bytes > sizeof d->packet_config ||
        strlen(config->codec) >= sizeof d->packet_codec)
        return LND_ERR_INVALID_ARG;
    if (d->packet_mode && !strcmp(d->packet_codec, config->codec) && d->packet_rate == config->sample_rate_hz && d->packet_channels == config->channels &&
        d->packet_elementary == config->elementary && d->packet_config_size == config->bytes &&
        (!config->bytes || !memcmp(d->packet_config, config->data, config->bytes)))
        return LND_OK;
    if (d->count_bytes || d->pending.frames > d->offset) return LND_ERR_BUSY;
    const LND_CODEC *codec = nullptr;
    for (uint32_t i = 0; i < d->count; i++) {
        const LND_CODEC *candidate = d->candidates[i];
        if (!candidate->stream || !candidate->stream->configure || !LND_CodecSupportsStream(candidate, config->codec)) continue;
        if (!(candidate->flags & LND_CODEC_FLAG_FALLBACK)) {
            codec = candidate;
            break;
        }
        if (!codec) codec = candidate;
    }
    if (!codec) return LND_ERR_UNSUPPORTED;
    lnd_decoder_reset(d);
    d->codec = codec;
    lnd_callback_enter();
    d->state = codec->stream->create();
    int32_t r = d->state ? codec->stream->configure(d->state, config) : LND_ERR_OUT_OF_MEMORY;
    lnd_callback_leave();
    if (r != LND_OK) return d->status = r;
    if (config->bytes) memcpy(d->packet_config, config->data, config->bytes);
    d->packet_config_size = config->bytes;
    d->packet_mode = true;
    strcpy(d->packet_codec, config->codec);
    d->packet_rate = config->sample_rate_hz;
    d->packet_channels = config->channels;
    d->packet_elementary = config->elementary;
    d->trim_remaining = d->trim_initial = config->trim_known ? config->trim_start_frames : 0;
    return LND_OK;
}

bool lnd_decoder_can_spill(const LND_DECODER *d) { return d && !d->codec && !d->state && !d->consumed_bytes && !d->packet_mode && d->allow_buffered; }

int32_t lnd_decoder_spill(LND_DECODER *d, LND_IO *io) {
    if (!io || !lnd_decoder_can_spill(d)) return LND_ERR_STATE;
    if (LND_IoWrite(io, d->data + d->head, d->count_bytes) != d->count_bytes) return LND_ERR_IO;
    lnd_decoder_reset(d);
    return LND_OK;
}

int32_t LND_DecoderTakeIo(LND_DECODER *d, LND_IO *io) {
    if (!d || !io || !LND_IoCanSeek(io) || LND_IoGetSizeBytes(io) == LND_IO_SIZE_UNKNOWN) return LND_ERR_INVALID_ARG;
    if (d->codec || d->state || d->count_bytes || d->pending.frames) return LND_ERR_STATE;
    const LND_CODEC *codec = nullptr;
    LND_CODEC_INFO info;
    void *state = nullptr;
    int32_t r = lnd_codec_open_candidates(io, d->candidates, d->count, d->forced, nullptr, &codec, &info, &state);
    if (r != LND_OK) return r;
    if (!info.sample_rate_hz || !info.channels || info.channels > LND_MAX_CHANNELS || !LND_PcmGetSampleBytes(info.format)) r = LND_ERR_FORMAT;
    void *scratch = r == LND_OK ? lnd_alloc((size_t)d->block * info.channels * LND_PcmGetSampleBytes(info.format)) : nullptr;
    if (r != LND_OK || !scratch) {
        lnd_callback_enter();
        codec->close(state);
        lnd_callback_leave();
        return r == LND_OK ? LND_ERR_OUT_OF_MEMORY : r;
    }
    d->codec = codec;
    d->state = state;
    d->info = info;
    d->io = io;
    d->scratch = scratch;
    d->buffered = d->end = true;
    d->status = LND_SOURCE_READY;
    return LND_OK;
}

int32_t LND_DecoderLoadMetadata(LND_DECODER *d) {
    if (!d) return LND_ERR_INVALID_ARG;
    if (!d->state) return LND_METADATA_ERR_NOT_FOUND;
    if (!d->buffered) {
        d->metadata_revision = 0;
        lnd_callback_enter();
        int32_t result = d->codec->stream->metadata ? d->codec->stream->metadata(d->state, nullptr, &d->metadata_revision) : LND_METADATA_ERR_NOT_FOUND;
        lnd_callback_leave();
        return result;
    }
#if LND_MODULE_METADATA_IO
    if (!d->tags.revision) {
        d->tags.metadata = LND_MetadataCreate(nullptr);
        if (!d->tags.metadata) return LND_ERR_OUT_OF_MEMORY;
        d->tags.status = LND_MetadataReadIo(d->tags.metadata, d->io, LND_METADATA_AUTO);
        d->tags.revision = lnd_tag_next_revision();
    }
#endif
    return lnd_decode_tags_get(&d->tags, nullptr, &d->metadata_revision);
}

int32_t LND_DecoderCopyMetadata(const LND_DECODER *d, LND_METADATA *metadata) {
    if (!d || !metadata) return LND_ERR_INVALID_ARG;
    if (!d->state) return LND_METADATA_ERR_NOT_FOUND;
    if (d->buffered) return lnd_decode_tags_get(&d->tags, metadata, nullptr);
    lnd_callback_enter();
    int32_t result = d->codec->stream->metadata ? d->codec->stream->metadata(d->state, metadata, nullptr) : LND_METADATA_ERR_NOT_FOUND;
    lnd_callback_leave();
    return result;
}

uint64_t LND_DecoderGetMetadataRevision(const LND_DECODER *d) { return d ? d->metadata_revision : 0; }

bool lnd_decoder_supports_stream(const LND_DECODER *decoder, const char *identifiers) {
    return decoder && lnd_codec_stream_supported(decoder->candidates, decoder->count, identifiers);
}
