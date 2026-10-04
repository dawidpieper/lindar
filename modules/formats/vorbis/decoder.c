#include <vorbis/vorbisfile.h>

#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_codecs.h"

#include <string.h>
#include <limits.h>

typedef struct lnd_vorbis_state {
    OggVorbis_File vf;
    lnd_io *io;
    uint32_t channels;
    uint32_t rate;
    int link;
    uint64_t frames;
    uint64_t pos;
} lnd_vorbis_state;

static size_t lnd_vorbis_read_cb(void *ptr, size_t size, size_t nmemb, void *user) {
    if (!size || nmemb > SIZE_MAX / size) return 0;
    int64_t got = LND_IoRead(user, ptr, size * nmemb);
    return got > 0 ? (size_t)got / size : 0;
}

static int lnd_vorbis_seek_cb(void *user, ogg_int64_t offset, int whence) {
    lnd_io *io = user;
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return -1;
    uint64_t origin = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? LND_IoGetPositionBytes(io) : LND_IoGetSizeBytes(io);
    if (origin > INT64_MAX || (offset > 0 && origin > (uint64_t)(INT64_MAX - offset))) return -1;
    int64_t base = (int64_t)origin;
    int64_t target = base + offset;
    if (target < 0) return -1;
    return LND_IoSeekBytes(io, (uint64_t)target) == LND_OK ? 0 : -1;
}

static long lnd_vorbis_tell_cb(void *user) {
    uint64_t pos = LND_IoGetPositionBytes(user);
    return pos <= LONG_MAX ? (long)pos : -1;
}

static const ov_callbacks lnd_vorbis_callbacks = {
    .read_func = lnd_vorbis_read_cb,
    .seek_func = lnd_vorbis_seek_cb,
    .close_func = nullptr,
    .tell_func = lnd_vorbis_tell_cb,
};

#if LND_MODULE_HTTP
#include "formats/ogg/duration.h"

int32_t lnd_vorbis_duration(lnd_io *io, int64_t *duration_us) {
    OggVorbis_File file;
    if (ov_open_callbacks(io, &file, nullptr, 0, lnd_vorbis_callbacks)) return LND_ERR_FORMAT;
    double duration = ov_time_total(&file, -1) * 1000000;
    bool valid = true;
    long links = ov_streams(&file);
    for (long link = 0; valid && link < links; link++) {
        uint64_t end = link + 1 < links ? (uint64_t)file.offsets[link + 1] : io->size;
        valid = lnd_ogg_duration_end(io, end, (int32_t)ov_serialnumber(&file, (int)link));
    }
    ov_clear(&file);
    if (!valid || !(duration >= 0 && duration < (double)INT64_MAX)) return LND_ERR_FORMAT;
    *duration_us = (int64_t)(duration + 0.5);
    return LND_OK;
}
#endif

static bool lnd_ogg_has(const uint8_t *h, size_t n, const char *magic, size_t len) {
    if (n < 28 || memcmp(h, "OggS", 4) != 0) return false;
    for (size_t i = 27; i + len <= n; i++) {
        if (memcmp(h + i, magic, len) == 0) return true;
    }
    return false;
}

static int32_t lnd_vorbis_probe(LND_IO *io) {
    uint8_t h[64];
    int64_t n = LND_IoRead(io, h, sizeof h);
    return lnd_ogg_has(h, n, "\x01vorbis", 7) ? 80 : 0;
}

static int32_t lnd_vorbis_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    lnd_vorbis_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    LND_IoSeekBytes(io, 0);
    if (ov_open_callbacks(io, &s->vf, nullptr, 0, lnd_vorbis_callbacks) < 0) {
        lnd_free(s);
        return LND_ERR_FORMAT;
    }
    vorbis_info *vi = ov_info(&s->vf, -1);
    if (!vi || vi->channels < 1 || vi->channels > LND_MAX_CHANNELS || vi->rate < 1 || (uint64_t)vi->rate > UINT32_MAX) {
        ov_clear(&s->vf);
        lnd_free(s);
        return LND_ERR_FORMAT;
    }
    for (long link = 0; link < ov_streams(&s->vf); link++) {
        vorbis_info *next = ov_info(&s->vf, (int)link);
        if (!next || next->channels != vi->channels || next->rate != vi->rate) {
            ov_clear(&s->vf);
            lnd_free(s);
            return LND_ERR_UNSUPPORTED;
        }
    }
    if (ov_seekable(&s->vf) && ov_pcm_tell(&s->vf) != 0 && ov_pcm_seek(&s->vf, 0) != 0) {
        ov_clear(&s->vf);
        lnd_free(s);
        return LND_ERR_FORMAT;
    }
    s->channels = (uint32_t)vi->channels;
    s->rate = (uint32_t)vi->rate;
    ogg_int64_t total = ov_pcm_total(&s->vf, -1);
    s->frames = total > 0 ? (uint64_t)total : 0;
    info->format = LND_FORMAT_F32;
    info->channels = s->channels;
    info->sample_rate_hz = (uint32_t)vi->rate;
    info->length_frames = s->frames;
    info->seekable = ov_seekable(&s->vf) != 0;
    *state = s;
    return LND_OK;
}

static uint64_t lnd_vorbis_read(void *state, void *dst, uint64_t frames) {
    lnd_vorbis_state *s = state;
    float *out = dst;
    uint32_t ch = s->channels;
    uint64_t total = 0;
    while (total < frames) {
        float **pcm = nullptr;
        int bitstream = 0;
        int want = (int)LND_MIN(frames - total, (uint64_t)4096);
        long n = ov_read_float(&s->vf, &pcm, want, &bitstream);
        if (n == OV_HOLE) continue;
        if (n <= 0) break;
        if (bitstream != s->link) {
            vorbis_info *vi = ov_info(&s->vf, bitstream);
            if (!vi || vi->channels != (int)ch || vi->rate != s->rate) break;
            s->link = bitstream;
        }
        for (long i = 0; i < n; i++) {
            for (uint32_t c = 0; c < ch; c++) out[(total + (uint64_t)i) * ch + c] = pcm[c][i];
        }
        total += (uint64_t)n;
    }
    s->pos += total;
    return total;
}

static int32_t lnd_vorbis_seek(void *state, uint64_t frame) {
    lnd_vorbis_state *s = state;
    if (frame > INT64_MAX) return LND_ERR_INVALID_ARG;
    if (s->frames && frame > s->frames) frame = s->frames;
    if (ov_pcm_seek(&s->vf, (ogg_int64_t)frame) != 0) return LND_ERR_UNSUPPORTED;
    s->pos = frame;
    return LND_OK;
}

static void lnd_vorbis_close(void *state) {
    lnd_vorbis_state *s = state;
    ov_clear(&s->vf);
    lnd_free(s);
}

#if LND_MODULE_DECODE
#include "stream.h"
#endif

const LND_CODEC lnd_codec_vorbis = {
    .name = "vorbis",
    .stream_identifiers = "vorbis",
    .extensions = "ogg;oga;ogv",
    .probe = lnd_vorbis_probe,
    .open = lnd_vorbis_open,
    .read = lnd_vorbis_read,
    .seek = lnd_vorbis_seek,
    .close = lnd_vorbis_close,
#if LND_MODULE_DECODE
    .stream = &lnd_vorbis_stream_ops,
#endif
};
