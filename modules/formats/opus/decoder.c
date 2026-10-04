#include <opusfile.h>

#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_codecs.h"

#include <string.h>

typedef struct lnd_opus_state {
    OggOpusFile *of;
    lnd_io *io;
    uint32_t channels;
    uint64_t frames;
    uint64_t pos;
} lnd_opus_state;

static int lnd_opus_read_cb(void *user, unsigned char *buf, int nbytes) { return nbytes > 0 ? (int)LND_IoRead(user, buf, (size_t)nbytes) : 0; }

static int lnd_opus_seek_cb(void *user, opus_int64 offset, int whence) {
    lnd_io *io = user;
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return -1;
    uint64_t origin = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? LND_IoGetPositionBytes(io) : LND_IoGetSizeBytes(io);
    if (origin > INT64_MAX || (offset > 0 && origin > (uint64_t)(INT64_MAX - offset))) return -1;
    int64_t base = (int64_t)origin;
    int64_t target = base + offset;
    if (target < 0) return -1;
    return LND_IoSeekBytes(io, (uint64_t)target) == LND_OK ? 0 : -1;
}

static opus_int64 lnd_opus_tell_cb(void *user) { return (opus_int64)LND_IoGetPositionBytes(user); }

static const OpusFileCallbacks lnd_opus_callbacks = {
    .read = lnd_opus_read_cb,
    .seek = lnd_opus_seek_cb,
    .tell = lnd_opus_tell_cb,
    .close = nullptr,
};

#if LND_MODULE_HTTP
#include "formats/ogg/duration.h"
#include "reader.h"

OggOpusFile *lnd_opus_open_io(lnd_io *io) { return op_open_callbacks(io, &lnd_opus_callbacks, nullptr, 0, nullptr); }

int32_t lnd_opus_io_duration(lnd_io *io, OggOpusFile *file, int64_t *duration_us) {
    ogg_int64_t frames = op_pcm_total(file, -1);
    uint64_t end = 0;
    bool valid = true;
    for (int link = 0; valid && link < op_link_count(file); link++) {
        opus_int64 bytes = op_raw_total(file, link);
        valid = bytes > 0 && (uint64_t)bytes <= io->size - end;
        if (valid) {
            end += (uint64_t)bytes;
            valid = lnd_ogg_duration_end(io, end, (int32_t)op_serialno(file, link));
        }
    }
    uint64_t fraction = frames >= 0 ? (uint64_t)(frames % 48000) * 1000000 / 48000 : 0;
    if (!valid || end != io->size || frames < 0 || (uint64_t)(frames / 48000) > ((uint64_t)INT64_MAX - fraction) / 1000000) return LND_ERR_FORMAT;
    *duration_us = frames / 48000 * 1000000 + (int64_t)fraction;
    return LND_OK;
}

int32_t lnd_opus_duration(lnd_io *io, int64_t *duration_us) {
    OggOpusFile *file = lnd_opus_open_io(io);
    if (!file) return LND_ERR_FORMAT;
    int32_t result = lnd_opus_io_duration(io, file, duration_us);
    op_free(file);
    return result;
}
#endif

static int32_t lnd_opus_probe(LND_IO *io) {
    uint8_t h[64];
    int64_t n = LND_IoRead(io, h, sizeof h);
    if (n < 28 || memcmp(h, "OggS", 4) != 0) return 0;
    for (size_t i = 27; i + 8 <= (uint64_t)n; i++) {
        if (memcmp(h + i, "OpusHead", 8) == 0) return 80;
    }
    return 0;
}

static int32_t lnd_opus_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    lnd_opus_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    LND_IoSeekBytes(io, 0);
    int err = 0;
    s->of = op_open_callbacks(io, &lnd_opus_callbacks, nullptr, 0, &err);
    if (!s->of) {
        lnd_free(s);
        return LND_ERR_FORMAT;
    }
    int channels = op_channel_count(s->of, -1);
    if (channels < 1 || channels > LND_MAX_CHANNELS) {
        op_free(s->of);
        lnd_free(s);
        return LND_ERR_FORMAT;
    }
    for (int link = 0; link < op_link_count(s->of); link++) {
        if (op_channel_count(s->of, link) != channels) {
            op_free(s->of);
            lnd_free(s);
            return LND_ERR_UNSUPPORTED;
        }
    }
    s->channels = (uint32_t)channels;
    ogg_int64_t total = op_pcm_total(s->of, -1);
    s->frames = total > 0 ? (uint64_t)total : 0;
#if LND_OPUS_INTEGER
    info->format = LND_FORMAT_S16;
#else
    info->format = LND_FORMAT_F32;
#endif
    info->channels = s->channels;
    info->sample_rate_hz = 48000;
    info->length_frames = s->frames;
    info->seekable = op_seekable(s->of) != 0;
    *state = s;
    return LND_OK;
}

static uint64_t lnd_opus_read(void *state, void *dst, uint64_t frames) {
    lnd_opus_state *s = state;
#if LND_OPUS_INTEGER
    opus_int16 *out = dst;
#else
    float *out = dst;
#endif
    uint32_t ch = s->channels;
    uint64_t total = 0;
    while (total < frames) {
        int want = (int)LND_MIN(frames - total, (uint64_t)5760);
#if LND_OPUS_INTEGER
        int n = op_read(s->of, out + total * ch, want * (int)ch, nullptr);
#else
        int n = op_read_float(s->of, out + total * ch, want * (int)ch, nullptr);
#endif
        if (n == OP_HOLE) continue;
        if (n <= 0) break;
        total += (uint64_t)n;
    }
    s->pos += total;
    return total;
}

static int32_t lnd_opus_seek(void *state, uint64_t frame) {
    lnd_opus_state *s = state;
    if (frame > INT64_MAX) return LND_ERR_INVALID_ARG;
    if (s->frames && frame > s->frames) frame = s->frames;
    if (op_pcm_seek(s->of, (ogg_int64_t)frame) != 0) return LND_ERR_UNSUPPORTED;
    s->pos = frame;
    return LND_OK;
}

static void lnd_opus_close(void *state) {
    lnd_opus_state *s = state;
    op_free(s->of);
    lnd_free(s);
}

#if LND_MODULE_DECODE
#include "stream.h"
#endif

const LND_CODEC lnd_codec_opus = {
    .name = "opus",
    .stream_identifiers = "opus",
    .extensions = "opus;ogg;oga",
    .probe = lnd_opus_probe,
    .open = lnd_opus_open,
    .read = lnd_opus_read,
    .seek = lnd_opus_seek,
    .close = lnd_opus_close,
#if LND_MODULE_DECODE
    .stream = &lnd_opus_stream_ops,
#endif
};
