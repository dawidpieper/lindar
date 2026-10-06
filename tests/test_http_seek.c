#include "codec_test.h"
#include "lindar_http.h"
#include "src/thread.h"
#include "http_ogg_fixture.h"

#include <stdint.h>
#include <stdatomic.h>

typedef struct seek_server {
    const uint8_t *data;
    size_t size;
    _Atomic unsigned opens, closes, ranges, polls, errors;
    _Atomic int mode;
    _Atomic bool waiting;
} seek_server;

typedef struct seek_transfer {
    seek_server *server;
    size_t start, end, position;
    bool range;
} seek_transfer;

static int32_t seek_open(void *user, const LND_HTTP_REQUEST *request, void **out) {
    seek_server *s = user;
    seek_transfer *t = calloc(1, sizeof *t);
    if (!t) return LND_ERR_OUT_OF_MEMORY;
    *t = (seek_transfer){.server = s, .end = s->size, .range = request->range};
    if (request->range) {
        s->ranges++;
        if (!request->if_range || strcmp(request->if_range, "\"stable\"")) s->errors++;
        if (request->range_start_bytes >= s->size || request->range_length_bytes > s->size - request->range_start_bytes) {
            free(t);
            return LND_ERR_IO;
        }
        t->start = t->position = (size_t)request->range_start_bytes;
        if (request->range_length_bytes) t->end = t->start + (size_t)request->range_length_bytes;
    }
    s->opens++;
    *out = t;
    return LND_OK;
}

static int32_t seek_poll(void *transfer, LND_HTTP_RESPONSE *response, void *data, size_t capacity, size_t *written) {
    seek_transfer *t = transfer;
    seek_server *s = t->server;
    s->polls++;
    *response = (LND_HTTP_RESPONSE){.status = t->range ? 206 : 200,
                                    .headers_complete = true,
                                    .accepts_ranges = true,
                                    .length_known = true,
                                    .content_length_bytes = t->end - t->start,
                                    .range = t->range,
                                    .range_start_bytes = t->start,
                                    .total_length_bytes = s->size};
    if (s->mode != 1) strcpy(response->etag, t->range && (s->mode == 2 || (s->mode == 5 && s->ranges > 4)) ? "\"changed\"" : "\"stable\"");
    if (t->range && s->mode == 3) response->status = 200;
    if (t->range && s->mode == 4) response->range_start_bytes++;
    *written = s->waiting ? 0 : LND_MIN(capacity, LND_MIN((size_t)16384, t->end - t->position));
    memcpy(data, s->data + t->position, *written);
    t->position += *written;
    return t->position == t->end ? LND_HTTP_DONE : LND_HTTP_PENDING;
}

static void seek_close(void *transfer) {
    seek_transfer *t = transfer;
    t->server->closes++;
    free(t);
}

static const LND_HTTP_TRANSPORT seek_transport = {
    .size = sizeof(LND_HTTP_TRANSPORT), .capabilities = LND_HTTP_CAP_HTTP | LND_HTTP_CAP_MANUAL, .open = seek_open, .poll = seek_poll, .close = seek_close};

static unsigned updates;

static void update(bool worker, unsigned budget) {
    updates++;
    if (worker)
        lnd_sleep_ms(1);
    else
        CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, budget) == LND_OK);
}

