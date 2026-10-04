#include "ogg.h"
#include "formats/ogg/duration.h"
#include "formats/decode/metadata.h"
#if LND_MODULE_OPUS_DECODER
#include "formats/opus/reader.h"
#endif

#include <string.h>

typedef struct lnd_ogg_range {
    struct lnd_ogg_range *next;
    uint64_t offset;
    size_t size;
    size_t capacity;
    uint64_t touched;
    bool pinned;
    uint8_t data[];
} lnd_ogg_range;

struct lnd_http_ogg {
    char *url;
    char etag[256];
    void *transfer;
    lnd_ogg_range *ranges;
    lnd_ogg_range *pending;
    lnd_io io;
    lnd_http_session *session;
#if LND_MODULE_OPUS_DECODER
    OggOpusFile *reader;
#endif
    lnd_decode_tags tags;
    void *pcm;
    uint64_t frame;
    uint64_t epoch;
    uint32_t channels;
    int link;
    lnd_http_bytes *body;
    uint64_t missing;
    uint64_t requested;
    uint64_t last_data;
    size_t chunk;
    size_t allocated;
    size_t limit;
    bool playing;
    bool opening;
    bool attached;
    bool failed;
    bool complete;
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
    if (capacity > SIZE_MAX - sizeof(lnd_ogg_range)) return nullptr;
    while (capacity > ogg->limit - ogg->allocated) {
        lnd_ogg_range **oldest = nullptr;
        for (lnd_ogg_range **p = &ogg->ranges; *p; p = &(*p)->next)
            if (!(*p)->pinned && (*p)->touched < ogg->epoch && (!oldest || (*p)->touched < (*oldest)->touched)) oldest = p;
        if (!ogg->playing || !oldest) return nullptr;
        lnd_ogg_range *range = *oldest;
        *oldest = range->next;
        ogg->allocated -= range->capacity;
        lnd_free(range);
    }
    lnd_ogg_range *range = lnd_alloc(sizeof *range + capacity);
    if (!range) return nullptr;
    *range = (lnd_ogg_range){.next = ogg->ranges, .offset = offset, .capacity = capacity};
    ogg->ranges = range;
    ogg->allocated += capacity;
    return range;
}

static int64_t lnd_ogg_http_read(void *state, uint64_t position, void *dst, size_t bytes) {
    lnd_http_ogg *ogg = state;
    if (ogg->body) {
        if (position >= ogg->body->size) return 0;
        size_t take = LND_MIN(bytes, ogg->body->size - (size_t)position);
        memcpy(dst, ogg->body->data + (size_t)position, take);
        return (int64_t)take;
    }
    if (position >= ogg->io.size) return 0;
    if (ogg->miss) return LND_ERR_IO;
    uint64_t offset = position / ogg->chunk * ogg->chunk;
    for (lnd_ogg_range *range = ogg->ranges; range; range = range->next) {
        if (range->offset != offset || position - offset >= range->size) continue;
        range->touched = ogg->epoch;
        range->pinned |= ogg->opening;
        size_t take = LND_MIN(bytes, range->size - (size_t)(position - offset));
        memcpy(dst, range->data + (size_t)(position - offset), take);
        return (int64_t)take;
    }
    /* Opening and seeking are retried after a cache miss; reads retain decoder state. */
    ogg->missing = offset;
    ogg->miss = true;
    return LND_ERR_IO;
}

static const lnd_io_vt lnd_ogg_http_io = {.read_at = lnd_ogg_http_read};

