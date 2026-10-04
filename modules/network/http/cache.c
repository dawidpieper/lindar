#include "cache.h"

#include <string.h>

typedef struct lnd_cache_block {
    struct lnd_cache_block *next;
    uint64_t offset;
    uint64_t touched;
    size_t capacity;
    size_t filled;
    uint8_t data[];
} lnd_cache_block;

struct lnd_http_cache {
    lnd_cache_block *blocks;
    lnd_cache_block *cursor;
    void *transfer;
    uint64_t requested;
    uint64_t last_data;
    size_t received;
    size_t request_bytes;
    bool ranges;
    uint64_t size;
    uint64_t clock;
    uint64_t ogg_offset;
    int64_t ogg_base_us;
    int64_t ogg_time_us;
    uint32_t ogg_rate;
    uint32_t ogg_skip;
    uint32_t ogg_serial;
    uint64_t revision;
    uint64_t protect_start;
    uint64_t protect_end;
    size_t chunk;
    size_t allocated;
    size_t retained;
    size_t limit;
    uint32_t views;
    bool known;
    bool complete;
    char etag[256];
    char url[];
};

static size_t lnd_cache_cost(size_t capacity) { return sizeof(lnd_cache_block) + capacity + (capacity + 7) / 8; }
static uint8_t *lnd_cache_data(lnd_cache_block *block) { return block->data + (block->capacity + 7) / 8; }

void lnd_http_cache_free(lnd_http_session *s) {
    lnd_http_cache *c = s->cache;
    if (!c) return;
    lnd_http_cache_stop(s);
    while (c->blocks) {
        lnd_cache_block *block = c->blocks;
        c->blocks = block->next;
        lnd_free(block);
    }
    lnd_free(c);
    s->cache = nullptr;
}

bool lnd_http_cache_begin(lnd_http_session *s, const char *url, const LND_HTTP_RESPONSE *r) {
    if (!s->options.cache.max_bytes) return false;
    uint64_t size = r->range ? r->total_length_bytes : r->content_length_bytes;
    bool known = r->range || r->length_known;
    if (s->cache && r->etag[0] == '"' && !strcmp(r->etag, s->cache->etag) && !strcmp(url, s->cache->url) && known && s->cache->known && size == s->cache->size)
        return true;
    lnd_http_cache_free(s);
    if (s->info.live || r->icy_interval_bytes || (r->status != 200 && r->status != 206) || (known && size > INT64_MAX)) return false;
    size_t bytes = sizeof(lnd_http_cache) + strlen(url) + 1;
    if (bytes >= s->options.cache.max_bytes) return false;
    lnd_http_cache *c = lnd_alloc_zero(bytes);
    if (!c) return false;
    c->limit = s->options.cache.max_bytes;
    c->allocated = bytes;
    c->ranges = r->accepts_ranges || r->range;
    c->known = known;
    c->size = size;
    c->chunk = 16384;
    while (c->chunk > 1 && lnd_cache_cost(c->chunk) > c->limit - bytes) c->chunk /= 2;
    memcpy(c->etag, r->etag, sizeof c->etag);
    strcpy(c->url, url);
    s->cache = c;
    return false;
}

uint64_t lnd_http_cache_revision(const lnd_http_cache *c) { return c ? c->revision : 0; }
uint64_t lnd_http_cache_size(const lnd_http_cache *c) { return c ? c->size : 0; }
size_t lnd_http_cache_bytes(const lnd_http_cache *c) { return c ? c->allocated : 0; }
bool lnd_http_cache_complete(const lnd_http_cache *c) { return c && c->complete; }

bool lnd_http_cache_fits(const lnd_http_cache *c) {
    if (!c || !c->known || c->size > c->limit) return false;
    uint64_t blocks = c->size / c->chunk;
    size_t tail = (size_t)(c->size % c->chunk);
    size_t root = sizeof *c + strlen(c->url) + 1 + 2 * sizeof(lnd_io);
    if (root > c->limit) return false;
    size_t remaining = c->limit - root;
    if (tail) {
        if (lnd_cache_cost(tail) > remaining) return false;
        remaining -= lnd_cache_cost(tail);
    }
    return blocks <= remaining / lnd_cache_cost(c->chunk);
}

void lnd_http_cache_protect(lnd_http_cache *c, uint64_t start, uint64_t end) {
    if (!c) return;
    c->protect_start = start;
    c->protect_end = end;
}

static lnd_cache_block *lnd_cache_find(const lnd_http_cache *c, uint64_t offset) {
    if (c->cursor && offset >= c->cursor->offset && offset - c->cursor->offset < c->cursor->capacity) return c->cursor;
    lnd_cache_block *start = c->cursor && c->cursor->offset <= offset ? c->cursor : c->blocks;
    for (lnd_cache_block *block = start; block && block->offset <= offset; block = block->next)
        if (offset - block->offset < block->capacity) return block;
    return nullptr;
}

