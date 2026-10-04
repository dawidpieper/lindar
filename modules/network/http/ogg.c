#include "ogg.h"
#include "formats/ogg/duration.h"

#include <string.h>

typedef struct lnd_ogg_range {
    struct lnd_ogg_range *next;
    uint64_t offset;
    size_t size;
    size_t capacity;
    uint8_t data[];
} lnd_ogg_range;

struct lnd_http_ogg {
    char *url;
    char etag[256];
    void *transfer;
    lnd_ogg_range *ranges;
    lnd_ogg_range *pending;
    lnd_io io;
    uint64_t missing;
    uint64_t requested;
    uint64_t last_data;
    size_t chunk;
    size_t allocated;
    size_t limit;
    bool miss;
    bool done;
};

static void lnd_ogg_http_close(lnd_http_session *s, lnd_http_ogg *ogg) {
    if (ogg->transfer) {
        lnd_callback_enter();
        s->transport->close(ogg->transfer);
        lnd_callback_leave();
    }
    ogg->transfer = nullptr;
    ogg->pending = nullptr;
}

static void lnd_ogg_http_finish(lnd_http_session *s, lnd_http_ogg *ogg) {
    lnd_ogg_http_close(s, ogg);
    while (ogg->ranges) {
        lnd_ogg_range *range = ogg->ranges;
        ogg->ranges = range->next;
        lnd_free(range);
    }
    ogg->allocated = 0;
    ogg->done = true;
}

static lnd_ogg_range *lnd_ogg_http_range(lnd_http_ogg *ogg, uint64_t offset) {
    for (lnd_ogg_range *range = ogg->ranges; range; range = range->next)
        if (range->offset == offset) return range;
    size_t capacity = (size_t)LND_MIN((uint64_t)ogg->chunk, ogg->io.size - offset);
    if (capacity > ogg->limit - ogg->allocated || capacity > SIZE_MAX - sizeof(lnd_ogg_range)) return nullptr;
    lnd_ogg_range *range = lnd_alloc(sizeof *range + capacity);
    if (!range) return nullptr;
    *range = (lnd_ogg_range){.next = ogg->ranges, .offset = offset, .capacity = capacity};
    ogg->ranges = range;
    ogg->allocated += capacity;
    return range;
}

static int64_t lnd_ogg_http_read(void *state, uint64_t position, void *dst, size_t bytes) {
    lnd_http_ogg *ogg = state;
    if (ogg->miss) return LND_ERR_IO;
    uint64_t offset = position / ogg->chunk * ogg->chunk;
    for (lnd_ogg_range *range = ogg->ranges; range; range = range->next) {
        if (range->offset != offset || position - offset >= range->size) continue;
        size_t take = LND_MIN(bytes, range->size - (size_t)(position - offset));
        memcpy(dst, range->data + (size_t)(position - offset), take);
        return (int64_t)take;
    }
    /* Restart the metadata reader once the missing range has arrived. */
    ogg->missing = offset;
    ogg->miss = true;
    return LND_ERR_IO;
}

static const lnd_io_vt lnd_ogg_http_io = {.read_at = lnd_ogg_http_read};

lnd_http_ogg *lnd_http_ogg_begin(lnd_http_session *s, const uint8_t *data, size_t bytes) {
    if (!s->probe_duration || s->info.live || !s->response.length_known || !s->response.accepts_ranges || s->response.etag[0] != '"' ||
        s->response.icy_interval_bytes || s->response.content_length_bytes > INT64_MAX || !s->response.content_length_bytes)
        return nullptr;
#if !LND_MODULE_OPUS_DECODER && !LND_MODULE_VORBIS_DECODER
    return nullptr;
#endif
    lnd_http_ogg *ogg = lnd_alloc_zero(sizeof *ogg);
    if (!ogg) return nullptr;
    ogg->url = lnd_http_copy(s->request_url);
    ogg->chunk = LND_MIN((size_t)65536, s->options.buffer.segment_bytes);
    ogg->limit = s->options.buffer.segment_bytes;
    ogg->io = (lnd_io){.vt = &lnd_ogg_http_io, .state = ogg, .size = s->response.content_length_bytes, .seekable = true};
    memcpy(ogg->etag, s->response.etag, sizeof ogg->etag);
    lnd_ogg_range *range = lnd_ogg_http_range(ogg, 0);
    if (!ogg->url || !range) {
        lnd_http_ogg_free(s, ogg);
        return nullptr;
    }
    range->size = LND_MIN(bytes, range->capacity);
    memcpy(range->data, data, range->size);
    return ogg;
}

