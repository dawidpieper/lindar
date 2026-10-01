#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_NO_STDIO
#include "minimp3/minimp3_ex.h"

#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_codecs.h"
#include "src/format.h"

#include <string.h>

typedef struct lnd_mp3_state {
    mp3dec_ex_t dec;
    mp3dec_io_t io;
    lnd_io *stream;
    uint32_t channels;
    uint64_t frames;
    uint64_t pos;
} lnd_mp3_state;

static size_t lnd_mp3_read_cb(void *buf, size_t size, void *user) {
    int64_t got = LND_IoRead(user, buf, size);
    return got > 0 ? (size_t)got : 0;
}

static int lnd_mp3_seek_cb(uint64_t position, void *user) { return LND_IoSeekBytes(user, position) == LND_OK ? 0 : -1; }

static bool lnd_mp3_sync(const uint8_t *h) {
    return h[0] == 0xFF && (h[1] & 0xE0) == 0xE0 && (h[1] & 0x18) != 0x08 && (h[1] & 0x06) != 0 && (h[2] >> 4) != 0xF && ((h[2] >> 2) & 3) != 3;
}

static int32_t lnd_mp3_probe(LND_IO *io) {
    uint8_t h[10];
    if (LND_IoRead(io, h, sizeof h) != sizeof h) return 0;
    if (memcmp(h, "ID3", 3) == 0) return 80;
    return lnd_mp3_sync(h) ? 80 : 0;
}

static int32_t lnd_mp3_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    lnd_mp3_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->stream = io;
    s->io.read = lnd_mp3_read_cb;
    s->io.read_data = io;
    s->io.seek = lnd_mp3_seek_cb;
    s->io.seek_data = io;
    LND_IoSeekBytes(io, 0);
    if (mp3dec_ex_open_cb(&s->dec, &s->io, MP3D_SEEK_TO_SAMPLE) != 0 || s->dec.info.channels < 1 || s->dec.info.hz < 1) {
        mp3dec_ex_close(&s->dec);
        lnd_free(s);
        return LND_ERR_FORMAT;
    }
    s->channels = (uint32_t)s->dec.info.channels;
    s->frames = s->dec.samples / s->channels;
    info->format = LND_FORMAT_F32;
    info->channels = s->channels;
    info->sample_rate_hz = (uint32_t)s->dec.info.hz;
    info->length_frames = s->frames;
    info->seekable = true;
    *state = s;
    return LND_OK;
}

static uint64_t lnd_mp3_read(void *state, void *dst, uint64_t frames) {
    lnd_mp3_state *s = state;
    size_t samples = mp3dec_ex_read(&s->dec, dst, (size_t)(frames * s->channels));
    uint64_t got = samples / s->channels;
    s->pos += got;
    return got;
}

static int32_t lnd_mp3_seek(void *state, uint64_t frame) {
    lnd_mp3_state *s = state;
    if (s->frames && frame > s->frames) frame = s->frames;
    if (mp3dec_ex_seek(&s->dec, frame * s->channels) != 0) return LND_ERR_UNSUPPORTED;
    s->pos = frame;
    return LND_OK;
}

static void lnd_mp3_close(void *state) {
    lnd_mp3_state *s = state;
    mp3dec_ex_close(&s->dec);
    lnd_free(s);
}

#if LND_MODULE_DECODE
#include "stream.h"
#endif

const LND_CODEC lnd_codec_mp3 = {
    .name = "mp3",
    .stream_identifiers = "mp4a.69;mp4a.6B;mp3",
    .extensions = "mp3;mp2;mpa;mpga",
    .probe = lnd_mp3_probe,
    .open = lnd_mp3_open,
    .read = lnd_mp3_read,
    .seek = lnd_mp3_seek,
    .close = lnd_mp3_close,
#if LND_MODULE_DECODE
    .stream = &lnd_mp3_stream_ops,
#endif
};
