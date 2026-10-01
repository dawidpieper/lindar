#include "io/reader.h"
#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_codecs.h"
#include "src/format.h"

#include <string.h>

typedef struct lnd_aiff_state {
    lnd_io *io;
    uint64_t data_offset;
    uint32_t block;
    uint32_t sample_bytes;
    uint32_t channels;
    uint64_t frames;
    uint64_t pos;
    bool big_endian;
    bool signed8;
} lnd_aiff_state;

static int32_t lnd_aiff_probe(LND_IO *io) {
    uint8_t h[12];
    if (LND_IoRead(io, h, 12) != 12) return 0;
    return lnd_tag_is(h, "FORM") && (lnd_tag_is(h + 8, "AIFF") || lnd_tag_is(h + 8, "AIFC")) ? 100 : 0;
}

static int32_t lnd_aiff_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    uint8_t h[12];
    if (LND_IoRead(io, h, 12) != 12 || !lnd_tag_is(h, "FORM")) return LND_ERR_FORMAT;
    bool aifc = lnd_tag_is(h + 8, "AIFC");
    if (!aifc && !lnd_tag_is(h + 8, "AIFF")) return LND_ERR_FORMAT;
    uint64_t pos = 12;
    uint64_t size = LND_IoGetSizeBytes(io);
    uint32_t channels = 0, bits = 0, sample_rate_hz = 0;
    uint64_t comm_frames = 0, data_offset = 0, data_bytes = 0;
    bool big_endian = true, is_float = false, have_comm = false, have_data = false;
    while (pos + 8 <= size && !(have_comm && have_data)) {
        uint8_t ch[8];
        LND_IoSeekBytes(io, pos);
        if (LND_IoRead(io, ch, 8) != 8) break;
        uint64_t csize = lnd_rd_u32be(ch + 4);
        uint64_t body = pos + 8;
        if (lnd_tag_is(ch, "COMM") && csize >= 18) {
            uint8_t c[64];
            size_t n = (size_t)LND_MIN(csize, (uint64_t)sizeof c);
            if (LND_IoRead(io, c, n) != (int64_t)n) return LND_ERR_FORMAT;
            channels = lnd_rd_u16be(c);
            comm_frames = lnd_rd_u32be(c + 2);
            bits = lnd_rd_u16be(c + 6);
            double rate = lnd_rd_f80be(c + 8);
            if (!(rate >= 0.5 && rate < (double)UINT32_MAX + 0.5)) return LND_ERR_FORMAT;
            sample_rate_hz = (uint32_t)(rate + 0.5);
            if (aifc && n >= 22) {
                const uint8_t *comp = c + 18;
                if (lnd_tag_is(comp, "NONE") || lnd_tag_is(comp, "twos")) big_endian = true;
                else if (lnd_tag_is(comp, "sowt")) big_endian = false;
                else if (lnd_tag_is(comp, "fl32") || lnd_tag_is(comp, "FL32")) is_float = true, bits = 32;
                else if (lnd_tag_is(comp, "fl64") || lnd_tag_is(comp, "FL64")) is_float = true, bits = 64;
                else return LND_ERR_FORMAT;
            }
            have_comm = true;
        } else if (lnd_tag_is(ch, "SSND") && csize >= 8) {
            uint8_t s[8];
            if (LND_IoRead(io, s, 8) != 8) return LND_ERR_FORMAT;
            uint64_t offset = lnd_rd_u32be(s);
            if (csize < 8 + offset) return LND_ERR_FORMAT;
            data_offset = body + 8 + offset;
            data_bytes = csize - 8 - offset;
            if (data_offset + data_bytes > size) data_bytes = size > data_offset ? size - data_offset : 0;
            have_data = true;
        }
        pos = body + csize + (csize & 1);
    }
    if (!have_comm || !have_data || channels < 1 || channels > LND_MAX_CHANNELS || sample_rate_hz < 1 || bits < 1 || bits > 64) return LND_ERR_FORMAT;
    int32_t format;
    uint32_t sample_bytes;
    if (is_float) {
        format = bits == 64 ? LND_FORMAT_F64 : LND_FORMAT_F32;
        sample_bytes = bits == 64 ? 8 : 4;
    } else if (bits <= 8) {
        format = LND_FORMAT_U8;
        sample_bytes = 1;
    } else if (bits <= 16) {
        format = LND_FORMAT_S16;
        sample_bytes = 2;
    } else if (bits <= 24) {
        format = LND_FORMAT_S24;
        sample_bytes = 3;
    } else if (bits <= 32) {
        format = LND_FORMAT_S32;
        sample_bytes = 4;
    } else {
        return LND_ERR_FORMAT;
    }
    lnd_aiff_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->data_offset = data_offset;
    s->sample_bytes = sample_bytes;
    s->channels = channels;
    s->block = sample_bytes * channels;
    s->frames = data_bytes / s->block;
    if (comm_frames && comm_frames < s->frames) s->frames = comm_frames;
    s->big_endian = big_endian;
    s->signed8 = !is_float && bits <= 8;
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

static uint64_t lnd_aiff_read(void *state, void *dst, uint64_t frames) {
    lnd_aiff_state *s = state;
    if (s->pos >= s->frames) return 0;
    uint64_t n = LND_MIN(frames, s->frames - s->pos);
    uint64_t at = s->data_offset + s->pos * s->block;
    if (LND_IoGetPositionBytes(s->io) != at) LND_IoSeekBytes(s->io, at);
    int64_t bytes = LND_IoRead(s->io, dst, (size_t)(n * s->block));
    if (bytes < 0) return LND_CODEC_READ_ERROR;
    uint64_t got = (uint64_t)bytes / s->block;
    size_t samples = (size_t)(got * s->channels);
    if (s->big_endian) lnd_swap_block(dst, s->sample_bytes, samples);
    if (s->signed8) {
        uint8_t *p = dst;
        for (size_t i = 0; i < samples; i++) p[i] ^= 0x80;
    }
    s->pos += got;
    return got;
}

static int32_t lnd_aiff_seek(void *state, uint64_t frame) {
    lnd_aiff_state *s = state;
    s->pos = LND_MIN(frame, s->frames);
    return LND_IoSeekBytes(s->io, s->data_offset + s->pos * s->block);
}

static void lnd_aiff_close(void *state) { lnd_free(state); }

const LND_CODEC lnd_codec_aiff = {
    .name = "aiff",
    .extensions = "aif;aiff;aifc",
    .probe = lnd_aiff_probe,
    .open = lnd_aiff_open,
    .read = lnd_aiff_read,
    .seek = lnd_aiff_seek,
    .close = lnd_aiff_close,
};
