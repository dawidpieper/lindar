#include "codec_test.h"
#include "lindar_http.h"
#include "lindar_metadata.h"
#include "src/thread.h"
#include "http_ogg_fixture.h"
#include <stdatomic.h>

typedef struct cache_server {
    const uint8_t *data;
    size_t size, chunk;
    _Atomic unsigned opens, closes, ranges, polls;
    _Atomic bool waiting, changed, range_changed;
    bool etag, unknown;
} cache_server;

typedef struct cache_transfer {
    cache_server *server;
    size_t start, end, at;
    bool range;
} cache_transfer;

static int32_t cache_open(void *user, const LND_HTTP_REQUEST *request, void **out) {
    cache_server *s = user;
    cache_transfer *t = calloc(1, sizeof *t);
    if (!t) return LND_ERR_OUT_OF_MEMORY;
    *t = (cache_transfer){.server = s, .end = s->size, .range = request->range};
    if (t->range) {
        CHECK(request->if_range && (!strcmp(request->if_range, "\"stable\"") || !strcmp(request->if_range, "\"changed\"")));
        if (request->range_start_bytes >= s->size || request->range_length_bytes > s->size - request->range_start_bytes) {
            free(t);
            return LND_ERR_IO;
        }
        t->start = t->at = (size_t)request->range_start_bytes;
        if (request->range_length_bytes) t->end = t->start + (size_t)request->range_length_bytes;
        s->ranges++;
    }
    s->opens++;
    *out = t;
    return LND_OK;
}

static int32_t cache_poll(void *transfer, LND_HTTP_RESPONSE *r, void *data, size_t capacity, size_t *written) {
    cache_transfer *t = transfer;
    cache_server *s = t->server;
    s->polls++;
    *r = (LND_HTTP_RESPONSE){.status = t->range ? 206 : 200,
                             .headers_complete = true,
                             .accepts_ranges = true,
                             .length_known = !s->unknown,
                             .content_length_bytes = t->end - t->start,
                             .range = t->range,
                             .range_start_bytes = t->start,
                             .total_length_bytes = s->size};
    if (t->range && s->range_changed) s->changed = true;
    if (s->etag) strcpy(r->etag, s->changed ? "\"changed\"" : "\"stable\"");
    *written = s->waiting ? 0 : LND_MIN(capacity, LND_MIN(s->chunk ? s->chunk : 16384, t->end - t->at));
    memcpy(data, s->data + t->at, *written);
    t->at += *written;
    return t->at == t->end ? LND_HTTP_DONE : LND_HTTP_PENDING;
}

static void cache_close(void *transfer) {
    cache_transfer *t = transfer;
    t->server->closes++;
    free(t);
}

static const LND_HTTP_TRANSPORT cache_transport = {
    .size = sizeof(LND_HTTP_TRANSPORT), .capabilities = LND_HTTP_CAP_HTTP | LND_HTTP_CAP_MANUAL, .open = cache_open, .poll = cache_poll, .close = cache_close};

static void advance(bool worker, unsigned budget) {
    if (worker)
        lnd_sleep_ms(1);
    else
        CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, budget) == LND_OK);
}

static LND_SOURCE *open_source(cache_server *server, LND_HTTP_OPTIONS *options, bool worker) {
    options->transport = &cache_transport;
    options->transport_user = server;
    options->execution = worker ? LND_HTTP_EXEC_WORKER : LND_HTTP_EXEC_MANUAL;
    options->flags = LND_HTTP_NO_PROBE_DURATION;
    options->buffer.pcm_ms = 25;
    options->buffer.compressed_bytes = 4096;
    options->buffer.segment_bytes = LND_MIN(options->buffer.segment_bytes, (size_t)524288);
    options->buffer.start_ms = options->buffer.resume_ms = 0;
    options->retry.attempts = 0;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/audio", options);
    CHECK(open != nullptr);
    if (!open) return nullptr;
    LND_SOURCE *source = nullptr;
    for (unsigned i = 0; i < 2000 && !source; i++) {
        advance(worker, 32);
        int32_t result = LND_HttpOpenTakeSource(open, &source);
        CHECK(result >= 0);
        if (result < 0) break;
    }
    CHECK(source != nullptr);
    CHECK(LND_HttpOpenFree(open) == LND_OK);
    return source;
}

