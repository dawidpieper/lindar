#include "io/reader.h"
#include "formats/riff/read.h"
#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_codecs.h"
#include "src/format.h"

#include <string.h>

typedef struct lnd_wav_state {
    lnd_io *io;
    uint64_t data_offset;
    uint32_t block;
    uint64_t frames;
    uint64_t pos;
} lnd_wav_state;

#define lnd_rd_u32 lnd_rd_u32le
#define lnd_rd_u16 lnd_rd_u16le
#define lnd_rd_u64 lnd_rd_u64le

static int32_t lnd_wav_probe(LND_IO *io) {
    uint8_t h[12];
    if (LND_IoRead(io, h, 12) != 12) return 0;
    bool riff = memcmp(h, "RIFF", 4) == 0 || memcmp(h, "RF64", 4) == 0 || memcmp(h, "BW64", 4) == 0;
    return riff && memcmp(h + 8, "WAVE", 4) == 0 ? 100 : 0;
}

static int32_t lnd_wav_format(uint16_t tag, uint16_t bits, const uint8_t *subformat) {
    bool is_float = tag == 3 || (tag == 0xFFFE && subformat && lnd_rd_u16(subformat) == 3);
    bool is_pcm = tag == 1 || (tag == 0xFFFE && subformat && lnd_rd_u16(subformat) == 1);
    if (is_float) return bits == 32 ? LND_FORMAT_F32 : (bits == 64 ? LND_FORMAT_F64 : LND_FORMAT_NONE);
    if (!is_pcm) return LND_FORMAT_NONE;
    switch (bits) {
    case 8:
        return LND_FORMAT_U8;
    case 16:
        return LND_FORMAT_S16;
    case 24:
        return LND_FORMAT_S24;
    case 32:
        return LND_FORMAT_S32;
    default:
        return LND_FORMAT_NONE;
    }
}

static int32_t lnd_wav_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    uint8_t h[12];
    if (LND_IoRead(io, h, 12) != 12 || memcmp(h + 8, "WAVE", 4) != 0) return LND_ERR_FORMAT;
    lnd_riff riff;
    int32_t result = lnd_riff_open(h, LND_IoGetSizeBytes(io), LND_RIFF_PHYSICAL | LND_RIFF_CLAMP_DATA | LND_RIFF_ALLOW_MISSING_PAD, &riff);
    if (result != LND_OK) return result;
    uint64_t pos = 12;
    int32_t format = LND_FORMAT_NONE;
    uint32_t channels = 0, sample_rate_hz = 0, block = 0;
    uint64_t data_offset = 0, data_bytes = 0;
    bool have_fmt = false, have_data = false;
    while (pos < riff.end && !(have_fmt && have_data)) {
        uint8_t ch[8];
        if (LND_IoSeekBytes(io, pos) != LND_OK || LND_IoRead(io, ch, 8) != 8) return LND_ERR_FORMAT;
        lnd_riff_chunk chunk;
        result = lnd_riff_next(&riff, ch, &pos, &chunk);
        if (result != LND_OK) return result;
        uint64_t csize = chunk.size, body = chunk.body;
        if (memcmp(ch, "ds64", 4) == 0 && riff.rf64) {
            uint8_t data[28];
            if (csize < sizeof data || LND_IoRead(io, data, sizeof data) != sizeof data) return LND_ERR_FORMAT;
            result = lnd_riff_ds64(&riff, data, sizeof data, csize);
            if (result != LND_OK) return result;
        } else if (memcmp(ch, "fmt ", 4) == 0) {
            if (have_fmt || csize < 16) return LND_ERR_FORMAT;
            uint8_t f[40];
            size_t n = (size_t)LND_MIN(csize, (uint64_t)sizeof f);
            if (LND_IoRead(io, f, n) != (int64_t)n) return LND_ERR_FORMAT;
            uint16_t tag = lnd_rd_u16(f);
            channels = lnd_rd_u16(f + 2);
            sample_rate_hz = lnd_rd_u32(f + 4);
            block = lnd_rd_u16(f + 12);
            uint16_t bits = lnd_rd_u16(f + 14);
            const uint8_t *sub = tag == 0xFFFE && n >= 40 ? f + 24 : nullptr;
            if (tag == 0xFFFE && !sub) return LND_ERR_FORMAT;
            format = lnd_wav_format(tag, bits, sub);
            if (format == LND_FORMAT_NONE || channels < 1 || channels > LND_MAX_CHANNELS || sample_rate_hz < 1) return LND_ERR_FORMAT;
            uint32_t computed = channels * lnd_format_bytes(format);
            if (block != computed) block = computed;
            have_fmt = true;
        } else if (memcmp(ch, "data", 4) == 0 && !have_data) {
            data_offset = body;
            data_bytes = csize;
            have_data = true;
        }
    }
    if (!have_fmt || !have_data) return LND_ERR_FORMAT;
    lnd_wav_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->data_offset = data_offset;
    s->block = block;
    s->frames = data_bytes / block;
    LND_IoSeekBytes(io, data_offset);
    info->format = format;
    info->channels = channels;
    info->sample_rate_hz = sample_rate_hz;
    info->length_frames = s->frames;
    info->length_known = true;
    info->seekable = true;
    *state = s;
    return LND_OK;
}

static uint64_t lnd_wav_read(void *state, void *dst, uint64_t frames) {
    lnd_wav_state *s = state;
    if (s->pos >= s->frames) return 0;
    uint64_t n = LND_MIN(frames, s->frames - s->pos);
    uint64_t at = s->data_offset + s->pos * s->block;
    if (LND_IoGetPositionBytes(s->io) != at) LND_IoSeekBytes(s->io, at);
    int64_t bytes = LND_IoRead(s->io, dst, (size_t)(n * s->block));
    if (bytes < 0) return LND_CODEC_READ_ERROR;
    uint64_t got = (uint64_t)bytes / s->block;
    s->pos += got;
    return got;
}

static int32_t lnd_wav_seek(void *state, uint64_t frame) {
    lnd_wav_state *s = state;
    s->pos = LND_MIN(frame, s->frames);
    return LND_IoSeekBytes(s->io, s->data_offset + s->pos * s->block);
}

static void lnd_wav_close(void *state) { lnd_free(state); }

#if LND_MODULE_DECODE
#include "stream.h"
#endif

const LND_CODEC lnd_codec_wav = {
    .name = "wav",
    .stream_identifiers = "pcm",
    .extensions = "wav;wave;bwf;rf64",
    .probe = lnd_wav_probe,
    .open = lnd_wav_open,
    .read = lnd_wav_read,
    .seek = lnd_wav_seek,
    .close = lnd_wav_close,
#if LND_MODULE_DECODE
    .stream = &lnd_wav_stream_ops,
#endif
};