static void test_seek(const uint8_t *data, size_t bytes, bool worker, int mode, unsigned budget, size_t cache) {
    seek_server server = {.data = data, .size = bytes, .mode = mode};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &seek_transport;
    options.transport_user = &server;
    options.codec_name = "opus";
    options.execution = worker ? LND_HTTP_EXEC_WORKER : LND_HTTP_EXEC_MANUAL;
    options.flags = LND_HTTP_NO_PROBE_DURATION;
    options.buffer.compressed_bytes = 4096;
    options.buffer.segment_bytes = cache ? cache : 524288;
    options.buffer.pcm_ms = 25;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    options.retry.attempts = 0;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/audio", &options);
    CHECK(open != nullptr);
    if (!open) return;
    LND_SOURCE *source = nullptr;
    uint64_t until = lnd_time_ns() + 2000000000ull;
    for (unsigned step = 0; !source && step < 1000 && lnd_time_ns() < until; step++) {
        update(worker, budget);
        CHECK(LND_HttpOpenTakeSource(open, &source) >= 0);
    }
    CHECK(source != nullptr && !server.ranges);
    LND_ENCODED_SOURCE_OPTIONS encoded = {.codec_name = "opus"};
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &encoded);
    CHECK(reference != nullptr);
    bool fast = !mode && (!cache || cache >= 524288);
    uint64_t length = LND_SourceGetLengthFrames(reference);
    int64_t end_us = (int64_t)(length / 48000 * 1000000 + length % 48000 * 1000000 / 48000);
    int64_t targets[] = {5000000, 30000000, 90000000, 5000000, 0, end_us, 0, 15000000, 45000000, 75000000, 105000000};
    if (length < 90 * 48000) {
        targets[0] = 500000;
        targets[1] = targets[3] = 2000000;
        targets[2] = 5000000;
    }
    for (unsigned at = 0; source && reference && at < LND_COUNTOF(targets); at++) {
        if (at >= 7 && (!fast || targets[at] >= end_us)) continue;
        if (!fast && targets[at] == end_us) continue;
        if (mode > 1 && at > 0 && targets[at]) continue;
        if (cache && at != 2 && targets[at]) continue;
        uint64_t id = 0, start = lnd_time_ns();
        LND_HTTP_STATS before, after;
        CHECK(LND_SourceGetHttpStats(source, &before) == LND_OK);
        unsigned opens = server.opens, ranges = server.ranges, steps = updates;
        CHECK(LND_SourceSeekHttpMicroseconds(source, targets[at], &id) == LND_OK && id);
        bool complete = false;
        for (unsigned step = 0; !complete && step < 20000 && lnd_time_ns() - start < 5000000000ull; step++) {
            update(worker, budget);
            LND_HTTP_EVENT event;
            while (LND_SourcePollHttpEvent(source, &event) == LND_OK)
                if (event.type == LND_HTTP_EVENT_SEEK && event.request_id == id) complete = true;
        }
        CHECK(complete);
        CHECK(LND_SourceGetHttpStats(source, &after) == LND_OK);
        uint64_t decoded = after.decoded_frames - before.decoded_frames;
        printf("%s mode=%d budget=%u seek=%u: %.2f ms, decoded=%llu requests=%u ranges=%u\n", worker ? "worker" : "manual", mode, budget,
               (unsigned)(targets[at] / 1000000), (double)(lnd_time_ns() - start) / 1000000, (unsigned long long)decoded, server.opens - opens,
               server.ranges - ranges);
        if (fast) {
            CHECK(server.opens - opens == server.ranges - ranges && server.ranges > ranges);
            CHECK(decoded < 48000 * 2);
        }
        uint64_t target_frame = (uint64_t)targets[at] / 1000000 * 48000 + (uint64_t)targets[at] % 1000000 * 48000 / 1000000;
        if (target_frame == length) {
            int16_t sample[2];
            CHECK(!LND_SourceRead(source, sample, LND_FORMAT_S16, 1) && LND_SourceGetStatus(source) == LND_SOURCE_EOF);
            continue;
        }
        CHECK(LND_SourceSeekFrames(reference, fast ? target_frame : 0) == LND_OK);
        if (!fast) {
            int16_t discarded[2048];
            for (uint64_t left = target_frame; left;) {
                uint64_t take = LND_MIN(left, UINT64_C(1024));
                CHECK(LND_SourceRead(reference, discarded, LND_FORMAT_S16, take) == take);
                left -= take;
            }
        }
        int16_t actual[1024], expected[1024];
        uint64_t got = 0;
        until = lnd_time_ns() + 2000000000ull;
        for (unsigned step = 0; !got && step < 2000 && LND_SourceGetStatus(source) != LND_SOURCE_EOF && lnd_time_ns() < until; step++) {
            update(worker, budget);
            got = LND_SourceRead(source, actual, LND_FORMAT_S16, 512);
        }
        CHECK(got && LND_SourceRead(reference, expected, LND_FORMAT_S16, got) == got);
        unsigned delta = 0;
        for (size_t i = 0; i < got * LND_SourceGetChannels(source); i++) delta = LND_MAX(delta, (unsigned)abs((int)actual[i] - expected[i]));
        printf("PCM delta=%u\n", delta);
        CHECK(delta <= (targets[at] ? 16u : 0u));
        CHECK(!worker || lnd_time_ns() - start < 4000000000ull);
        if (!worker && !fast && budget > 1 && target_frame >= 30 * 48000) CHECK(updates - steps < decoded / 1024 / 4);
        CHECK(after.buffered_bytes <= options.buffer.segment_bytes + options.buffer.compressed_bytes);
        if (fast && !worker && budget > 1 && at == 2 && length > 90 * 48000) {
            uint64_t read = got;
            until = lnd_time_ns() + 2000000000ull;
            for (unsigned step = 0; step < 20000 && LND_SourceGetStatus(source) != LND_SOURCE_EOF && lnd_time_ns() < until; step++) {
                update(worker, budget);
                got = LND_SourceRead(source, actual, LND_FORMAT_S16, 512);
                CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, got) == got);
                CHECK(!memcmp(actual, expected, (size_t)got * LND_SourceGetChannels(source) * sizeof(int16_t)));
                read += got;
                CHECK(LND_SourceGetHttpStats(source, &after) == LND_OK);
                CHECK(after.buffered_bytes <= options.buffer.segment_bytes + options.buffer.compressed_bytes);
            }
            CHECK(LND_SourceGetStatus(source) == LND_SOURCE_EOF && read == length - target_frame);
        }
    }
    if (source) CHECK(LND_SourceFree(source) == LND_OK);
    if (reference) CHECK(LND_SourceFree(reference) == LND_OK);
    if (open) CHECK(LND_HttpOpenFree(open) == LND_OK);
    until = lnd_time_ns() + 1000000000ull;
    do update(worker, budget);
    while (server.opens != server.closes && lnd_time_ns() < until);
    CHECK(server.opens == server.closes && !server.errors);
}