static void release(LND_SOURCE *source, cache_server *server, bool worker) {
    CHECK(LND_SourceFree(source) == LND_OK);
    for (unsigned i = 0; i < 100 && server->closes != server->opens; i++) advance(worker, 1);
    advance(worker, 1);
    CHECK(server->closes == server->opens);
}

static void full_cache(const uint8_t *data, size_t bytes, bool etag, bool unknown, bool worker) {
    cache_server server = {.data = data, .size = bytes, .etag = etag, .unknown = unknown, .chunk = unknown ? 173 : 16384};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.cache.max_bytes = bytes * 2 + 65536;
    if (unknown) options.cache.ahead_ms = 600000;
    LND_SOURCE *source = open_source(&server, &options, worker);
    if (!source) return;
    LND_HTTP_INFO info;
    for (unsigned i = 0; i < 4000; i++) {
        advance(worker, 32);
        CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK);
        if ((info.cache_complete && info.length_kind == LND_LENGTH_EXACT) || info.state == LND_HTTP_FAILED) break;
    }
    CHECK(info.cache_complete && info.length_kind == LND_LENGTH_EXACT);
    CHECK(info.cached_seek_start_us == 0 && info.cached_seek_end_us == info.duration_us);
    LND_HTTP_STATS stats;
    CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK);
    CHECK(stats.cache_bytes > 0 && stats.cache_bytes <= options.cache.max_bytes && stats.download_complete);
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
    CHECK(reference != nullptr);
#if LND_MODULE_METADATA_COMMENTS
    LND_METADATA *tags = LND_MetadataCreate(nullptr);
    LND_SourceCopyMetadata(source, tags);
    const char *title = LND_MetadataGetValue(tags, "TITLE", 0);
    char expected_title[512] = {0};
    if (title) snprintf(expected_title, sizeof expected_title, "%s", title);
    LND_MetadataFree(tags);
#endif
    unsigned requests = server.opens;
    server.waiting = server.changed = true;

    int64_t targets[] = {500000, 1500000, info.duration_us > 6000000 ? 5000000 : 0, info.duration_us, 0};
    for (unsigned at = 0; reference && at < LND_COUNTOF(targets); at++) {
        uint64_t id;
        CHECK(LND_SourceSeekHttpMicroseconds(source, targets[at], &id) == LND_OK);
        uint32_t rate = LND_SourceGetSampleRateHz(reference), channels = LND_SourceGetChannels(reference);
        if (targets[at] != info.duration_us) CHECK(LND_SourceSeekFrames(reference, (uint64_t)targets[at] * rate / 1000000) == LND_OK);
        int16_t actual[1024], expected[1024];
        int64_t got = 0;
        unsigned events = 0;
        for (unsigned i = 0; i < 2000; i++) {
            advance(worker, 32);
            LND_HTTP_EVENT event;
            while (LND_SourcePollHttpEvent(source, &event) == LND_OK)
                if (event.type == LND_HTTP_EVENT_SEEK && event.request_id == id) {
                    events++;
                    CHECK(event.result == LND_OK);
                }
            got = LND_SourceReadPcm(source, &(LND_PCM){.data = actual, .frames = 512, .channels = channels, .format = LND_FORMAT_S16}, 0, 512);
            if (got || events) break;
        }
        CHECK(events == 1 && server.opens == requests);
        if (targets[at] == info.duration_us)
            CHECK(got == 0 && LND_SourceGetStatus(source) == LND_SOURCE_EOF);
        else {
            CHECK(got > 0);
            if (got > 0) {
                CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, (uint64_t)got) == (uint64_t)got);
                CHECK(!memcmp(actual, expected, (size_t)got * channels * 2));
#if LND_MODULE_METADATA_COMMENTS
                if (*expected_title) {
                    tags = LND_MetadataCreate(nullptr);
                    CHECK(LND_SourceCopyMetadata(source, tags) == LND_OK);
                    title = LND_MetadataGetValue(tags, "TITLE", 0);
                    CHECK(title && !strcmp(title, expected_title));
                    LND_MetadataFree(tags);
                }
#endif
            }
        }
    }
    LND_SourceFree(reference);
    release(source, &server, worker);
}