size_t lnd_http_cache_reserve(lnd_http_cache *c, uint64_t offset) {
    if (!c || (c->known && offset >= c->size)) return 0;
    lnd_cache_block *block = lnd_cache_find(c, offset);
    if (!block) {
        uint64_t base = offset / c->chunk * c->chunk;
        size_t capacity = c->known ? (size_t)LND_MIN((uint64_t)c->chunk, c->size - base) : c->chunk;
        size_t bytes = lnd_cache_cost(capacity);
        size_t reserved = (2 - c->views) * sizeof(lnd_io);
        while (reserved > c->limit - c->allocated || bytes > c->limit - c->allocated - reserved) {
            lnd_cache_block **oldest = nullptr;
            for (lnd_cache_block **p = &c->blocks; *p; p = &(*p)->next) {
                lnd_cache_block *b = *p;
                bool protected = b->offset < c->protect_end && c->protect_start < b->offset + b->capacity;
                if (b->offset && !protected && (!oldest || b->touched < (*oldest)->touched)) oldest = p;
            }
            if (!oldest || c->complete) return 0;
            lnd_cache_block *b = *oldest;
            *oldest = b->next;
            c->allocated -= lnd_cache_cost(b->capacity);
            c->retained -= b->filled;
            c->revision++;
            if (c->cursor == b) c->cursor = nullptr;
            lnd_free(b);
        }
        block = lnd_alloc_zero(bytes);
        if (!block) return 0;
        block->offset = base;
        block->capacity = capacity;
        lnd_cache_block **p = &c->blocks;
        while (*p && (*p)->offset < base) p = &(*p)->next;
        block->next = *p;
        *p = block;
        c->allocated += bytes;
    }
    c->cursor = block;
    block->touched = ++c->clock;
    return block->capacity - (size_t)(offset - block->offset);
}

bool lnd_http_cache_put(lnd_http_cache *c, uint64_t offset, const void *data, size_t bytes) {
    if (c && c->complete) return lnd_http_cache_has(c, offset, bytes);
    const uint8_t *src = data;
    while (bytes) {
        size_t available = lnd_http_cache_reserve(c, offset);
        if (!available) return false;
        lnd_cache_block *block = c->cursor;
        size_t at = (size_t)(offset - block->offset), take = LND_MIN(bytes, available);
        memcpy(lnd_cache_data(block) + at, src, take);
        for (size_t i = at; i < at + take;) {
            unsigned count = (unsigned)LND_MIN((size_t)(8 - i % 8), at + take - i);
            uint8_t mask = (uint8_t)(((1u << count) - 1) << (i % 8));
            uint8_t fresh = (uint8_t)(mask & ~block->data[i / 8]);
            block->data[i / 8] |= mask;
            unsigned added = fresh == 255 ? 8 : 0;
            if (fresh != 255)
                for (; fresh; fresh &= (uint8_t)(fresh - 1)) added++;
            block->filled += added;
            c->retained += added;
            i += count;
        }
        offset += take;
        src += take;
        bytes -= take;
    }
    return true;
}

static size_t lnd_cache_valid(const lnd_cache_block *block, size_t at, size_t bytes) {
    if (block->filled == block->capacity) return bytes;
    size_t valid = 0;
    while (valid < bytes && (at + valid) % 8 && (block->data[(at + valid) / 8] & (1u << ((at + valid) % 8)))) valid++;
    if ((at + valid) % 8) return valid;
    while (bytes - valid >= 8 && block->data[(at + valid) / 8] == 255) valid += 8;
    while (valid < bytes && (block->data[(at + valid) / 8] & (1u << ((at + valid) % 8)))) valid++;
    return valid;
}

size_t lnd_http_cache_read(lnd_http_cache *c, uint64_t offset, void *data, size_t bytes) {
    if (!c) return 0;
    size_t done = 0;
    while (done < bytes) {
        lnd_cache_block *block = lnd_cache_find(c, offset);
        if (!block) break;
        size_t at = (size_t)(offset - block->offset), take = LND_MIN(bytes - done, block->capacity - at), valid = 0;
        valid = lnd_cache_valid(block, at, take);
        if (!valid) break;
        memcpy((uint8_t *)data + done, lnd_cache_data(block) + at, valid);
        c->cursor = block;
        block->touched = ++c->clock;
        done += valid;
        offset += valid;
        if (valid != take) break;
    }
    return done;
}

