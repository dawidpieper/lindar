#include "block.h"
#include "src/alloc.h"
#include "src/error.h"
#include "io/reader.h"
#include "formats/riff/read.h"
#include "lindar_codecs.h"

#include <string.h>

typedef struct lnd_adpcm_header {
    uint64_t data, bytes, frames, coef_offset;
    uint32_t tag, channels, sample_rate_hz, block, samples, coefficients;
} lnd_adpcm_header;

typedef struct lnd_adpcm_decoder {
    LND_IO *io;
    lnd_adpcm_header header;
    uint64_t position, cached;
    uint8_t *block;
    int16_t *pcm;
    int16_t (*coef)[2];
} lnd_adpcm_decoder;

static int32_t lnd_adpcm_parse(LND_IO *io, lnd_adpcm_header *h) {
    uint8_t riff[12];
    if (LND_IoSeekBytes(io, 0) != LND_OK || LND_IoRead(io, riff, 12) != 12 || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) return LND_ERR_FORMAT;
    lnd_riff reader;
    int32_t result = lnd_riff_open(riff, LND_IoGetSizeBytes(io), LND_RIFF_CLAMP_CONTAINER | LND_RIFF_ALLOW_MISSING_PAD, &reader);
    if (result != LND_OK) return result;
    bool have_fmt = false, have_data = false, have_fact = false;
    uint32_t fact = 0;
    for (uint64_t pos = 12; pos < reader.end;) {
        uint8_t chunk[8];
        if (LND_IoSeekBytes(io, pos) != LND_OK || LND_IoRead(io, chunk, 8) != 8) return LND_ERR_FORMAT;
        lnd_riff_chunk view;
        result = lnd_riff_next(&reader, chunk, &pos, &view);
        if (result != LND_OK) return result;
        uint64_t body = view.body;
        uint32_t bytes = (uint32_t)view.size;
        if (!memcmp(chunk, "fmt ", 4)) {
            uint8_t fmt[22];
            size_t n = LND_MIN(bytes, sizeof fmt);
            if (have_fmt || n < 20 || LND_IoRead(io, fmt, n) != (int64_t)n) return LND_ERR_FORMAT;
            h->tag = lnd_rd_u16le(fmt);
            h->channels = lnd_rd_u16le(fmt + 2);
            h->sample_rate_hz = lnd_rd_u32le(fmt + 4);
            h->block = lnd_rd_u16le(fmt + 12);
            h->samples = lnd_rd_u16le(fmt + 18);
            uint32_t extra = lnd_rd_u16le(fmt + 16);
            if ((h->tag != 2 && h->tag != 17) || !h->channels || h->channels > 2 || !h->sample_rate_hz || lnd_rd_u16le(fmt + 14) != 4 || extra < 2 || extra > bytes - 18)
                return LND_ERR_FORMAT;
            uint32_t count = lnd_adpcm_block_frames(h->tag, h->channels, h->block);
            if (!count || h->samples != count) return LND_ERR_FORMAT;
            if (h->tag == 17 && h->block % (4 * h->channels)) return LND_ERR_FORMAT;
            if (h->tag == 2) {
                if (n < 22 || extra < 4) return LND_ERR_FORMAT;
                h->coefficients = lnd_rd_u16le(fmt + 20);
                if (!h->coefficients || h->coefficients > 256 || h->coefficients * 4 > extra - 4) return LND_ERR_FORMAT;
                h->coef_offset = body + 22;
            }
            have_fmt = true;
        } else if (!memcmp(chunk, "fact", 4)) {
            uint8_t data[4];
            if (bytes < 4 || LND_IoRead(io, data, 4) != 4) return LND_ERR_FORMAT;
            fact = lnd_rd_u32le(data);
            have_fact = true;
        } else if (!memcmp(chunk, "data", 4) && !have_data) {
            h->data = body;
            h->bytes = bytes;
            have_data = true;
        }
    }
    if (!have_fmt || !have_data) return LND_ERR_FORMAT;
    h->frames = h->bytes / h->block * h->samples;
    uint32_t tail = (uint32_t)(h->bytes % h->block);
    if (tail) {
        uint32_t count = lnd_adpcm_block_frames(h->tag, h->channels, tail);
        if (!count || (h->tag == 17 && tail % (4 * h->channels))) return LND_ERR_FORMAT;
        h->frames += count;
    }
    if (have_fact) {
        if (fact > h->frames) return LND_ERR_FORMAT;
        h->frames = fact;
    }
    return LND_OK;
}