static void retention(const uint8_t *data, size_t bytes, uint32_t ahead, size_t limit) {
    cache_server server = {.data = data, .size = bytes, .etag = true};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.cache = (LND_HTTP_CACHE_OPTIONS){.max_bytes = limit, .ahead_ms = ahead};
    LND_SOURCE *source = open_source(&server, &options, false);
    if (!source) return;
    for (unsigned i = 0; i < 100; i++) advance(false, 32);
    LND_HTTP_STATS before, after;
    CHECK(LND_SourceGetHttpStats(source, &before) == LND_OK);
    CHECK(before.cache_bytes <= limit && !before.download_complete);
    if (before.cache_bytes) {
        CHECK(before.received_bytes > 16384);
        if (!ahead) CHECK(before.cache_bytes > limit - 32768);
        if (ahead <= 3000 && ahead) CHECK(before.received_bytes <= (uint64_t)bytes * ahead / 120000 + bytes / 120 + 32768);
    } else
        CHECK(before.received_bytes <= 32768);
    for (unsigned i = 0; i < 10; i++) advance(false, 65536);
    CHECK(LND_SourceGetHttpStats(source, &after) == LND_OK);
    CHECK(before.received_bytes == after.received_bytes);
    if (!limit) CHECK(after.cache_bytes == 0);
    int16_t pcm[1024];
    uint64_t frames = 0;
    for (unsigned i = 0; i < 10000 && frames < 48000 * 15; i++) {
        advance(false, 32);
        int64_t got =
            LND_SourceReadPcm(source, &(LND_PCM){.data = pcm, .frames = 512, .channels = LND_SourceGetChannels(source), .format = LND_FORMAT_S16}, 0, 512);
        CHECK(got >= 0);
        if (got < 0) break;
        frames += (uint64_t)got;
        CHECK(LND_SourceGetHttpStats(source, &after) == LND_OK && after.cache_bytes <= limit);
    }
    CHECK(frames >= 48000 * 15);
    release(source, &server, false);
}

static void partial_opus(const uint8_t *data, size_t bytes, bool changed) {
    cache_server server = {.data = data, .size = bytes, .etag = true};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.cache = (LND_HTTP_CACHE_OPTIONS){.max_bytes = 524288};
    LND_SOURCE *source = open_source(&server, &options, false);
    if (!source) return;
    for (unsigned i = 0; i < 100; i++) advance(false, 32);
    uint64_t id;
    int16_t actual[1024], expected[1024];
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
    CHECK(reference != nullptr);
    for (unsigned at = 0; at < 3; at++) {
        int64_t target = changed && at == 2 ? 90000000 : 5000000;
        if (changed && at == 2) server.changed = true;
        unsigned requests = server.opens;
        CHECK(LND_SourceSeekHttpMicroseconds(source, target, &id) == LND_OK);
        unsigned events = 0;
        for (unsigned i = 0; i < 20000 && !events; i++) {
            advance(false, 1);
            LND_HTTP_EVENT event;
            while (LND_SourcePollHttpEvent(source, &event) == LND_OK)
                if (event.type == LND_HTTP_EVENT_SEEK && event.request_id == id) {
                    events++;
                    CHECK(event.result == LND_OK);
                }
        }
        CHECK(events == 1);
        if (at == 1) CHECK(server.opens == requests);
        CHECK(LND_SourceGetStatus(source) >= 0);
        int64_t got = LND_SourceReadPcm(source, &(LND_PCM){.data = actual, .frames = 512, .channels = 2, .format = LND_FORMAT_S16}, 0, 512);
        CHECK(got > 0);
        if (got > 0 && reference) {
            if (changed && at == 2) {
                LND_SourceFree(reference);
                reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
                uint64_t skip = (uint64_t)target * 48000 / 1000000;
                while (skip) {
                    uint64_t take = LND_MIN(skip, 512ull);
                    CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, take) == take);
                    skip -= take;
                }
            } else
                CHECK(LND_SourceSeekFrames(reference, (uint64_t)target * 48000 / 1000000) == LND_OK);
            CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, (uint64_t)got) == (uint64_t)got);
            CHECK(!memcmp(actual, expected, (size_t)got * 4));
        }
        LND_HTTP_STATS stats;
        CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK && stats.cache_bytes <= options.cache.max_bytes);
    }
    LND_SourceFree(reference);
    release(source, &server, false);
}

