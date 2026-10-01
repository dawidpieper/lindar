#include "formats/decode/stream.h"
#include "formats/decode/metadata.h"

typedef struct lnd_flac_stream {
    lnd_flac_state file;
    lnd_decode_tags tags;
    const uint8_t *input;
    size_t bytes;
    size_t scan;
    uint64_t decoded;
    uint32_t skip;
    uint16_t crc;
    bool metadata;
    bool last;
    bool raw;
} lnd_flac_stream;

static FLAC__StreamDecoderReadStatus lnd_flac_stream_read(const FLAC__StreamDecoder *decoder, FLAC__byte data[], size_t *bytes, void *user) {
    lnd_flac_stream *s = user;
    *bytes = LND_MIN(*bytes, s->bytes);
    if (!*bytes) return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    memcpy(data, s->input, *bytes);
    s->input += *bytes;
    s->bytes -= *bytes;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static void lnd_flac_stream_error(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *user) {
    ((lnd_flac_stream *)user)->file.failed = true;
}

static void *lnd_flac_stream_create(void) { return lnd_alloc_zero(sizeof(lnd_flac_stream)); }

static int32_t lnd_flac_stream_init(lnd_flac_stream *s, const uint8_t info[34]) {
    uint8_t header[42] = {'f', 'L', 'a', 'C', 128, 0, 0, 34};
    memcpy(header + 8, info, 34);
    s->input = header;
    s->bytes = sizeof header;
    lnd_flac_state *f = &s->file;
    f->dec = FLAC__stream_decoder_new();
    if (!f->dec) return LND_ERR_OUT_OF_MEMORY;
    FLAC__stream_decoder_set_md5_checking(f->dec, false);
    if (FLAC__stream_decoder_init_stream(f->dec, lnd_flac_stream_read, nullptr, nullptr, nullptr, nullptr, lnd_flac_write_cb, lnd_flac_metadata_cb,
                                         lnd_flac_stream_error, s) != FLAC__STREAM_DECODER_INIT_STATUS_OK ||
        !FLAC__stream_decoder_process_until_end_of_metadata(f->dec) || f->failed || !f->sample_rate_hz || !f->channels || f->channels > LND_MAX_CHANNELS ||
        f->bits < 4 || f->bits > 32)
        return LND_ERR_FORMAT;
    f->format = f->bits <= 16 ? LND_FORMAT_S16 : f->bits <= 24 ? LND_FORMAT_S24 : LND_FORMAT_S32;
    f->frame_bytes = f->channels * lnd_format_bytes(f->format);
    return LND_OK;
}

static uint16_t lnd_flac_crc16(uint16_t crc, uint8_t byte) {
    crc ^= (uint16_t)byte << 8;
    for (unsigned i = 0; i < 8; i++)
        crc = (uint16_t)(crc << 1 ^ (crc & 0x8000 ? 0x8005 : 0));
    return crc;
}

static int32_t lnd_flac_header(const uint8_t *p, size_t bytes) {
    if (bytes < 6) return 0;
    if (p[0] != 255 || (p[1] & 0xfe) != 0xf8 || !(p[2] >> 4) || (p[2] & 15) == 15 || (p[3] & 1) || (p[3] >> 4) > 10 || (p[3] >> 1 & 7) == 3) return -1;
    unsigned count = 0;
    for (uint8_t mask = 128; p[4] & mask; mask >>= 1)
        count++;
    if (count == 1 || count > 7) return -1;
    size_t at = 4 + LND_MAX(count, 1u);
    if (bytes < at) return 0;
    for (unsigned i = 1; i < count; i++)
        if ((p[4 + i] & 0xc0) != 0x80) return -1;
    unsigned block = p[2] >> 4, sample_rate_hz = p[2] & 15;
    at += block == 6 ? 1 : block == 7 ? 2 : 0;
    at += sample_rate_hz == 12 ? 1 : sample_rate_hz == 13 || sample_rate_hz == 14 ? 2 : 0;
    if (bytes <= at) return 0;
    uint8_t crc = 0;
    for (size_t i = 0; i <= at; i++) {
        crc ^= p[i];
        for (unsigned j = 0; j < 8; j++)
            crc = (uint8_t)(crc << 1 ^ (crc & 128 ? 7 : 0));
    }
    return crc ? -1 : (int32_t)at + 1;
}

static int32_t lnd_flac_stream_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    lnd_flac_stream *s = state;
    lnd_flac_state *f = &s->file;
    *pcm = (LND_PCM){0};
    if (!f->dec) {
        if (bytes < 42) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        if (memcmp(data, "fLaC", 4) || (data[4] & 127) || data[5] || data[6] || data[7] != 34) return LND_ERR_FORMAT;
        int32_t r = lnd_flac_stream_init(s, data + 8);
        if (r != LND_OK) return r;
        *used = 42;
        s->metadata = !(data[4] & 128);
        return LND_SOURCE_READY;
    }
    *info = (LND_CODEC_INFO){.sample_rate_hz = f->sample_rate_hz, .channels = f->channels, .format = f->format, .length_frames = f->total};
    if (s->metadata) {
        if (s->skip) {
            *used = LND_MIN(bytes, s->skip);
            lnd_decode_tags_append(&s->tags, data, *used);
            s->skip -= (uint32_t)*used;
            if (!s->skip && s->last) s->metadata = false;
            return *used ? LND_SOURCE_READY : end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        }
        if (bytes < 4) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        if ((data[0] & 127) == 127 || !(data[0] & 127)) return LND_ERR_FORMAT;
        s->last = (data[0] & 128) != 0;
        s->skip = (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 | data[3];
        if ((data[0] & 127) == 4) lnd_decode_tags_start(&s->tags, s->skip, LND_METADATA_FLAC);
        *used = 4;
        if (!s->skip && s->last) s->metadata = false;
        return LND_SOURCE_READY;
    }
    if (!bytes) {
        if (end && f->total && s->decoded != f->total) return LND_ERR_FORMAT;
        if (end) info->length_frames = s->decoded;
        return end ? LND_SOURCE_EOF : LND_SOURCE_WAITING;
    }
    size_t length = 0, prefix = 0;
    if (s->raw) {
        if (bytes < 8) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        length = (size_t)data[0] << 24 | (size_t)data[1] << 16 | (size_t)data[2] << 8 | data[3];
        if (!length) return LND_ERR_FORMAT;
        if (length > bytes - 8) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        prefix = 8;
    } else {
        int32_t header = lnd_flac_header(data, bytes);
        if (header < 0) return LND_ERR_FORMAT;
        if (!header) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        while (s->scan < bytes) {
            if (s->scan >= (size_t)header + 2 && !s->crc && data[s->scan] == 255) {
                int32_t next = lnd_flac_header(data + s->scan, bytes - s->scan);
                if (next > 0) {
                    length = s->scan;
                    break;
                }
                if (!next && !end) return LND_SOURCE_WAITING;
            }
            s->crc = lnd_flac_crc16(s->crc, data[s->scan++]);
        }
        if (!length) {
            if (!end) return LND_SOURCE_WAITING;
            if (s->crc) return LND_ERR_FORMAT;
            length = bytes;
        }
    }
    s->input = data + prefix;
    s->bytes = length;
    f->pending_frames = f->pending_offset = 0;
    if (!FLAC__stream_decoder_process_single(f->dec) || f->failed || !f->pending_frames) return LND_ERR_FORMAT;
    *used = prefix + length;
    s->scan = s->crc = 0;
    s->decoded += f->pending_frames;
    *pcm = (LND_PCM){.data = f->pending, .frames = f->pending_frames, .channels = f->channels, .format = f->format, .layout = LND_LAYOUT_INTERLEAVED};
    return LND_SOURCE_READY;
}

static int32_t lnd_flac_stream_configure(void *state, const LND_CODEC_STREAM_CONFIG *config) {
    lnd_flac_stream *s = state;
    const uint8_t *p = config->data;
    if (config->bytes < 42 || p[0] || p[1] || p[2] || p[3] || (p[4] & 127) || p[5] || p[6] || p[7] != 34) return LND_ERR_FORMAT;
    int32_t r = lnd_flac_stream_init(s, p + 8);
    if (r == LND_OK) {
        s->raw = true;
        s->file.total = 0;
    }
    return r;
}

static void lnd_flac_stream_close(void *state) {
    lnd_decode_tags_clear(&((lnd_flac_stream *)state)->tags);
    lnd_flac_close(state);
}

static int32_t lnd_flac_stream_metadata(void *state, LND_METADATA *m, uint64_t *revision) {
    return lnd_decode_tags_get(&((lnd_flac_stream *)state)->tags, m, revision);
}

static const LND_CODEC_STREAM lnd_flac_stream_ops = {.metadata = lnd_flac_stream_metadata,
                                                     .create = lnd_flac_stream_create,
                                                     .step = lnd_flac_stream_step,
                                                     .close = lnd_flac_stream_close,
                                                     .configure = lnd_flac_stream_configure};
