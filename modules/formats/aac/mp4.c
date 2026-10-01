#include "mp4.h"
#include "src/alloc.h"
#include "lindar_output.h"

#include <string.h>

typedef struct lnd_mp4_buf {
    uint8_t *data;
    size_t size;
    size_t cap;
    bool ok;
} lnd_mp4_buf;

static void lnd_mp4_put(lnd_mp4_buf *b, const void *data, size_t n) {
    if (!b->ok) return;
    if (n > SIZE_MAX - b->size) {
        b->ok = false;
        return;
    }
    size_t need = b->size + n;
    if (need > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        while (cap < need) cap = cap > SIZE_MAX / 2 ? need : cap * 2;
        uint8_t *grown = lnd_realloc(b->data, cap);
        if (!grown) {
            b->ok = false;
            return;
        }
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->size, data, n);
    b->size += n;
}

static void lnd_mp4_u8(lnd_mp4_buf *b, uint8_t v) { lnd_mp4_put(b, &v, 1); }

static void lnd_mp4_u16(lnd_mp4_buf *b, uint16_t v) {
    uint8_t p[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    lnd_mp4_put(b, p, 2);
}

static void lnd_mp4_u32(lnd_mp4_buf *b, uint32_t v) {
    uint8_t p[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    lnd_mp4_put(b, p, 4);
}

static void lnd_mp4_u64(lnd_mp4_buf *b, uint64_t v) {
    lnd_mp4_u32(b, (uint32_t)(v >> 32));
    lnd_mp4_u32(b, (uint32_t)v);
}

static void lnd_mp4_tag(lnd_mp4_buf *b, const char *tag) { lnd_mp4_put(b, tag, 4); }

static size_t lnd_mp4_box_begin(lnd_mp4_buf *b, const char *tag) {
    size_t at = b->size;
    lnd_mp4_u32(b, 0);
    lnd_mp4_tag(b, tag);
    return at;
}

static void lnd_mp4_box_end(lnd_mp4_buf *b, size_t at) {
    if (!b->ok) return;
    uint32_t size = (uint32_t)(b->size - at);
    b->data[at] = (uint8_t)(size >> 24);
    b->data[at + 1] = (uint8_t)(size >> 16);
    b->data[at + 2] = (uint8_t)(size >> 8);
    b->data[at + 3] = (uint8_t)size;
}

static void lnd_mp4_full(lnd_mp4_buf *b, uint8_t version, uint32_t flags) { lnd_mp4_u32(b, (uint32_t)version << 24 | (flags & 0xFFFFFF)); }

static void lnd_mp4_desc(lnd_mp4_buf *b, uint8_t tag, uint32_t len) {
    lnd_mp4_u8(b, tag);
    lnd_mp4_u8(b, (uint8_t)(0x80 | ((len >> 21) & 0x7F)));
    lnd_mp4_u8(b, (uint8_t)(0x80 | ((len >> 14) & 0x7F)));
    lnd_mp4_u8(b, (uint8_t)(0x80 | ((len >> 7) & 0x7F)));
    lnd_mp4_u8(b, (uint8_t)(len & 0x7F));
}

int32_t lnd_mp4_begin(lnd_mp4_mux *m, lnd_io *io, uint32_t sample_rate_hz, uint32_t channels, uint32_t frame_length, uint32_t delay, uint32_t bitrate_bps, const uint8_t *asc,
                      uint32_t asc_len) {
    if (!LND_IoCanSeek(io) || asc_len > sizeof m->asc) return LND_ERR_UNSUPPORTED;
    memset(m, 0, sizeof *m);
    m->io = io;
    m->sample_rate_hz = sample_rate_hz;
    m->channels = channels;
    m->frame_length = frame_length;
    m->delay = delay;
    m->bitrate_bps = bitrate_bps;
    memcpy(m->asc, asc, asc_len);
    m->asc_len = asc_len;
    static const uint8_t ftyp[] = {0, 0, 0,   0x1C, 'f', 't', 'y', 'p', 'M', '4', 'A', ' ', 0,   0,
                                   0, 0, 'M', '4',  'A', ' ', 'i', 's', 'o', 'm', 'm', 'p', '4', '2'};
    static const uint8_t mdat[] = {0, 0, 0, 0, 'm', 'd', 'a', 't'};
    if (LND_IoWrite(io, ftyp, sizeof ftyp) != sizeof ftyp) return LND_ERR_IO;
    m->mdat_pos = LND_IoGetPositionBytes(io);
    if (LND_IoWrite(io, mdat, sizeof mdat) != sizeof mdat) return LND_ERR_IO;
    return LND_OK;
}

int32_t lnd_mp4_sample(lnd_mp4_mux *m, const void *data, uint32_t size) {
    uint32_t limit = (uint32_t)LND_MIN((uint64_t)UINT32_MAX, SIZE_MAX / sizeof *m->sizes);
    if (m->count >= limit) return LND_ERR_OUT_OF_MEMORY;
    if (m->count == m->cap) {
        uint32_t cap = m->cap ? (m->cap > limit / 2 ? limit : m->cap * 2) : 1024;
        uint32_t *sizes = lnd_realloc(m->sizes, sizeof(uint32_t) * cap);
        if (!sizes) return LND_ERR_OUT_OF_MEMORY;
        m->sizes = sizes;
        m->cap = cap;
    }
    if (LND_IoWrite(m->io, data, size) != size) return LND_ERR_IO;
    m->sizes[m->count++] = size;
    m->bytes += size;
    return LND_OK;
}

int32_t lnd_mp4_finish(lnd_mp4_mux *m, uint64_t pcm_frames) {
    uint64_t end = LND_IoGetPositionBytes(m->io);
    uint64_t mdat_size = end - m->mdat_pos;
    if (mdat_size > 0xFFFFFFFFull) return LND_ERR_UNSUPPORTED;
    uint8_t size_be[4] = {(uint8_t)(mdat_size >> 24), (uint8_t)(mdat_size >> 16), (uint8_t)(mdat_size >> 8), (uint8_t)mdat_size};
    if (LND_IoSeekBytes(m->io, m->mdat_pos) != LND_OK || LND_IoWrite(m->io, size_be, 4) != 4 || LND_IoSeekBytes(m->io, end) != LND_OK) return LND_ERR_IO;
    lnd_mp4_buf b = {.ok = true};
    uint64_t media_duration = (uint64_t)m->count * m->frame_length;
    uint64_t presentation = pcm_frames ? pcm_frames : (media_duration > m->delay ? media_duration - m->delay : media_duration);
    size_t moov = lnd_mp4_box_begin(&b, "moov");
    size_t mvhd = lnd_mp4_box_begin(&b, "mvhd");
    lnd_mp4_full(&b, 1, 0);
    lnd_mp4_u64(&b, 0);
    lnd_mp4_u64(&b, 0);
    lnd_mp4_u32(&b, m->sample_rate_hz);
    lnd_mp4_u64(&b, presentation);
    lnd_mp4_u32(&b, 0x00010000);
    lnd_mp4_u16(&b, 0x0100);
    lnd_mp4_u16(&b, 0);
    lnd_mp4_u64(&b, 0);
    static const uint32_t matrix[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    for (int i = 0; i < 9; i++) lnd_mp4_u32(&b, matrix[i]);
    for (int i = 0; i < 6; i++) lnd_mp4_u32(&b, 0);
    lnd_mp4_u32(&b, 2);
    lnd_mp4_box_end(&b, mvhd);
    size_t trak = lnd_mp4_box_begin(&b, "trak");
    size_t tkhd = lnd_mp4_box_begin(&b, "tkhd");
    lnd_mp4_full(&b, 1, 7);
    lnd_mp4_u64(&b, 0);
    lnd_mp4_u64(&b, 0);
    lnd_mp4_u32(&b, 1);
    lnd_mp4_u32(&b, 0);
    lnd_mp4_u64(&b, presentation);
    lnd_mp4_u64(&b, 0);
    lnd_mp4_u16(&b, 0);
    lnd_mp4_u16(&b, 0);
    lnd_mp4_u16(&b, 0x0100);
    lnd_mp4_u16(&b, 0);
    for (int i = 0; i < 9; i++) lnd_mp4_u32(&b, matrix[i]);
    lnd_mp4_u32(&b, 0);
    lnd_mp4_u32(&b, 0);
    lnd_mp4_box_end(&b, tkhd);
    size_t edts = lnd_mp4_box_begin(&b, "edts");
    size_t elst = lnd_mp4_box_begin(&b, "elst");
    lnd_mp4_full(&b, 1, 0);
    lnd_mp4_u32(&b, 1);
    lnd_mp4_u64(&b, presentation);
    lnd_mp4_u64(&b, m->delay);
    lnd_mp4_u32(&b, 0x00010000);
    lnd_mp4_box_end(&b, elst);
    lnd_mp4_box_end(&b, edts);
    size_t mdia = lnd_mp4_box_begin(&b, "mdia");
    size_t mdhd = lnd_mp4_box_begin(&b, "mdhd");
    lnd_mp4_full(&b, 1, 0);
    lnd_mp4_u64(&b, 0);
    lnd_mp4_u64(&b, 0);
    lnd_mp4_u32(&b, m->sample_rate_hz);
    lnd_mp4_u64(&b, media_duration);
    lnd_mp4_u16(&b, 0x55C4);
    lnd_mp4_u16(&b, 0);
    lnd_mp4_box_end(&b, mdhd);
    size_t hdlr = lnd_mp4_box_begin(&b, "hdlr");
    lnd_mp4_full(&b, 0, 0);
    lnd_mp4_u32(&b, 0);
    lnd_mp4_tag(&b, "soun");
    for (int i = 0; i < 3; i++) lnd_mp4_u32(&b, 0);
    lnd_mp4_put(&b, "SoundHandler", 13);
    lnd_mp4_box_end(&b, hdlr);
    size_t minf = lnd_mp4_box_begin(&b, "minf");
    size_t smhd = lnd_mp4_box_begin(&b, "smhd");
    lnd_mp4_full(&b, 0, 0);
    lnd_mp4_u32(&b, 0);
    lnd_mp4_box_end(&b, smhd);
    size_t dinf = lnd_mp4_box_begin(&b, "dinf");
    size_t dref = lnd_mp4_box_begin(&b, "dref");
    lnd_mp4_full(&b, 0, 0);
    lnd_mp4_u32(&b, 1);
    size_t url = lnd_mp4_box_begin(&b, "url ");
    lnd_mp4_full(&b, 0, 1);
    lnd_mp4_box_end(&b, url);
    lnd_mp4_box_end(&b, dref);
    lnd_mp4_box_end(&b, dinf);
    size_t stbl = lnd_mp4_box_begin(&b, "stbl");
    size_t stsd = lnd_mp4_box_begin(&b, "stsd");
    lnd_mp4_full(&b, 0, 0);
    lnd_mp4_u32(&b, 1);
    size_t mp4a = lnd_mp4_box_begin(&b, "mp4a");
    for (int i = 0; i < 6; i++) lnd_mp4_u8(&b, 0);
    lnd_mp4_u16(&b, 1);
    lnd_mp4_u64(&b, 0);
    lnd_mp4_u16(&b, (uint16_t)m->channels);
    lnd_mp4_u16(&b, 16);
    lnd_mp4_u32(&b, 0);
    lnd_mp4_u32(&b, m->sample_rate_hz < 65536 ? m->sample_rate_hz << 16 : 0);
    size_t esds = lnd_mp4_box_begin(&b, "esds");
    lnd_mp4_full(&b, 0, 0);
    uint32_t dsi_len = m->asc_len;
    uint32_t dcd_len = 13 + 5 + dsi_len;
    uint32_t es_len = 3 + 5 + dcd_len + 5 + 1;
    lnd_mp4_desc(&b, 0x03, es_len);
    lnd_mp4_u16(&b, 1);
    lnd_mp4_u8(&b, 0);
    lnd_mp4_desc(&b, 0x04, dcd_len);
    lnd_mp4_u8(&b, 0x40);
    lnd_mp4_u8(&b, 0x15);
    lnd_mp4_u8(&b, 0);
    lnd_mp4_u16(&b, 0x1800);
    lnd_mp4_u32(&b, m->bitrate_bps);
    lnd_mp4_u32(&b, m->bitrate_bps);
    lnd_mp4_desc(&b, 0x05, dsi_len);
    lnd_mp4_put(&b, m->asc, dsi_len);
    lnd_mp4_desc(&b, 0x06, 1);
    lnd_mp4_u8(&b, 0x02);
    lnd_mp4_box_end(&b, esds);
    lnd_mp4_box_end(&b, mp4a);
    lnd_mp4_box_end(&b, stsd);
    size_t stts = lnd_mp4_box_begin(&b, "stts");
    lnd_mp4_full(&b, 0, 0);
    lnd_mp4_u32(&b, 1);
    lnd_mp4_u32(&b, m->count);
    lnd_mp4_u32(&b, m->frame_length);
    lnd_mp4_box_end(&b, stts);
    size_t stsc = lnd_mp4_box_begin(&b, "stsc");
    lnd_mp4_full(&b, 0, 0);
    lnd_mp4_u32(&b, 1);
    lnd_mp4_u32(&b, 1);
    lnd_mp4_u32(&b, m->count ? m->count : 1);
    lnd_mp4_u32(&b, 1);
    lnd_mp4_box_end(&b, stsc);
    size_t stsz = lnd_mp4_box_begin(&b, "stsz");
    lnd_mp4_full(&b, 0, 0);
    lnd_mp4_u32(&b, 0);
    lnd_mp4_u32(&b, m->count);
    for (uint32_t i = 0; i < m->count; i++) lnd_mp4_u32(&b, m->sizes[i]);
    lnd_mp4_box_end(&b, stsz);
    size_t stco = lnd_mp4_box_begin(&b, "stco");
    lnd_mp4_full(&b, 0, 0);
    lnd_mp4_u32(&b, 1);
    lnd_mp4_u32(&b, (uint32_t)(m->mdat_pos + 8));
    lnd_mp4_box_end(&b, stco);
    lnd_mp4_box_end(&b, stbl);
    lnd_mp4_box_end(&b, minf);
    lnd_mp4_box_end(&b, mdia);
    lnd_mp4_box_end(&b, trak);
    lnd_mp4_box_end(&b, moov);
    int32_t r = b.ok ? LND_OK : LND_ERR_OUT_OF_MEMORY;
    if (r == LND_OK && LND_IoWrite(m->io, b.data, b.size) != b.size) r = LND_ERR_IO;
    lnd_free(b.data);
    return r;
}

void lnd_mp4_free(lnd_mp4_mux *m) {
    lnd_free(m->sizes);
    m->sizes = nullptr;
}