static void timed_seek(const uint8_t *data, size_t bytes) {
    cache_server server = {.data = data, .size = bytes, .etag = true};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.cache = (LND_HTTP_CACHE_OPTIONS){.max_bytes = bytes * 2, .ahead_ms = 1000};
    LND_SOURCE *source = open_source(&server, &options, false);
    if (!source) return;
    uint64_t id;
    CHECK(LND_SourceSeekHttpMicroseconds(source, 90000000, &id) == LND_OK);
    unsigned events = 0;
    for (unsigned i = 0; i < 1000; i++) {
        advance(false, 32);
        LND_HTTP_EVENT event;
        while (LND_SourcePollHttpEvent(source, &event) == LND_OK)
            if (event.type == LND_HTTP_EVENT_SEEK && event.request_id == id) {
                events++;
                CHECK(event.result == LND_OK);
            }
    }
    LND_HTTP_STATS before, after;
    CHECK(events == 1);
    CHECK(LND_SourceGetHttpStats(source, &before) == LND_OK);
    CHECK(before.cache_bytes <= options.cache.max_bytes && !before.download_complete && before.received_bytes < bytes / 4);
    for (unsigned i = 0; i < 10; i++) advance(false, 65536);
    CHECK(LND_SourceGetHttpStats(source, &after) == LND_OK);
    CHECK(before.received_bytes == after.received_bytes);
    int16_t actual[1024], expected[1024];
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
    CHECK(reference != nullptr);
    if (reference) {
        CHECK(LND_SourceSeekFrames(reference, 48000 * 90) == LND_OK);
        CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, 512) == 512);
        CHECK(LND_SourceRead(source, actual, LND_FORMAT_S16, 512) == 512);
        CHECK(!memcmp(actual, expected, sizeof actual));
        LND_SourceFree(reference);
    }
    release(source, &server, false);
}

static void early_eof(const uint8_t *data, size_t bytes) {

    cache_server server = {.data = data, .size = bytes, .etag = true};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.cache.max_bytes = bytes * 2 + 65536;
    LND_SOURCE *source = open_source(&server, &options, false);
    if (!source) return;
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
    CHECK(reference != nullptr);
    uint64_t frames = LND_SourceGetLengthFrames(reference), id;
    int64_t end = (int64_t)(frames / 48000 * 1000000 + frames % 48000 * 1000000 / 48000);
    CHECK(LND_SourceSeekHttpMicroseconds(source, end, &id) == LND_OK);
    LND_HTTP_INFO info;
    unsigned events = 0;
    for (unsigned i = 0; i < 1000; i++) {
        advance(false, 32);
        LND_HTTP_EVENT event;
        while (LND_SourcePollHttpEvent(source, &event) == LND_OK)
            if (event.type == LND_HTTP_EVENT_SEEK && event.request_id == id) events++;
        CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK);
        if (info.cache_complete || info.state == LND_HTTP_FAILED) break;
    }
    CHECK(info.cache_complete && events == 1);
    LND_SourceFree(reference);
    release(source, &server, false);
}

static void cancel_seek(const uint8_t *data, size_t bytes) {
    cache_server server = {.data = data, .size = bytes, .etag = true};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.cache = (LND_HTTP_CACHE_OPTIONS){.max_bytes = 524288, .ahead_ms = 1000};
    LND_SOURCE *source = open_source(&server, &options, false);
    if (!source) return;
    server.waiting = true;
    CHECK(LND_SourceSeekHttpMicroseconds(source, 90000000, nullptr) == LND_OK);
    for (unsigned i = 0; i < 4; i++) {
        unsigned polls = server.polls;
        advance(false, 65536);
        CHECK(server.polls <= polls + 1);
    }
    CHECK(LND_SourceCancelHttp(source) == LND_OK);
    release(source, &server, false);
}

