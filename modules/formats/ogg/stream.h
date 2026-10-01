#pragma once

#include <ogg/ogg.h>

typedef struct lnd_ogg_stream {
    ogg_sync_state sync;
    ogg_stream_state stream;
    int64_t granule;
    uint32_t packets;
    bool page_eos;
    bool opened;
    bool eos;
    bool chain;
} lnd_ogg_stream;

static void lnd_ogg_stream_init(lnd_ogg_stream *s) { ogg_sync_init(&s->sync); }
static void lnd_ogg_stream_clear(lnd_ogg_stream *s) {
    if (s->opened) ogg_stream_clear(&s->stream);
    ogg_sync_clear(&s->sync);
}

static int32_t lnd_ogg_stream_feed(lnd_ogg_stream *s, const uint8_t *data, size_t bytes, bool end, size_t *used) {
    ogg_page page;
    int result = ogg_sync_pageout(&s->sync, &page);
    if (result < 0) return LND_ERR_FORMAT;
    if (!result && bytes) {
        size_t take = LND_MIN(bytes, (size_t)4096);
        char *buffer = ogg_sync_buffer(&s->sync, (long)take);
        if (!buffer) return LND_ERR_OUT_OF_MEMORY;
        memcpy(buffer, data, take);
        if (ogg_sync_wrote(&s->sync, (long)take)) return LND_ERR_FORMAT;
        *used = take;
        result = ogg_sync_pageout(&s->sync, &page);
        if (result < 0) return LND_ERR_FORMAT;
    }
    if (!result) {
        if (!bytes && end) return s->eos && s->sync.fill == s->sync.returned ? LND_SOURCE_EOF : LND_ERR_FORMAT;
        return LND_SOURCE_WAITING;
    }
    int serial = ogg_page_serialno(&page);
    s->chain = false;
    if (!s->opened || serial != s->stream.serialno) {
        if (!ogg_page_bos(&page) || (s->opened && !s->eos)) return LND_ERR_UNSUPPORTED;
        if (s->opened) ogg_stream_clear(&s->stream);
        if (ogg_stream_init(&s->stream, serial)) return LND_ERR_OUT_OF_MEMORY;
        s->opened = true;
        s->chain = true;
        s->eos = false;
    }
    if (ogg_stream_pagein(&s->stream, &page)) return LND_ERR_FORMAT;
    if (s->stream.body_fill - s->stream.body_returned > 1048576) return LND_ERR_FORMAT;
    s->granule = ogg_page_granulepos(&page);
    s->packets = (uint32_t)ogg_page_packets(&page);
    s->page_eos = ogg_page_eos(&page) != 0;
    if (s->page_eos) s->eos = true;

    return LND_SOURCE_READY;
}