lnd_http_ogg *lnd_http_ogg_begin(lnd_http_session *s, const uint8_t *data, size_t bytes) {
    if (s->info.live || s->response.status != 200 || s->response.range || s->response.icy_interval_bytes ||
        (s->response.length_known && (!s->response.content_length_bytes || s->response.content_length_bytes > INT64_MAX)))
        return nullptr;
    bool full = !s->response.length_known || !s->response.accepts_ranges || s->response.etag[0] != '"';
    if (full && !s->probe_duration) return nullptr;
    if (full && s->response.length_known && s->response.content_length_bytes > s->options.buffer.segment_bytes) return nullptr;
#if !LND_MODULE_OPUS_DECODER && !LND_MODULE_VORBIS_DECODER
    return nullptr;
#endif
    lnd_http_ogg *ogg = lnd_alloc_zero(sizeof *ogg);
    if (!ogg) return nullptr;
    ogg->done = !s->probe_duration;
    ogg->epoch = 1;
    ogg->link = -1;
    ogg->url = lnd_http_copy(s->request_url);
    ogg->chunk = LND_MIN((size_t)65536, s->options.buffer.segment_bytes);
    ogg->limit = s->options.buffer.segment_bytes;
    ogg->io = (lnd_io){.vt = &lnd_ogg_http_io, .state = ogg, .size = s->response.content_length_bytes, .seekable = true};
    memcpy(ogg->etag, s->response.etag, sizeof ogg->etag);
    if (full) ogg->body = &s->input;
    lnd_ogg_range *range = full ? nullptr : lnd_ogg_http_range(ogg, 0);
    if (!ogg->url || (!full && !range)) {
        lnd_http_ogg_free(s, ogg);
        return nullptr;
    }
    if (range) {
        range->size = LND_MIN(bytes, range->capacity);
        memcpy(range->data, data, range->size);
    }
    return ogg;
}