static void shared_budget(const uint8_t *data, size_t bytes) {
    cache_server stalled = {.data = data, .size = bytes, .etag = true, .waiting = true};
    cache_server ready = {.data = data, .size = bytes};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.cache.max_bytes = bytes * 2 + 65536;
    options.transport = &cache_transport;
    options.transport_user = &stalled;
    options.execution = LND_HTTP_EXEC_MANUAL;
    LND_HTTP_OPEN *waiting = LND_HttpOpen("http://test/stalled", &options);
    CHECK(waiting != nullptr);
    LND_SOURCE *source = open_source(&ready, &options, false);
    if (source) {
        LND_HTTP_INFO info;
        for (unsigned i = 0; i < 1000; i++) {
            unsigned polls = stalled.polls;
            advance(false, 2);
            CHECK(stalled.polls <= polls + 1);
            CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK);
            if (info.cache_complete) break;
        }
        CHECK(info.cache_complete);
        release(source, &ready, false);
    }
    CHECK(LND_HttpOpenCancel(waiting) == LND_OK);
    CHECK(LND_HttpOpenFree(waiting) == LND_OK);
    advance(false, 1);
    CHECK(stalled.opens == stalled.closes);
}

static void waiting(void) {
    const uint8_t data[32] = {0};
    cache_server server = {.data = data, .size = sizeof data, .etag = true, .waiting = true};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.cache.max_bytes = 65536;
    options.transport = &cache_transport;
    options.transport_user = &server;
    options.execution = LND_HTTP_EXEC_MANUAL;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/wait", &options);
    CHECK(open != nullptr);
    for (unsigned i = 0; i < 4; i++) {
        unsigned polls = server.polls;
        advance(false, 65536);
        CHECK(server.polls == polls + 1);
    }
    CHECK(LND_HttpOpenCancel(open) == LND_OK);
    CHECK(LND_HttpOpenFree(open) == LND_OK);
    advance(false, 1);
    CHECK(server.opens == server.closes);
}

#if LND_MODULE_AAC_DECODER && LND_MODULE_HTTP_MP4
static uint32_t get_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put_be32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (24 - i * 8));
}

static void partial_mp4(bool changed) {
    size_t bytes;
    uint8_t *tone = read_file("audiosamples/tone.m4a", &bytes);
    if (!tone) return;
    size_t padding = 1048576, head = get_be32(tone);
    uint8_t *data = calloc(1, bytes + padding);
    CHECK(data != nullptr);
    if (!data) {
        free(tone);
        return;
    }
    memcpy(data, tone, head);
    put_be32(data + head, (uint32_t)padding);
    memcpy(data + head + 4, "free", 4);
    memcpy(data + head + padding, tone + head, bytes - head);
    bytes += padding;
    for (size_t i = head + padding; i + 12 < bytes; i++) {
        if (memcmp(data + i, "stco", 4)) continue;
        uint32_t count = get_be32(data + i + 8);
        CHECK(count <= (bytes - i - 12) / 4);
        for (uint32_t j = 0; j < count; j++) {
            uint8_t *offset = data + i + 12 + j * 4;
            put_be32(offset, get_be32(offset) + (uint32_t)padding);
        }
    }
    cache_server server = {.data = data, .size = bytes, .etag = true, .range_changed = changed};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.cache = (LND_HTTP_CACHE_OPTIONS){.max_bytes = 262144, .ahead_ms = 500};
    LND_SOURCE *source = open_source(&server, &options, false);
    if (source) {
        for (unsigned i = 0; i < 100; i++) advance(false, 32);
        LND_HTTP_INFO info;
        CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK && !info.cache_complete);
        if (changed) {
            CHECK(server.opens >= 3);
            int16_t actual[1024], expected[1024];
            LND_SOURCE *reference = LND_SourceCreateEncodedMemory(tone, bytes - padding, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
            CHECK(reference != nullptr);
            CHECK(LND_SourceRead(source, actual, LND_FORMAT_S16, 512) == 512);
            CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, 512) == 512);
            CHECK(!memcmp(actual, expected, sizeof actual));
            LND_SourceFree(reference);
            release(source, &server, false);
            free(tone);
            free(data);
            return;
        }
        CHECK(info.cached_seek_start_us == 0 && info.cached_seek_end_us > 1000000);
        unsigned requests = server.opens;
        uint64_t id;
        CHECK(LND_SourceSeekHttpMicroseconds(source, 1000000, &id) == LND_OK);
        for (unsigned i = 0; i < 100; i++) advance(false, 32);
        CHECK(server.opens == requests);
        int16_t pcm[1024];
        CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 512) == 512);
        release(source, &server, false);
    }
    free(tone);
    free(data);
}
#endif

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
#if LND_MODULE_WAV_DECODER
    size_t bytes;
    uint8_t *data = read_file("audiosamples/tone.wav", &bytes);
    if (data) {
        full_cache(data, bytes, false, false, false);
        full_cache(data, bytes, true, true, false);
        shared_budget(data, bytes);
#if LND_THREADS
        full_cache(data, bytes, true, false, true);
#endif
        free(data);
    }