bool lnd_http_cache_has(lnd_http_cache *c, uint64_t offset, size_t bytes) {
    if (!c) return false;
    while (bytes) {
        lnd_cache_block *block = lnd_cache_find(c, offset);
        if (!block) return false;
        size_t at = (size_t)(offset - block->offset), take = LND_MIN(bytes, block->capacity - at);
        if (lnd_cache_valid(block, at, take) != take) return false;
        c->cursor = block;
        offset += take;
        bytes -= take;
    }
    return true;
}

void lnd_http_cache_end(lnd_http_cache *c, uint64_t size) {
    if (!c || (c->known && c->size != size)) return;
    c->size = size;
    c->known = true;
    c->complete = c->retained == size;
}

static int64_t lnd_cache_io_read(void *state, uint64_t offset, void *data, size_t bytes) {
    lnd_http_cache *c = state;
    if (offset >= c->size) return 0;
    size_t take = lnd_http_cache_read(c, offset, data, (size_t)LND_MIN((uint64_t)bytes, c->size - offset));
    return take ? (int64_t)take : LND_ERR_IO;
}

static int32_t lnd_cache_io_close(void *state, bool borrowed) {
    lnd_http_cache *c = state;
    c->views--;
    c->allocated -= sizeof(lnd_io);
    return LND_OK;
}
static const lnd_io_vt lnd_cache_io_vt = {.read_at = lnd_cache_io_read, .close = lnd_cache_io_close};

lnd_io *lnd_http_cache_io(lnd_http_cache *c) {
    if (!c || !c->complete || c->views == 2 || sizeof(lnd_io) > c->limit - c->allocated) return nullptr;
    lnd_io *io = lnd_alloc_zero(sizeof *io);
    if (io) {
        c->views++;
        c->allocated += sizeof *io;
        *io = (lnd_io){.vt = &lnd_cache_io_vt, .state = c, .size = c->size, .seekable = true, .references = 1};
    }
    return io;
}

static uint64_t lnd_cache_rate(long double value) { return value >= (long double)UINT64_MAX ? UINT64_MAX : (uint64_t)value; }

static int64_t lnd_cache_ogg_time(lnd_http_cache *c) {
    uint8_t header[282];
    while (lnd_http_cache_read(c, c->ogg_offset, header, 27) == 27) {
        if (memcmp(header, "OggS", 4) || header[4]) return -1;
        size_t head = 27 + header[26], bytes = head;
        if (lnd_http_cache_read(c, c->ogg_offset + 27, header + 27, head - 27) != head - 27) break;
        for (size_t i = 27; i < head; i++) bytes += header[i];
        if (!lnd_http_cache_has(c, c->ogg_offset, bytes)) break;
        uint32_t serial = 0;
        for (unsigned i = 0; i < 4; i++) serial |= (uint32_t)header[14 + i] << (i * 8);
        if (!(header[5] & 2) && serial != c->ogg_serial) return -1;
        if (header[5] & 2) {
            c->ogg_serial = serial;
            uint8_t ident[19];
            c->ogg_base_us = c->ogg_time_us;
            c->ogg_rate = c->ogg_skip = 0;
            if (bytes - head >= sizeof ident && lnd_http_cache_read(c, c->ogg_offset + head, ident, sizeof ident) == sizeof ident) {
                if (!memcmp(ident, "OpusHead", 8)) {
                    c->ogg_rate = 48000;
                    c->ogg_skip = (uint32_t)ident[10] | (uint32_t)ident[11] << 8;
                } else if (ident[0] == 1 && !memcmp(ident + 1, "vorbis", 6)) {
                    for (unsigned i = 0; i < 4; i++) c->ogg_rate |= (uint32_t)ident[12 + i] << (i * 8);
                }
            }
        }
        if (c->ogg_rate > 768000) return -1;
        uint64_t granule = 0;
        for (unsigned i = 0; i < 8; i++) granule |= (uint64_t)header[6 + i] << (i * 8);
        if (c->ogg_rate && granule <= INT64_MAX && granule >= c->ogg_skip) {
            long double time = c->ogg_base_us + (long double)(granule - c->ogg_skip) * 1000000 / c->ogg_rate;
            c->ogg_time_us = time >= (long double)INT64_MAX ? INT64_MAX : (int64_t)time;
        }
        c->ogg_offset += bytes;
    }
    return c->ogg_rate && c->ogg_time_us > 0 ? c->ogg_time_us : -1;
}

