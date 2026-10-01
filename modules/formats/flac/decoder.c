#include <FLAC/stream_decoder.h>

#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_codecs.h"
#include "src/format.h"

#include <string.h>

typedef struct lnd_flac_state {
    FLAC__StreamDecoder *dec;
    lnd_io *io;
    uint32_t channels;
    uint32_t sample_rate_hz;
    uint32_t bits;
    int32_t format;
    uint32_t frame_bytes;
    uint64_t total;
    uint64_t pos;
    uint64_t skip_until;
    uint8_t *pending;
    size_t pending_cap;
    size_t pending_frames;
    size_t pending_offset;
    bool eof;
    bool failed;
} lnd_flac_state;

static FLAC__StreamDecoderReadStatus lnd_flac_read_cb(const FLAC__StreamDecoder *dec, FLAC__byte buffer[], size_t *bytes, void *client) {
    LND_UNUSED(dec);
    lnd_flac_state *s = client;
    int64_t n = LND_IoRead(s->io, buffer, *bytes);
    *bytes = n > 0 ? (size_t)n : 0;
    if (n < 0) return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    return n ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE : FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
}

static FLAC__StreamDecoderSeekStatus lnd_flac_seek_cb(const FLAC__StreamDecoder *dec, FLAC__uint64 offset, void *client) {
    LND_UNUSED(dec);
    lnd_flac_state *s = client;
    return LND_IoSeekBytes(s->io, offset) == LND_OK ? FLAC__STREAM_DECODER_SEEK_STATUS_OK : FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
}