bool lnd_http_ogg_matches(const lnd_http_session *s, const lnd_http_ogg *ogg) {
    return ogg->etag[0] == '"' && (!ogg->body || ogg->done) && s->response.length_known && !s->info.live && s->request_url &&
           !strcmp(s->request_url, ogg->url) && !strcmp(s->response.etag, ogg->etag) &&
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

bool lnd_http_ogg_collecting(const lnd_http_ogg *ogg) { return ogg && ogg->body && !ogg->done; }

void lnd_http_ogg_end(lnd_http_ogg *ogg) {
    if (!lnd_http_ogg_collecting(ogg)) return;
    ogg->io.size = ogg->body->size;
    ogg->complete = true;
}

static int32_t lnd_ogg_http_fetch(lnd_http_session *s, lnd_http_ogg *ogg) {
    if (!ogg->transfer) {
        ogg->pending = lnd_ogg_http_range(ogg, ogg->missing);
        if (!ogg->pending) return LND_ERR_UNSUPPORTED;
        ogg->requested = ogg->pending->offset + ogg->pending->size;
        int32_t result = lnd_http_request_open(s, ogg->url, ogg->etag, true, ogg->requested, ogg->pending->capacity - ogg->pending->size, &ogg->transfer);
        ogg->last_data = s->now;
        return result;
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
        (result == LND_HTTP_DONE && !valid))
        return LND_ERR_UNSUPPORTED;
    if (written) {
        memcpy(range->data + range->size, data, written);
        range->size += written;
        s->stats.received_bytes += written;
        ogg->last_data = s->now;
    }
    if (result == LND_HTTP_DONE) {
        if (range->size != range->capacity) return LND_ERR_UNSUPPORTED;
        lnd_ogg_http_close(s, ogg);
        ogg->miss = false;
        ogg->io.status = LND_OK;
        return LND_OK;
    }
    uint32_t timeout = response.headers_complete ? s->options.retry.receive_timeout_ms : s->options.retry.connect_timeout_ms;
    if (timeout && s->now - ogg->last_data >= timeout) return LND_ERR_UNSUPPORTED;
    return written ? LND_OK : LND_HTTP_PENDING;
}

bool lnd_http_ogg_step(lnd_http_session *s, lnd_http_ogg *ogg) {
    if (ogg->done || (ogg->body && !ogg->complete)) return false;
    if (!ogg->transfer) {
        int64_t duration;
        int32_t result = lnd_ogg_http_duration(ogg, &duration);
        if (!ogg->miss) {
            if (result == LND_OK) {
                s->info.duration_us = s->info.seek_end_us = duration;
                s->info.length_kind = LND_LENGTH_EXACT;
            }
            if (result != LND_OK || ogg->body)
                lnd_ogg_http_finish(s, ogg);
            else
                ogg->done = true;
            return true;
        }
    }
    int32_t result = lnd_ogg_http_fetch(s, ogg);
    if (result < 0) lnd_ogg_http_finish(s, ogg);
    return result != LND_HTTP_PENDING;
}

#if LND_MODULE_OPUS_DECODER
static void lnd_ogg_reader_close(void *state) {
    lnd_http_ogg *ogg = state;
    if (ogg->reader) op_free(ogg->reader);
    ogg->reader = nullptr;
    lnd_free(ogg->pcm);
    ogg->pcm = nullptr;
    ogg->playing = ogg->attached = false;
}

static void lnd_ogg_reader_tags(lnd_http_ogg *ogg, int link) {
    if (link == ogg->link) return;
    ogg->link = link;
#if LND_MODULE_METADATA_COMMENTS
    const OpusTags *tags = op_tags(ogg->reader, link);
    lnd_tag_buffer data = {.limit = 32u * 1024 * 1024};
    lnd_tag_append(&data, "OpusTags", 8);
    lnd_tag_u32(&data, (uint32_t)strlen(tags->vendor), false);
    lnd_tag_append(&data, tags->vendor, strlen(tags->vendor));
    lnd_tag_u32(&data, (uint32_t)tags->comments, false);
    for (int i = 0; i < tags->comments; i++) {
        lnd_tag_u32(&data, (uint32_t)tags->comment_lengths[i], false);
        lnd_tag_append(&data, tags->user_comments[i], (size_t)tags->comment_lengths[i]);
    }
    if (!data.error) lnd_decode_tags_read(&ogg->tags, data.data, data.size, LND_METADATA_OPUS);
    lnd_free(data.data);
#endif
}

static int32_t lnd_ogg_reader_metadata(void *state, LND_METADATA *metadata, uint64_t *revision) {
    return lnd_decode_tags_get(&((lnd_http_ogg *)state)->tags, metadata, revision);
}

static int32_t lnd_ogg_reader_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    lnd_http_ogg *ogg = state;
    *pcm = (LND_PCM){0};
    if (ogg->miss || ogg->failed) return LND_SOURCE_WAITING;
    if (!ogg->reader) {
        ogg->io.pos = 0;
        ogg->io.status = LND_OK;
        ogg->opening = true;
        ogg->reader = lnd_opus_open_io(&ogg->io);
        if (ogg->reader) {
            uint64_t position = ogg->io.pos;
            int64_t duration;
            int32_t result = lnd_opus_io_duration(&ogg->io, ogg->reader, &duration);
            if (result != LND_OK) {
                op_free(ogg->reader);
                ogg->reader = nullptr;
            } else {
                ogg->io.pos = position;
                ogg->io.status = LND_OK;
            }
        }
        ogg->opening = false;
        if (ogg->reader) {
            ogg->channels = 0;
            for (int link = 0; link < op_link_count(ogg->reader); link++) {
                int channels = op_channel_count(ogg->reader, link);
                if (channels < 1 || channels > LND_MAX_CHANNELS) {
                    ogg->failed = true;
                    return LND_SOURCE_WAITING;
                }
                ogg->channels = LND_MAX(ogg->channels, (uint32_t)channels);
            }
            info->length_frames = (uint64_t)op_pcm_total(ogg->reader, -1);
            info->length_known = info->seekable = true;
            if (ogg->frame >= info->length_frames) {
                ogg->session->input_end = true;
                return LND_SOURCE_EOF;
            }
            if (!ogg->pcm) ogg->pcm = lnd_alloc((size_t)5760 * ogg->channels * LND_PcmGetSampleBytes(info->format));
            if (!ogg->pcm) {
                ogg->failed = true;
                return LND_SOURCE_WAITING;
            }
            if (op_pcm_seek(ogg->reader, (ogg_int64_t)ogg->frame)) {
                op_free(ogg->reader);
                ogg->reader = nullptr;
            }
        }
        if (!ogg->reader) {
            ogg->failed = !ogg->miss;
            return LND_SOURCE_WAITING;
        }
    }
    int link = -1;
#if LND_OPUS_INTEGER
    int got = op_read(ogg->reader, ogg->pcm, (int)(5760 * ogg->channels), &link);
#else
    int got = op_read_float(ogg->reader, ogg->pcm, (int)(5760 * ogg->channels), &link);
#endif
    if (got > 0) {
        info->channels = (uint32_t)op_channel_count(ogg->reader, link);
        lnd_ogg_reader_tags(ogg, link);
        *pcm = (LND_PCM){.data = ogg->pcm, .frames = (size_t)got, .channels = info->channels, .format = info->format, .layout = LND_LAYOUT_INTERLEAVED};
        ogg->frame += (uint32_t)got;
        ogg->epoch++;
    }
    if (ogg->miss) { return got > 0 ? LND_SOURCE_READY : LND_SOURCE_WAITING; }
    if (got == OP_HOLE) return LND_SOURCE_READY;
    if (got < 0) {
        ogg->failed = true;
        return LND_SOURCE_WAITING;
    }
    if (!got) ogg->session->input_end = true;
    return got ? LND_SOURCE_READY : LND_SOURCE_EOF;
}