bool lnd_http_cache_prefetch(lnd_http_session *s, uint64_t unread) {

    if (!s->cache || lnd_http_cache_complete(s->cache)) return false;
    if (!s->options.cache.ahead_ms) return true;
    if (!s->info.sample_rate_hz) return false;
    if (!strcmp(s->info.codec, "opus") || !strcmp(s->info.codec, "vorbis")) {
        int64_t end = lnd_cache_ogg_time(s->cache);
        if (end >= 0) {
            uint64_t played = lnd_load(&s->played);
            long double position = s->seek_commit ? s->seek_target_us : (long double)played * 1000000 / s->info.sample_rate_hz;
            return (long double)end - position < (long double)s->options.cache.ahead_ms * 1000;
        }
        return false;
    }
    uint64_t rate = s->info.bitrate_bps;

    if (!rate && s->info.duration_us > 0 && s->response.length_known)
        rate = lnd_cache_rate((long double)s->response.content_length_bytes * 8000000 / s->info.duration_us);
    uint64_t consumed = lnd_decoder_consumed(s->decoder);
    if (!rate && consumed && s->stats.decoded_frames) rate = lnd_cache_rate((long double)consumed * s->info.sample_rate_hz * 8 / s->stats.decoded_frames);
    if (!rate) return false;
    lnd_spinlock_lock(&s->pcm_lock);
    uint64_t pcm_ms = (uint64_t)s->count * 1000 / s->info.sample_rate_hz;
    lnd_spinlock_unlock(&s->pcm_lock);
    return pcm_ms < s->options.cache.ahead_ms && (long double)unread * 8000 < (long double)(s->options.cache.ahead_ms - pcm_ms) * rate;
}

void lnd_http_cache_stop(lnd_http_session *s) {
    if (!s->cache || !s->cache->transfer) return;
    lnd_callback_enter();
    s->transport->close(s->cache->transfer);
    lnd_callback_leave();
    s->cache->transfer = nullptr;
}

bool lnd_http_cache_pending(const lnd_http_session *s) {
    return s->cache && s->cache->ranges && s->cache->etag[0] == '"' && !s->cache->complete && !s->options.cache.ahead_ms && lnd_http_cache_fits(s->cache);
}

int32_t lnd_http_cache_step(lnd_http_session *s, uint64_t offset, uint64_t bytes) {
    lnd_http_cache *c = s->cache;
    if (!c || !c->ranges || c->etag[0] != '"' || !c->known || c->complete || offset >= c->size) return LND_HTTP_PENDING;
    if (!c->transfer) {
        uint64_t end = offset + LND_MIN(bytes, c->size - offset);
        offset = offset / c->chunk * c->chunk;
        while (offset < end) {
            size_t take = (size_t)LND_MIN((uint64_t)c->chunk, c->size - offset);
            if (!lnd_http_cache_has(c, offset, take)) break;
            offset += take;
        }
        if (offset >= end) return LND_HTTP_PENDING;
        size_t take = lnd_http_cache_reserve(c, offset);
        if (!take) return LND_HTTP_PENDING;
        if (lnd_http_cache_pending(s)) {
            size_t target = (size_t)LND_MIN(UINT64_C(65536), c->size - offset);
            while (take < target) {
                size_t next = lnd_http_cache_reserve(c, offset + take);
                if (!next) break;
                take += next;
            }
        }
        c->requested = offset;
        c->request_bytes = take;
        c->received = 0;
        c->last_data = s->now;
        return lnd_http_request_open(s, c->url, c->etag, true, offset, take, &c->transfer);
    }
    uint8_t data[16384];
    size_t written = 0;
    LND_HTTP_RESPONSE response = {0};
    lnd_callback_enter();
    int32_t result = s->transport->poll(c->transfer, &response, data, sizeof data, &written);
    lnd_callback_leave();
    bool valid = response.headers_complete && response.status == 206 && response.range && !strcmp(response.etag, c->etag) &&
                 response.range_start_bytes == c->requested && response.total_length_bytes == c->size &&
                 (!response.length_known || response.content_length_bytes == c->request_bytes);
    if (written > sizeof data || written > c->request_bytes - c->received || (response.headers_complete && !valid) || (written && !valid) || result < 0 ||
        (result == LND_HTTP_DONE && (!valid || c->received + written != c->request_bytes))) {
        lnd_http_cache_stop(s);
        return LND_ERR_UNSUPPORTED;
    }
    if (written) {
        if (!lnd_http_cache_put(c, c->requested + c->received, data, written)) {
            lnd_http_cache_stop(s);
            return LND_HTTP_PENDING;
        }
        c->received += written;
        s->stats.received_bytes += written;
        c->last_data = s->now;
    }
    if (result == LND_HTTP_DONE) {
        lnd_http_cache_stop(s);
        lnd_http_cache_end(c, c->size);
        return LND_OK;
    }
    uint32_t timeout = response.headers_complete ? s->options.retry.receive_timeout_ms : s->options.retry.connect_timeout_ms;
    if (timeout && s->now - c->last_data >= timeout) {
        lnd_http_cache_stop(s);
        return LND_ERR_UNSUPPORTED;
    }
    return written ? LND_OK : LND_HTTP_PENDING;
}