static FLAC__StreamDecoderTellStatus lnd_flac_tell_cb(const FLAC__StreamDecoder *dec, FLAC__uint64 *offset, void *client) {
    LND_UNUSED(dec);
    lnd_flac_state *s = client;
    *offset = LND_IoGetPositionBytes(s->io);
    return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__StreamDecoderLengthStatus lnd_flac_length_cb(const FLAC__StreamDecoder *dec, FLAC__uint64 *length, void *client) {
    LND_UNUSED(dec);
    lnd_flac_state *s = client;
    *length = LND_IoGetSizeBytes(s->io);
    return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool lnd_flac_eof_cb(const FLAC__StreamDecoder *dec, void *client) {
    LND_UNUSED(dec);
    lnd_flac_state *s = client;
    return LND_IoGetPositionBytes(s->io) >= LND_IoGetSizeBytes(s->io);
}

static FLAC__StreamDecoderWriteStatus lnd_flac_write_cb(const FLAC__StreamDecoder *dec, const FLAC__Frame *frame, const FLAC__int32 *const buffer[],
                                                        void *client) {
    LND_UNUSED(dec);
    lnd_flac_state *s = client;
    uint32_t ch = frame->header.channels;
    uint32_t bits = frame->header.bits_per_sample;
    size_t frames = frame->header.blocksize;
    if (ch != s->channels || bits != s->bits || frame->header.sample_rate != s->sample_rate_hz || !frames || frames > SIZE_MAX / s->frame_bytes) {
        s->failed = true;
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    size_t skip = 0;
    if (s->skip_until) {
        uint64_t first = frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER ? frame->header.number.sample_number
                                                                                            : (uint64_t)frame->header.number.frame_number * frames;
        if (first < s->skip_until) skip = (size_t)LND_MIN((uint64_t)frames, s->skip_until - first);
        s->skip_until = 0;
    }
    size_t keep = frames - skip;
    size_t bytes = keep * s->frame_bytes;
    if (bytes > s->pending_cap) {
        uint8_t *grown = lnd_realloc(s->pending, bytes);
        if (!grown) {
            s->failed = true;
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }
        s->pending = grown;
        s->pending_cap = bytes;
    }
    uint32_t sample_bytes = lnd_format_bytes(s->format);
    uint32_t shift = sample_bytes * 8 - bits;
    uint8_t *out = s->pending;
    for (size_t i = skip; i < frames; i++) {
        for (uint32_t c = 0; c < ch; c++) {
            int32_t v = (int32_t)((uint32_t)buffer[c][i] << shift);
            if (sample_bytes == 2) {
                out[0] = (uint8_t)v;
                out[1] = (uint8_t)(v >> 8);
            } else if (sample_bytes == 3) {
                out[0] = (uint8_t)v;
                out[1] = (uint8_t)(v >> 8);
                out[2] = (uint8_t)(v >> 16);
            } else {
                out[0] = (uint8_t)v;
                out[1] = (uint8_t)(v >> 8);
                out[2] = (uint8_t)(v >> 16);
                out[3] = (uint8_t)(v >> 24);
            }
            out += sample_bytes;
        }
    }
    s->pending_frames = keep;
    s->pending_offset = 0;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void lnd_flac_metadata_cb(const FLAC__StreamDecoder *dec, const FLAC__StreamMetadata *metadata, void *client) {
    LND_UNUSED(dec);
    lnd_flac_state *s = client;
    if (metadata->type != FLAC__METADATA_TYPE_STREAMINFO) return;
    s->channels = metadata->data.stream_info.channels;
    s->sample_rate_hz = metadata->data.stream_info.sample_rate;
    s->bits = metadata->data.stream_info.bits_per_sample;
    s->total = metadata->data.stream_info.total_samples;
}

static void lnd_flac_error_cb(const FLAC__StreamDecoder *dec, FLAC__StreamDecoderErrorStatus status, void *client) {
    LND_UNUSED(dec);
    LND_UNUSED(status);
    LND_UNUSED(client);
}

static int32_t lnd_flac_probe(LND_IO *io) {
    uint8_t h[4];
    return LND_IoRead(io, h, 4) == 4 && memcmp(h, "fLaC", 4) == 0 ? 80 : 0;
}

static void lnd_flac_close(void *state);

static int32_t lnd_flac_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    lnd_flac_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    LND_IoSeekBytes(io, 0);
    s->dec = FLAC__stream_decoder_new();
    if (!s->dec) {
        lnd_free(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    FLAC__StreamDecoderInitStatus st = FLAC__stream_decoder_init_stream(s->dec, lnd_flac_read_cb, lnd_flac_seek_cb, lnd_flac_tell_cb, lnd_flac_length_cb,
                                                                        lnd_flac_eof_cb, lnd_flac_write_cb, lnd_flac_metadata_cb, lnd_flac_error_cb, s);
    if (st != FLAC__STREAM_DECODER_INIT_STATUS_OK || !FLAC__stream_decoder_process_until_end_of_metadata(s->dec) || !s->sample_rate_hz || s->channels < 1 ||
        s->channels > LND_MAX_CHANNELS || s->bits < 4 || s->bits > 32) {
        lnd_flac_close(s);
        return LND_ERR_FORMAT;
    }
    s->format = s->bits <= 16 ? LND_FORMAT_S16 : (s->bits <= 24 ? LND_FORMAT_S24 : LND_FORMAT_S32);
    s->frame_bytes = s->channels * lnd_format_bytes(s->format);
    info->format = s->format;
    info->channels = s->channels;
    info->sample_rate_hz = s->sample_rate_hz;
    info->length_frames = s->total;
    info->seekable = true;
    *state = s;
    return LND_OK;
}

static bool lnd_flac_fill(lnd_flac_state *s) {
    while (!s->eof && !s->failed) {
        if (!FLAC__stream_decoder_process_single(s->dec)) {
            s->failed = true;
            return false;
        }
        if (FLAC__stream_decoder_get_state(s->dec) == FLAC__STREAM_DECODER_END_OF_STREAM) s->eof = true;
        if (s->pending_frames > s->pending_offset) return true;
    }
    return false;
}

static uint64_t lnd_flac_read(void *state, void *dst, uint64_t frames) {
    lnd_flac_state *s = state;
    uint8_t *out = dst;
    uint64_t total = 0;
    while (total < frames) {
        if (s->pending_offset >= s->pending_frames && !lnd_flac_fill(s)) break;
        size_t avail = s->pending_frames - s->pending_offset;
        size_t n = (size_t)LND_MIN((uint64_t)avail, frames - total);
        memcpy(out + total * s->frame_bytes, s->pending + s->pending_offset * s->frame_bytes, n * s->frame_bytes);
        s->pending_offset += n;
        total += n;
    }
    s->pos += total;
    return total;
}

static int32_t lnd_flac_seek(void *state, uint64_t frame) {
    lnd_flac_state *s = state;
    if (s->total && frame > s->total) frame = s->total;
    s->pending_frames = s->pending_offset = 0;
    s->eof = false;
    s->failed = false;
    s->skip_until = frame;
    if (!FLAC__stream_decoder_seek_absolute(s->dec, frame)) {
        FLAC__stream_decoder_flush(s->dec);
        s->skip_until = 0;
        return LND_ERR_UNSUPPORTED;
    }
    s->pos = frame;
    return LND_OK;
}

static void lnd_flac_close(void *state) {
    lnd_flac_state *s = state;
    if (s->dec) {
        FLAC__stream_decoder_finish(s->dec);
        FLAC__stream_decoder_delete(s->dec);
    }
    lnd_free(s->pending);
    lnd_free(s);
}

#if LND_MODULE_DECODE
#include "stream.h"
#endif

const LND_CODEC lnd_codec_flac = {
#if LND_MODULE_DECODE
    .stream = &lnd_flac_stream_ops,
#endif
    .name = "flac",
    .stream_identifiers = "flac;fLaC",
    .extensions = "flac",
    .probe = lnd_flac_probe,
    .open = lnd_flac_open,
    .read = lnd_flac_read,
    .seek = lnd_flac_seek,
    .close = lnd_flac_close,
};