static void test_wait(const uint8_t *data, size_t bytes) {
    seek_server servers[2] = {{.data = data, .size = bytes}, {.data = data, .size = bytes}};
    LND_HTTP_OPEN *opens[2] = {0};
    LND_SOURCE *sources[2] = {0};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &seek_transport;
    options.codec_name = "opus";
    options.flags = LND_HTTP_NO_PROBE_DURATION;
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.buffer.compressed_bytes = 4096;
    options.buffer.segment_bytes = 524288;
    options.buffer.pcm_ms = 25;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    for (unsigned i = 0; i < 2; i++) {
        options.transport_user = servers + i;
        opens[i] = LND_HttpOpen("http://test/audio", &options);
        CHECK(opens[i] != nullptr);
        for (unsigned step = 0; !sources[i] && step < 100; step++) {
            update(false, 2);
            CHECK(LND_HttpOpenTakeSource(opens[i], sources + i) >= 0);
        }
        CHECK(sources[i] != nullptr);
    }
    uint64_t ids[2];
    servers[0].waiting = true;
    CHECK(LND_SourceSeekHttpMicroseconds(sources[0], 90000000, ids) == LND_OK);
    CHECK(LND_SourceSeekHttpMicroseconds(sources[1], 5000000, ids + 1) == LND_OK);
    unsigned polls = servers[0].polls;
    update(false, 65536);
    CHECK(servers[0].polls == polls + 1);
    bool complete = false;
    LND_HTTP_EVENT event;
    while (LND_SourcePollHttpEvent(sources[1], &event) == LND_OK)
        if (event.type == LND_HTTP_EVENT_SEEK && event.request_id == ids[1]) complete = true;
    CHECK(complete);
    while (LND_SourcePollHttpEvent(sources[0], &event) == LND_OK) CHECK(event.type != LND_HTTP_EVENT_SEEK);
    for (unsigned i = 0; i < 3; i++) {
        polls = servers[0].polls;
        update(false, 65536);
        CHECK(servers[0].polls == polls + 1);
    }
    uint64_t replacement;
    CHECK(LND_SourceSeekHttpMicroseconds(sources[0], 30000000, &replacement) == LND_OK && replacement != ids[0]);
    update(false, 65536);
    CHECK(LND_SourceCancelHttp(sources[0]) == LND_OK);
    update(false, 65536);
    LND_HTTP_INFO info;
    CHECK(LND_SourceGetHttpInfo(sources[0], &info) == LND_OK && info.state == LND_HTTP_CANCELLED);
    CHECK(servers[0].opens == servers[0].closes);
    for (unsigned i = 0; i < 2; i++) {
        CHECK(LND_SourceFree(sources[i]) == LND_OK);
        CHECK(LND_HttpOpenFree(opens[i]) == LND_OK);
    }
    update(false, 2);
    CHECK(servers[1].opens == servers[1].closes && !servers[0].errors && !servers[1].errors);
}