static const LND_CODEC_STREAM lnd_ogg_reader_ops = {
    .step = lnd_ogg_reader_step, .close = lnd_ogg_reader_close, .metadata = lnd_ogg_reader_metadata, .flags = LND_CODEC_STREAM_ASYNC};
static const LND_CODEC lnd_ogg_reader_codec = {.name = "opus", .stream = &lnd_ogg_reader_ops};
#endif

bool lnd_http_ogg_playing(const lnd_http_ogg *ogg) { return ogg && ogg->playing; }

int32_t lnd_http_ogg_seek(lnd_http_session *s, lnd_http_ogg *ogg, int64_t time_us) {
#if LND_MODULE_OPUS_DECODER
    if (!ogg || ogg->body || ogg->failed || strcmp(s->info.codec, "opus")) return LND_ERR_UNSUPPORTED;
    lnd_decoder_reset(s->decoder);
    lnd_ogg_http_close(s, ogg);
    lnd_http_request_close(s);
    ogg->frame = (uint64_t)time_us / 1000000 * 48000 + (uint64_t)time_us % 1000000 * 48000 / 1000000;
    ogg->epoch++;
    ogg->missing = 0;
    lnd_ogg_range *range = lnd_ogg_http_range(ogg, 0);
    if (!range) return LND_ERR_UNSUPPORTED;
    range->size = 0;
    ogg->miss = true;
    ogg->session = s;
    LND_CODEC_INFO info = s->format;
    info.sample_rate_hz = 48000;
    info.format = LND_OPUS_INTEGER ? LND_FORMAT_S16 : LND_FORMAT_F32;
    lnd_decoder_attach(s->decoder, &lnd_ogg_reader_codec, ogg, &info);
    ogg->attached = ogg->playing = true;
    return LND_OK;
#else
    return LND_ERR_UNSUPPORTED;
#endif
}

int32_t lnd_http_ogg_play(lnd_http_session *s, lnd_http_ogg *ogg, uint32_t budget) {
    for (uint32_t i = 0; i < budget; i++) {
        if (ogg->failed) return LND_ERR_UNSUPPORTED;
        if (ogg->miss || ogg->transfer) {
            int32_t result = lnd_ogg_http_fetch(s, ogg);
            if (result != LND_OK) return result;
            continue;
        }
        uint64_t decoded = s->stats.decoded_frames;
        int32_t result = lnd_http_decode(s);
        if (result < 0) return result;
        if (ogg->failed) return LND_ERR_UNSUPPORTED;
        if (s->finished) return LND_HTTP_DONE;
        if (!ogg->miss && decoded == s->stats.decoded_frames) return LND_HTTP_PENDING;
    }
    return LND_HTTP_PENDING;
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
    if (ogg->attached) lnd_decoder_reset(s->decoder);
    lnd_ogg_http_finish(s, ogg);
    lnd_decode_tags_clear(&ogg->tags);
    lnd_free(ogg->url);
    lnd_free(ogg);
}