#endif
#if LND_MODULE_OPUS_DECODER
    size_t opus_bytes;
    uint8_t *opus = long_fixture(&opus_bytes);
    if (opus) {
        full_cache(opus, opus_bytes, false, false, false);
        full_cache(opus, opus_bytes, true, false, false);
        retention(opus, opus_bytes, 60000, 0);
        retention(opus, opus_bytes, 0, 100);
        retention(opus, opus_bytes, 0, 262144);
        retention(opus, opus_bytes, 60000, 262144);
        retention(opus, opus_bytes, 1000, 262144);
        retention(opus, opus_bytes, 3000, opus_bytes * 2);
        partial_opus(opus, opus_bytes, false);
        partial_opus(opus, opus_bytes, true);
        timed_seek(opus, opus_bytes);
        early_eof(opus, opus_bytes);
        cancel_seek(opus, opus_bytes);
        free(opus);
    }
    uint8_t *chains = chained_fixture(&opus_bytes);
    if (chains) {
        full_cache(chains, opus_bytes, false, false, false);
        full_cache(chains, opus_bytes, true, false, false);
        free(chains);
    }
#if LND_MODULE_METADATA_COMMENTS
    uint8_t *comments = read_file("audiosamples/comments.opus", &opus_bytes);
    if (comments) {
        full_cache(comments, opus_bytes, false, false, false);
        free(comments);
    }
#endif
#endif

#if LND_MODULE_VORBIS_DECODER
    size_t vorbis_bytes;
    uint8_t *vorbis = read_file("audiosamples/tone.ogg", &vorbis_bytes);
    if (vorbis) {
        full_cache(vorbis, vorbis_bytes, false, false, false);
        free(vorbis);
    }
#endif
#if LND_MODULE_MP3_DECODER
    size_t mp3_bytes;
    uint8_t *mp3 = read_file("audiosamples/tone.mp3", &mp3_bytes);
    if (mp3) {
        full_cache(mp3, mp3_bytes, false, false, false);
        free(mp3);
    }
#endif
#if LND_MODULE_AIFF_DECODER
    size_t aiff_bytes;
    uint8_t *aiff = read_file("audiosamples/tone.aiff", &aiff_bytes);
    if (aiff) {
        full_cache(aiff, aiff_bytes, false, false, false);
        free(aiff);
    }
#endif
#if LND_MODULE_FLAC_DECODER
    size_t flac_bytes;
    uint8_t *flac = read_file("audiosamples/tone.flac", &flac_bytes);
    if (flac) {
        full_cache(flac, flac_bytes, false, false, false);
        free(flac);
    }
#endif
    if (LND_CodecGetCount()) waiting();
#if LND_MODULE_AAC_DECODER
    size_t aac_bytes;
    uint8_t *aac = read_file("audiosamples/tone.m4a", &aac_bytes);
    if (aac) {
        full_cache(aac, aac_bytes, true, false, false);
        free(aac);
    }
#endif
#if LND_MODULE_AAC_DECODER && LND_MODULE_HTTP_MP4
    partial_mp4(false);
    partial_mp4(true);
#endif
    LND_LibraryFree();
    CHECK(live_allocations == 0);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