static void test_continuation(const uint8_t *data, size_t bytes) {
    seek_server server = {.data = data, .size = bytes, .mode = 5};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &seek_transport;
    options.transport_user = &server;
    options.codec_name = "opus";
    options.flags = LND_HTTP_NO_PROBE_DURATION;
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.buffer.compressed_bytes = 4096;
    options.buffer.segment_bytes = 524288;
    options.buffer.pcm_ms = 25;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/audio", &options);
    CHECK(open != nullptr);
    LND_SOURCE *source = nullptr;
    for (unsigned i = 0; open && !source && i < 100; i++) {
        update(false, 32);
        CHECK(LND_HttpOpenTakeSource(open, &source) >= 0);
    }
    CHECK(source != nullptr);
    LND_ENCODED_SOURCE_OPTIONS encoded = {.codec_name = "opus"};
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &encoded);
    CHECK(reference && LND_SourceSeekFrames(reference, 5 * 48000) == LND_OK);
    uint64_t id, frames = 0;
    CHECK(LND_SourceSeekHttpMicroseconds(source, 5000000, &id) == LND_OK);
    unsigned events = 0;
    for (unsigned i = 0; source && reference && i < 24000 && LND_SourceGetStatus(source) != LND_SOURCE_EOF; i++) {
        update(false, 32);
        int16_t actual[1024], expected[1024];
        uint64_t got = LND_SourceRead(source, actual, LND_FORMAT_S16, 512);
        CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, got) == got);
        CHECK(!memcmp(actual, expected, (size_t)got * LND_SourceGetChannels(source) * sizeof(int16_t)));
        frames += got;
        LND_HTTP_EVENT event;
        while (LND_SourcePollHttpEvent(source, &event) == LND_OK)
            if (event.type == LND_HTTP_EVENT_SEEK) {
                CHECK(event.request_id == id);
                events++;
            }
    }
    CHECK(events == 1 && server.ranges > 4 && server.opens == server.ranges + 2);
    CHECK(LND_SourceGetStatus(source) == LND_SOURCE_EOF && frames == LND_SourceGetLengthFrames(reference) - 5 * 48000);
    if (source) CHECK(LND_SourceFree(source) == LND_OK);
    if (reference) CHECK(LND_SourceFree(reference) == LND_OK);
    if (open) CHECK(LND_HttpOpenFree(open) == LND_OK);
    update(false, 32);
    CHECK(server.opens == server.closes && !server.errors);
}

static void test_chains(void) {
    size_t bytes;
    uint8_t *data = chained_fixture(&bytes);
    if (data) {
        test_seek(data, bytes, false, 0, 1, 0);
#if LND_THREADS
        test_seek(data, bytes, true, 0, 32, 0);
#endif
        free(data);
    }
}

int main(void) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    test_init(LND_LAYOUT_INTERLEAVED);
    size_t bytes;
    uint8_t *data = long_fixture(&bytes);
    if (data) {
        test_seek(data, bytes, false, 0, 32, 0);
#if LND_THREADS
        test_seek(data, bytes, true, 0, 32, 0);
        test_seek(data, bytes, true, 1, 32, 0);
#endif
        test_seek(data, bytes, false, 1, 32, 0);
        test_seek(data, bytes, false, 0, 1, 0);
        test_seek(data, bytes, false, 0, 32, 65536);
        for (int mode = 2; mode <= 4; mode++) test_seek(data, bytes, false, mode, 32, 0);
        test_wait(data, bytes);
        test_continuation(data, bytes);
        test_chains();
        free(data);
    }
    LND_LibraryFree();
    CHECK(!live_allocations);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
