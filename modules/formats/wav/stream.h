#include "formats/decode/stream.h"

typedef struct lnd_wav_stream {
    uint64_t skip;
    uint64_t remaining;
    lnd_riff riff;
    uint64_t position;
    uint32_t block;
    bool header;
    bool data;
} lnd_wav_stream;

static void *lnd_wav_stream_create(void) { return lnd_alloc_zero(sizeof(lnd_wav_stream)); }

static int32_t lnd_wav_stream_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    lnd_wav_stream *s = state;
    *pcm = (LND_PCM){0};
    if (!s->header) {
        if (bytes < 12) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        int32_t result = lnd_riff_open(data, UINT64_MAX, LND_RIFF_PHYSICAL, &s->riff);
        if (result != LND_OK) return result;
        s->position = 12;
        s->header = true;
        *used = 12;
        return LND_SOURCE_READY;
    }
    if (s->skip) {
        *used = (size_t)LND_MIN(s->skip, bytes);
        s->skip -= *used;
        return !*used && end ? LND_ERR_FORMAT : LND_SOURCE_READY;
    }
    if (s->data) {
        if (!s->remaining) return LND_SOURCE_EOF;
        size_t take = (size_t)LND_MIN(LND_MIN(s->remaining, bytes), (uint64_t)s->block * 2048);
        take -= take % s->block;
        if (!take) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        *pcm = (LND_PCM){.data = (void *)data, .frames = take / s->block, .channels = info->channels, .format = info->format, .layout = LND_LAYOUT_INTERLEAVED};
        *used = take;
        s->remaining -= take;
        return LND_SOURCE_READY;
    }
    if (bytes < 8) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
    lnd_riff_chunk chunk;
    uint64_t next = s->position;
    int32_t result = lnd_riff_next(&s->riff, data, &next, &chunk);
    if (result != LND_OK) return result;
    uint64_t length = chunk.size;
    if (!memcmp(data, "fmt ", 4)) {
        if (length < 16) return LND_ERR_FORMAT;
        size_t need = (size_t)LND_MIN(length, 40);
        if (bytes < 8 + need) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        uint16_t tag = lnd_rd_u16(data + 8);
        LND_CODEC_INFO parsed = *info;
        parsed.format = lnd_wav_format(tag, lnd_rd_u16(data + 22), tag == 0xfffe && need >= 40 ? data + 32 : nullptr);
        parsed.channels = lnd_rd_u16(data + 10);
        parsed.sample_rate_hz = lnd_rd_u32(data + 12);
        if (!parsed.format || !parsed.channels || parsed.channels > LND_MAX_CHANNELS || !parsed.sample_rate_hz) return LND_ERR_FORMAT;
        uint32_t width = parsed.channels * LND_PcmGetSampleBytes(parsed.format);
        if (width != lnd_rd_u16(data + 20)) return LND_ERR_FORMAT;
        *info = parsed;
        s->block = width;
    } else if (!memcmp(data, "ds64", 4) && s->riff.rf64) {
        if (length < 28) return LND_ERR_FORMAT;
        if (bytes < 36) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        result = lnd_riff_ds64(&s->riff, data + 8, bytes - 8, length);
        if (result != LND_OK) return result;
    } else if (!memcmp(data, "data", 4)) {
        if (!s->block) return LND_ERR_FORMAT;
        s->remaining = length;
        if (s->remaining % s->block) return LND_ERR_FORMAT;
        info->length_frames = s->remaining / s->block;
        info->length_known = true;
        s->data = true;
        *used = 8;
        return LND_SOURCE_READY;
    }
    *used = 8;
    s->position = next;
    s->skip = chunk.next - chunk.body;
    return LND_SOURCE_READY;
}

static const LND_CODEC_STREAM lnd_wav_stream_ops = {.create = lnd_wav_stream_create, .step = lnd_wav_stream_step, .close = lnd_free};