bool lnd_http_ogg_matches(const lnd_http_session *s, const lnd_http_ogg *ogg) {
    return s->response.length_known && !s->info.live && !strcmp(s->request_url, ogg->url) && !strcmp(s->response.etag, ogg->etag) &&
           (s->response.range ? s->response.total_length_bytes : s->response.content_length_bytes) == ogg->io.size;
}

static int32_t lnd_ogg_http_duration(lnd_http_ogg *ogg, int64_t *duration_us) {
    int32_t result = LND_ERR_UNSUPPORTED;
    ogg->io.pos = 0;
    ogg->io.status = LND_OK;
    ogg->miss = false;
    lnd_callback_enter();
#if LND_MODULE_OPUS_DECODER
    result = lnd_opus_duration(&ogg->io, duration_us);
#endif
#if LND_MODULE_VORBIS_DECODER
    if (result != LND_OK && !ogg->miss) {
        ogg->io.pos = 0;
        ogg->io.status = LND_OK;
        result = lnd_vorbis_duration(&ogg->io, duration_us);
    }
#endif
    lnd_callback_leave();
    return result;
}

bool lnd_http_ogg_step(lnd_http_session *s, lnd_http_ogg *ogg) {
    if (ogg->done) return false;
    if (!ogg->transfer) {
        int64_t duration;
        int32_t result = lnd_ogg_http_duration(ogg, &duration);
        if (!ogg->miss) {
            if (result == LND_OK) {
                s->info.duration_us = s->info.seek_end_us = duration;
                s->info.length_kind = LND_LENGTH_EXACT;
            }
            lnd_ogg_http_finish(s, ogg);
            return true;
        }
        ogg->pending = lnd_ogg_http_range(ogg, ogg->missing);
        if (!ogg->pending) {
            lnd_ogg_http_finish(s, ogg);
            return true;
        }
        ogg->requested = ogg->pending->offset + ogg->pending->size;
        result = lnd_http_request_open(s, ogg->url, ogg->etag, true, ogg->requested, ogg->pending->capacity - ogg->pending->size, &ogg->transfer);
        ogg->last_data = s->now;
        if (result != LND_OK) lnd_ogg_http_finish(s, ogg);
        return true;
    }
    uint8_t data[16384];
    size_t written = 0;
    LND_HTTP_RESPONSE response = {0};
    lnd_callback_enter();
    int32_t result = s->transport->poll(ogg->transfer, &response, data, sizeof data, &written);
    lnd_callback_leave();
    lnd_ogg_range *range = ogg->pending;
    bool valid = response.headers_complete && response.status == 206 && response.range && !strcmp(response.etag, ogg->etag) &&
                 response.range_start_bytes == ogg->requested && response.total_length_bytes == ogg->io.size &&
                 (!response.length_known || response.content_length_bytes == range->offset + range->capacity - ogg->requested);
    if (result < 0 || written > sizeof data || written > range->capacity - range->size || (response.headers_complete && !valid) || (written && !valid) ||
        (result == LND_HTTP_DONE && !valid)) {
        lnd_ogg_http_finish(s, ogg);
        return true;
    }
    if (written) {
        memcpy(range->data + range->size, data, written);
        range->size += written;
        s->stats.received_bytes += written;
        ogg->last_data = s->now;
    }
    if (result == LND_HTTP_DONE) {
        if (range->size == range->capacity)
            lnd_ogg_http_close(s, ogg);
        else
            lnd_ogg_http_finish(s, ogg);
    } else {
        uint32_t timeout = response.headers_complete ? s->options.retry.receive_timeout_ms : s->options.retry.connect_timeout_ms;
        if (timeout && s->now - ogg->last_data >= timeout) lnd_ogg_http_finish(s, ogg);
    }
    return true;
}

size_t lnd_http_ogg_buffered(const lnd_http_ogg *ogg) {
    size_t bytes = 0;
    if (ogg)
        for (lnd_ogg_range *range = ogg->ranges; range; range = range->next) bytes += range->size;
    return bytes;
}

void lnd_http_ogg_stop(lnd_http_session *s, lnd_http_ogg *ogg) {
    if (ogg) lnd_ogg_http_finish(s, ogg);
}

void lnd_http_ogg_free(lnd_http_session *s, lnd_http_ogg *ogg) {
    if (!ogg) return;
    lnd_ogg_http_finish(s, ogg);
    lnd_free(ogg->url);
    lnd_free(ogg);
}