static int32_t lnd_adpcm_probe(LND_IO *io) {
    lnd_adpcm_header header = {0};
    return lnd_adpcm_parse(io, &header) == LND_OK ? 110 : 0;
}

static void lnd_adpcm_close(void *state) {
    lnd_adpcm_decoder *s = state;
    lnd_free(s->coef);
    lnd_free(s->pcm);
    lnd_free(s->block);
    lnd_free(s);
}

static int32_t lnd_adpcm_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    lnd_adpcm_header header = {0};
    int32_t r = lnd_adpcm_parse(io, &header);
    if (r != LND_OK) return r;
    lnd_adpcm_decoder *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->header = header;
    s->io = io;
    s->cached = UINT64_MAX;
    s->block = lnd_alloc(header.block);
    s->pcm = lnd_alloc((size_t)header.samples * header.channels * sizeof(int16_t));
    if (header.coefficients) s->coef = lnd_alloc(header.coefficients * sizeof *s->coef);
    if (!s->block || !s->pcm || (header.coefficients && !s->coef)) {
        lnd_adpcm_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    if (header.coefficients) {
        LND_IoSeekBytes(io, header.coef_offset);
        for (uint32_t i = 0; i < header.coefficients; i++) {
            uint8_t pair[4];
            if (LND_IoRead(io, pair, 4) != 4) {
                lnd_adpcm_close(s);
                return LND_ERR_IO;
            }
            s->coef[i][0] = (int16_t)lnd_rd_u16le(pair);
            s->coef[i][1] = (int16_t)lnd_rd_u16le(pair + 2);
        }
    }
    *info = (LND_CODEC_INFO){.format = LND_FORMAT_S16, .channels = header.channels, .sample_rate_hz = header.sample_rate_hz, .length_frames = header.frames, .seekable = true};
    *state = s;
    return LND_OK;
}

static uint64_t lnd_adpcm_read(void *state, void *dst, uint64_t frames) {
    lnd_adpcm_decoder *s = state;
    lnd_adpcm_header *h = &s->header;
    uint64_t total = 0;
    frames = LND_MIN(frames, h->frames - s->position);
    while (total < frames) {
        uint64_t index = s->position / h->samples;
        uint32_t skip = (uint32_t)(s->position % h->samples);
        if (index != s->cached) {
            uint32_t bytes = (uint32_t)LND_MIN(h->bytes - index * h->block, h->block);
            uint32_t count = lnd_adpcm_block_frames(h->tag, h->channels, bytes);
            if (LND_IoSeekBytes(s->io, h->data + index * h->block) != LND_OK || LND_IoRead(s->io, s->block, bytes) != bytes) {
                lnd_error(LND_ERR_IO);
                break;
            }
            if (!lnd_adpcm_decode_block(h->tag, h->channels, s->block, count, s->coef, h->coefficients, s->pcm)) {
                lnd_error(LND_ERR_FORMAT);
                break;
            }
            s->cached = index;
        }
        uint64_t n = LND_MIN(frames - total, h->samples - skip);
        size_t width = h->channels * sizeof(int16_t);
        memcpy((uint8_t *)dst + (size_t)total * width, s->pcm + skip * h->channels, (size_t)n * width);
        total += n;
        s->position += n;
    }
    return total;
}

static int32_t lnd_adpcm_seek(void *state, uint64_t frame) {
    lnd_adpcm_decoder *s = state;
    s->position = LND_MIN(frame, s->header.frames);
    return LND_OK;
}

const LND_CODEC lnd_codec_adpcm = {.name = "adpcm",
                                   .extensions = "wav;wave",
                                   .probe = lnd_adpcm_probe,
                                   .open = lnd_adpcm_open,
                                   .read = lnd_adpcm_read,
                                   .seek = lnd_adpcm_seek,
                                   .close = lnd_adpcm_close};
