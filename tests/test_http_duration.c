#include "codec_test.h"
#include "lindar_http.h"
#include "src/thread.h"

#include <stdint.h>

typedef struct duration_server {
    const uint8_t *data, *alternate;
    size_t size;
    int mode;
    unsigned opens, closes, ranges, errors, version;
} duration_server;

typedef struct duration_transfer {
    duration_server *server;
    size_t start, end, position;
    bool range, delayed, matches;
    unsigned version;
} duration_transfer;

static int32_t duration_open(void *user, const LND_HTTP_REQUEST *request, void **out) {
    duration_server *s = user;
    duration_transfer *t = calloc(1, sizeof *t);
    if (!t) return LND_ERR_OUT_OF_MEMORY;
    t->server = s;
    t->version = s->version;
    t->range = request->range;
    t->end = s->size;
    if (request->range) {
        s->ranges++;
        if (!request->if_range || (strcmp(request->if_range, "\"changed\"") && strcmp(request->if_range, "\"stable\""))) s->errors++;
        t->matches = request->if_range && !strcmp(request->if_range, s->version ? "\"changed\"" : "\"stable\"");
        if (s->mode != 2) {
            if (request->range_start_bytes >= s->size || request->range_length_bytes > s->size - request->range_start_bytes) {
                free(t);
                return LND_ERR_IO;
            }
            t->start = (size_t)request->range_start_bytes;
            t->end = t->start + (size_t)request->range_length_bytes;
        }
    }
    if (request->flags & (LND_HTTP_PROBE_DURATION | LND_HTTP_NO_PROBE_DURATION)) s->errors++;
    if (t->range && !t->matches) {
        t->start = 0;
        t->end = s->size;
    }
    t->position = t->start;
    s->opens++;
    *out = t;
    return LND_OK;
}

static int32_t duration_poll(void *transfer, LND_HTTP_RESPONSE *response, void *data, size_t capacity, size_t *written) {
    duration_transfer *t = transfer;
    duration_server *s = t->server;
    bool partial = t->range && t->matches && s->mode != 2;
    *written = 0;
    *response = (LND_HTTP_RESPONSE){.status = partial ? 206 : 200,
                                    .headers_complete = true,
                                    .accepts_ranges = s->mode != 1,
                                    .length_known = s->mode != 10 && s->mode != 15 && s->mode != 17,
                                    .content_length_bytes = t->end - t->start,
                                    .range = partial,
                                    .range_start_bytes = t->start,
                                    .total_length_bytes = s->size};
    snprintf(response->etag, sizeof response->etag, "\"%s\"", (t->range && s->mode == 3) || t->version ? "changed" : "stable");
    if (s->mode >= 13) response->etag[0] = 0;
    if (s->mode == 9) snprintf(response->etag, sizeof response->etag, "W/\"stable\"");
    if (partial && s->mode == 11) response->content_length_bytes++;
    if (partial && s->mode == 12) response->status = 302;
    if (partial && s->mode == 4) response->range_start_bytes++;
    if (partial && s->mode == 5) response->total_length_bytes++;
    if (partial && s->mode == 7) return LND_ERR_IO;
    if (partial && s->mode == 8) return LND_HTTP_PENDING;
    size_t take = LND_MIN(capacity, LND_MIN((size_t)2048, t->end - t->position));
    if (partial && s->mode == 6) take = LND_MIN(take, (t->end - t->position) / 2);
    if (s->mode == 14) take = LND_MIN(take, s->size / 2 - t->position);
    const uint8_t *body = t->version && s->alternate ? s->alternate : s->data;
    memcpy(data, body + t->position, take);
    t->position += take;
    *written = take;
    if (s->mode == 18 && t->position - take == 65536) fail_allocation = 0;
    if (t->position == t->end && s->mode >= 16 && !t->delayed) {
        t->delayed = true;
        return LND_HTTP_PENDING;
    }
    return t->position == t->end || (s->mode == 14 && t->position == s->size / 2) || (partial && s->mode == 6) ? LND_HTTP_DONE : LND_HTTP_PENDING;
}

static void duration_close(void *transfer) {
    duration_transfer *t = transfer;
    t->server->closes++;
    free(t);
}

static const LND_HTTP_TRANSPORT duration_transport = {.size = sizeof(LND_HTTP_TRANSPORT),
                                                      .capabilities = LND_HTTP_CAP_HTTP | LND_HTTP_CAP_MANUAL,
                                                      .open = duration_open,
                                                      .poll = duration_poll,
                                                      .close = duration_close};

typedef struct duration_case {
    int64_t expected_us, next_us;
    const uint8_t *alternate;
    int mode;
    uint32_t flags;
    bool enabled;
    bool worker;
    bool live;
    bool read;
    bool seek;
    size_t compressed_bytes;
    size_t probe_bytes;
    unsigned budget;
} duration_case;

static void test_duration(const uint8_t *data, size_t bytes, const char *codec, duration_case c) {
    static uint64_t clock;
    if (!clock) clock = lnd_time_ns() / 1000000;
    clock += 20000;
    CHECK(LND_ConfigSet(LND_CFG_HTTP_PROBE_DURATION, !c.enabled) == LND_OK);
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    CHECK(LND_ConfigSet(LND_CFG_HTTP_PROBE_DURATION, c.enabled) == LND_OK);
    duration_server server = {.data = data, .alternate = c.alternate, .size = bytes, .mode = c.mode};
    options.transport = &duration_transport;
    options.transport_user = &server;
    options.codec_name = codec;
    options.flags = c.flags;
    options.execution = c.worker ? LND_HTTP_EXEC_WORKER : LND_HTTP_EXEC_MANUAL;
    options.content_mode = c.live ? LND_HTTP_LIVE : LND_HTTP_FINITE;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    options.buffer.pcm_ms = 25;
    if (c.compressed_bytes) options.buffer.compressed_bytes = c.compressed_bytes;
    if (c.probe_bytes) options.buffer.segment_bytes = c.probe_bytes;
    options.retry.connect_timeout_ms = options.retry.receive_timeout_ms = 100;
    options.retry.attempts = 0;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/audio", &options);
    CHECK(open != nullptr);
    CHECK(LND_ConfigSet(LND_CFG_HTTP_PROBE_DURATION, !c.enabled) == LND_OK);
    LND_SOURCE *source = nullptr;
    LND_HTTP_INFO info = {0};
    LND_HTTP_STATS stats = {0};
    uint64_t deadline = lnd_time_ns() + 5000000000ull;
    unsigned budget = c.budget ? c.budget : 16;
    for (unsigned i = 0; open && i < 12000 && lnd_time_ns() < deadline; i++) {
        if (!c.worker) CHECK(LND_HttpUpdate(clock + i, budget) == LND_OK);
        if (!source) CHECK(LND_HttpOpenTakeSource(open, &source) >= 0);
        if (source) {
            CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK);
            CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK);
            if (c.expected_us >= 0 && info.length_kind == LND_LENGTH_EXACT && (stats.buffered_frames || !c.expected_us)) break;
            if (c.expected_us < 0 && stats.buffered_frames && (stats.download_complete || c.live || c.compressed_bytes) && i >= 200) break;
        }
        if (c.worker) lnd_sleep_ms(1);
    }
    printf("%s %s mode=%d flags=%u cfg=%d: duration=%lld kind=%d requests=%llu\n", c.worker ? "worker" : "manual", codec, c.mode, c.flags, c.enabled,
           (long long)info.duration_us, info.length_kind, (unsigned long long)stats.requests);
    CHECK(source != nullptr && info.state != LND_HTTP_FAILED);
    if (c.expected_us >= 0) {
        CHECK(info.length_kind == LND_LENGTH_EXACT && info.duration_us == c.expected_us && info.seek_end_us == c.expected_us);
        CHECK(c.expected_us ? stats.buffered_frames && stats.decoded_frames < (uint64_t)c.expected_us * info.sample_rate_hz / 1000000 : !stats.decoded_frames);
    } else
        CHECK(info.length_kind == LND_LENGTH_UNKNOWN);
    if (c.mode == 18) {
        CHECK(fail_allocation == 0);
        fail_allocation = -1;
    }
    if (!c.compressed_bytes && !c.live) {
        for (unsigned i = 0; source && !stats.download_complete && i < 1000; i++) {
            if (!c.worker)
                CHECK(LND_HttpUpdate(clock + 12000 + i, budget) == LND_OK);
            else
                lnd_sleep_ms(1);
            CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK);
        }
        CHECK(stats.download_complete);
    }
    bool full = c.mode == 1 || c.mode == 9 || c.mode == 10 || c.mode >= 13;
    if (c.expected_us >= 0 && full) CHECK(stats.received_bytes == bytes && stats.download_complete && server.opens == 1);
    if (c.compressed_bytes && !full) CHECK(stats.received_bytes < bytes / 4 && !stats.download_complete);
    if (source && c.seek) {
        uint64_t id;
        CHECK(LND_SourceSeekHttpMicroseconds(source, 0, &id) == LND_OK && id);
        if (c.mode == 9 || c.mode >= 13) {
            CHECK(LND_HttpUpdate(clock + 14000, 1) == LND_OK);
            CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK && info.length_kind == LND_LENGTH_UNKNOWN);
        }
        for (unsigned i = 0; i < 300; i++) CHECK(LND_HttpUpdate(clock + 14000 + i, budget) == LND_OK);
        CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK && info.duration_us == c.expected_us && info.length_kind == LND_LENGTH_EXACT);
        server.version++;
        CHECK(LND_SourceSeekHttpMicroseconds(source, 0, &id) == LND_OK && id);
        CHECK(LND_HttpUpdate(clock + 14500, 1) == LND_OK);
        if (!strcmp(codec, "opus")) CHECK(LND_HttpUpdate(clock + 14500, 1) == LND_OK);
        CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK && info.length_kind == LND_LENGTH_UNKNOWN);
        for (unsigned i = 0; i < 300; i++) CHECK(LND_HttpUpdate(clock + 14501 + i, budget) == LND_OK);
        int64_t next = c.next_us ? c.next_us : c.expected_us;
        CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK && info.duration_us == next && info.length_kind == LND_LENGTH_EXACT);
    }
    if (source && c.read) {
        LND_ENCODED_SOURCE_OPTIONS encoded = {.codec_name = codec};
        LND_SOURCE *reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &encoded);
        CHECK(reference != nullptr);
        int16_t actual[1024], expected[1024];
        uint64_t frames = 0;
        for (unsigned i = 0; reference && i < 10000; i++) {
            if (!c.worker) CHECK(LND_HttpUpdate(clock + 15000 + i, 16) == LND_OK);
            LND_PCM pcm = {.data = actual, .channels = LND_SourceGetChannels(source), .frames = 512, .format = LND_FORMAT_S16};
            int64_t got = LND_SourceReadPcm(source, &pcm, 0, 512);
            CHECK(got >= 0);
            if (got > 0) {
                CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, (uint64_t)got) == (uint64_t)got);
                CHECK(!memcmp(actual, expected, (size_t)got * pcm.channels * sizeof(int16_t)));
                frames += (uint64_t)got;
            }
            if (LND_SourceGetStatus(source) == LND_SOURCE_EOF) break;
            if (c.worker && !got) lnd_sleep_ms(1);
        }
        CHECK(LND_SourceGetStatus(source) == LND_SOURCE_EOF && frames > 0);
        CHECK(!LND_SourceRead(reference, expected, LND_FORMAT_S16, 1));
        CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK && info.length_kind == LND_LENGTH_EXACT);
        if (reference) CHECK(LND_SourceFree(reference) == LND_OK);
    }
    if (source) CHECK(LND_SourceFree(source) == LND_OK);
    if (open) CHECK(LND_HttpOpenFree(open) == LND_OK);
    CHECK(LND_HttpUpdate(clock + 30000, 1) == LND_OK);
    clock += 30000;
    CHECK(server.opens == server.closes && !server.errors);
    if (!c.enabled && !(c.flags & LND_HTTP_PROBE_DURATION)) CHECK(!server.ranges);
    if (c.flags & LND_HTTP_NO_PROBE_DURATION || c.live || full) CHECK(!server.ranges);
}

static void put32(uint8_t *data, uint32_t value) {
    for (unsigned i = 0; i < 4; i++) data[i] = (uint8_t)(value >> (i * 8));
}

static void put64(uint8_t *data, uint64_t value) {
    for (unsigned i = 0; i < 8; i++) data[i] = (uint8_t)(value >> (i * 8));
}

static size_t page_bytes(const uint8_t *data) {
    size_t bytes = 27 + data[26];
    for (unsigned i = 0; i < data[26]; i++) bytes += data[27 + i];
    return bytes;
}

static void checksum(uint8_t *data, size_t bytes) {
    put32(data + 22, 0);
    uint32_t crc = 0;
    for (size_t i = 0; i < bytes; i++) {
        crc ^= (uint32_t)data[i] << 24;
        for (unsigned bit = 0; bit < 8; bit++) crc = crc << 1 ^ (crc >> 31 ? UINT32_C(0x04c11db7) : 0);
    }
    put32(data + 22, crc);
}

static void test_cancel(const uint8_t *data, size_t bytes, const char *codec, int mode) {
    CHECK(LND_ConfigSet(LND_CFG_HTTP_PROBE_DURATION, 1) == LND_OK);
    duration_server server = {.data = data, .size = bytes, .mode = mode};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &duration_transport;
    options.transport_user = &server;
    options.codec_name = codec;
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    options.buffer.pcm_ms = 25;
    options.retry.connect_timeout_ms = options.retry.receive_timeout_ms = 0;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/audio", &options);
    CHECK(open != nullptr);
    LND_SOURCE *source = nullptr;
    for (unsigned i = 0; open && (!source || (mode == 8 && !server.ranges)) && i < 1000; i++) {
        CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, 1) == LND_OK);
        if (!source) CHECK(LND_HttpOpenTakeSource(open, &source) >= 0);
    }
    CHECK(source && (mode == 8 ? server.ranges : !server.ranges) && server.closes < server.opens);
    if (source) {
        CHECK(LND_SourceCancelHttp(source) == LND_OK);
        CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, 1) == LND_OK);
        LND_HTTP_INFO info;
        CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK && info.state == LND_HTTP_CANCELLED);
        CHECK(LND_SourceFree(source) == LND_OK);
    }
    if (open) CHECK(LND_HttpOpenFree(open) == LND_OK);
    CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, 1) == LND_OK);
    CHECK(server.opens == server.closes && !server.errors);
}

static uint8_t *chain_fixture(const uint8_t *data, size_t bytes, unsigned links) {
    uint8_t *chain = malloc(bytes * links);
    if (!chain) return nullptr;
    for (unsigned link = 0; link < links; link++) {
        uint8_t *start = chain + bytes * link;
        memcpy(start, data, bytes);
        for (size_t at = 0; at < bytes;) {
            size_t page = page_bytes(start + at);
            uint32_t serial = 0;
            for (unsigned i = 0; i < 4; i++) serial |= (uint32_t)start[at + 14 + i] << (i * 8);
            put32(start + at + 14, serial + link);
            checksum(start + at, page);
            at += page;
        }
    }
    return chain;
}

static void test_complete_response(const uint8_t *data, size_t bytes, const char *codec) {
    uint8_t *chain = chain_fixture(data, bytes, 6);
    CHECK(chain != nullptr);
    if (!chain) return;
    bytes *= 6;
    duration_case c = {.expected_us = 12000000, .mode = 13, .enabled = true, .read = true, .seek = true, .compressed_bytes = 4096, .budget = 1};
    test_duration(chain, bytes, codec, c);
    c.seek = c.read = false;
    c.mode = 1;
    test_duration(chain, bytes, codec, c);
    c.mode = 9;
    test_duration(chain, bytes, codec, c);
    c.mode = 15;
    test_duration(chain, bytes, codec, c);
    c.mode = 13;
    c.probe_bytes = bytes;
    test_duration(chain, bytes, codec, c);
    c.mode = 16;
    test_duration(chain, bytes, codec, c);
    c.mode = 17;
    test_duration(chain, bytes, codec, c);
    c.mode = 13;
    c.probe_bytes = bytes - 1;
    c.expected_us = -1;
    test_duration(chain, bytes, codec, c);
    c.mode = 15;
    c.read = true;
    test_duration(chain, bytes, codec, c);
    c.read = false;
    c.mode = 13;
    c.probe_bytes = 0;
    c.enabled = false;
    test_duration(chain, bytes, codec, c);
    c.flags = LND_HTTP_PROBE_DURATION;
    c.expected_us = 12000000;
    test_duration(chain, bytes, codec, c);
    c.enabled = true;
    c.flags = LND_HTTP_NO_PROBE_DURATION;
    c.expected_us = -1;
    test_duration(chain, bytes, codec, c);
    c.flags = 0;
    c.expected_us = 12000000;
#if LND_THREADS
    c.worker = true;
    c.read = false;
    test_duration(chain, bytes, codec, c);
    c.worker = false;
    c.read = true;
#endif
    c.mode = 18;
    c.expected_us = -1;
    c.read = true;
    test_duration(chain, bytes, codec, c);
    c.mode = 13;
    c.expected_us = 12000000;
    if (!strcmp(codec, "opus")) {
        uint8_t *alternate = malloc(bytes);
        CHECK(alternate != nullptr);
        if (alternate) {
            memcpy(alternate, chain, bytes);
            for (size_t at = 0; at < bytes;) {
                size_t page = page_bytes(alternate + at);
                if (alternate[at + 5] & 2) {
                    size_t packet = at + 27 + alternate[at + 26];
                    alternate[packet + 10] = 0xc0;
                    alternate[packet + 11] = 0x03;
                    checksum(alternate + at, page);
                }
                at += page;
            }
            c.read = false;
            c.seek = true;
            c.alternate = alternate;
            c.next_us = 11919000;
            test_duration(chain, bytes, codec, c);
            free(alternate);
        }
    }
    free(chain);
}

static void test_incomplete_response(const uint8_t *data, size_t bytes, const char *codec) {
    CHECK(LND_ConfigSet(LND_CFG_HTTP_PROBE_DURATION, 1) == LND_OK);
    duration_server server = {.data = data, .size = bytes, .mode = 14};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &duration_transport;
    options.transport_user = &server;
    options.codec_name = codec;
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.retry.attempts = 0;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/audio", &options);
    CHECK(open != nullptr);
    LND_HTTP_INFO info = {0};
    for (unsigned i = 0; open && info.state != LND_HTTP_FAILED && i < 500; i++) {
        CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, 1) == LND_OK);
        CHECK(LND_HttpOpenGetInfo(open, &info) == LND_OK);
        CHECK(info.length_kind == LND_LENGTH_UNKNOWN);
    }
    CHECK(info.state == LND_HTTP_FAILED && info.error.code == LND_ERR_IO);
    if (open) CHECK(LND_HttpOpenFree(open) == LND_OK);
    CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, 1) == LND_OK);
    CHECK(server.opens == 1 && server.opens == server.closes && !server.errors);
}

static void test_fixture(const char *codec, const char *path) {
    size_t bytes;
    uint8_t *data = read_file(path, &bytes);
    if (!data) return;
    test_cancel(data, bytes, codec, 8);
    test_cancel(data, bytes, codec, 13);
    test_complete_response(data, bytes, codec);
    test_incomplete_response(data, bytes, codec);
    duration_case c = {.expected_us = 2000000, .enabled = true, .read = true, .seek = true};
    test_duration(data, bytes, codec, c);
    c.read = c.seek = false;
    c.budget = 1;
    test_duration(data, bytes, codec, c);
    c.budget = 16;
    c.enabled = false;
    c.expected_us = -1;
    c.read = true;
    test_duration(data, bytes, codec, c);
    c.flags = LND_HTTP_PROBE_DURATION;
    c.expected_us = 2000000;
    test_duration(data, bytes, codec, c);
    c.enabled = true;
    c.flags = LND_HTTP_NO_PROBE_DURATION;
    c.expected_us = -1;
    test_duration(data, bytes, codec, c);
    c.flags = 0;
    for (c.mode = 1; c.mode <= 13; c.mode++) {
        c.expected_us = c.mode == 1 || c.mode == 9 || c.mode == 10 || c.mode == 13 ? 2000000 : -1;
        test_duration(data, bytes, codec, c);
    }
    c.mode = 13;
    c.probe_bytes = bytes;
    test_duration(data, bytes, codec, c);
    c.expected_us = -1;
    c.probe_bytes = bytes - 1;
    test_duration(data, bytes, codec, c);
    c.mode = 15;
    test_duration(data, bytes, codec, c);
    c.expected_us = 2000000;
    c.probe_bytes = bytes;
    test_duration(data, bytes, codec, c);
    c.expected_us = -1;
    c.mode = 0;
    c.read = false;
    c.probe_bytes = 4096;
    test_duration(data, bytes, codec, c);
    c.probe_bytes = 0;
    c.live = true;
    test_duration(data, bytes, codec, c);
    c.live = false;
    c.expected_us = 2000000;
#if LND_THREADS
    c.worker = true;
    c.read = true;
    test_duration(data, bytes, codec, c);
    c.worker = c.read = false;
#endif
    uint8_t *chain = chain_fixture(data, bytes, 2);
    CHECK(chain != nullptr);
    if (chain) {
        c.expected_us = 4000000;
        c.read = true;
        test_duration(chain, bytes * 2, codec, c);
        c.mode = 13;
        test_duration(chain, bytes * 2, codec, c);
        c.mode = 0;
        if (!strcmp(codec, "vorbis")) {
            put32(chain + bytes + 27 + chain[bytes + 26] + 12, 22050);
            checksum(chain + bytes, page_bytes(chain + bytes));
            c.expected_us = 6000000;
            c.read = false;
            test_duration(chain, bytes * 2, codec, c);
        }
        free(chain);
    }
    uint8_t *damaged = malloc(bytes);
    CHECK(damaged != nullptr);
    if (damaged) {
        memcpy(damaged, data, bytes);
        damaged[bytes - 1] ^= 1;
        c.read = false;
        c.expected_us = -1;
        test_duration(damaged, bytes, codec, c);
        test_duration(data, bytes - 1, codec, c);
        c.mode = 13;
        test_duration(damaged, bytes, codec, c);
        test_duration(data, bytes - 1, codec, c);
        c.mode = 0;
        free(damaged);
    }
    if (!strcmp(codec, "opus")) {
        c.read = false;
        size_t first = page_bytes(data);
        uint8_t *copy = malloc(bytes);
        CHECK(copy != nullptr);
        if (copy) {
            memcpy(copy, data, bytes);
            copy[27 + copy[26] + 10] = 0xc0;
            copy[27 + copy[26] + 11] = 0x03;
            checksum(copy, first);
            c.expected_us = 1986500;
            test_duration(copy, bytes, codec, c);
            memcpy(copy, data, bytes);
            for (size_t at = 0; at < bytes;) {
                size_t page = page_bytes(copy + at);
                uint64_t granule = 0;
                for (unsigned i = 0; i < 8; i++) granule |= (uint64_t)copy[at + 6 + i] << (i * 8);
                if (granule && granule != UINT64_MAX) put64(copy + at + 6, granule + 48000);
                checksum(copy + at, page);
                at += page;
            }
            c.expected_us = 2000000;
            c.read = true;
            test_duration(copy, bytes, codec, c);
            free(copy);
        }
        size_t head = first + page_bytes(data + first), page = page_bytes(data + head);
        uint8_t *empty = malloc(head + page);
        CHECK(empty != nullptr);
        if (empty) {
            memcpy(empty, data, head + page);
            size_t packet = 27 + empty[26];
            empty[packet + 10] = 0x80;
            empty[packet + 11] = 0xbb;
            checksum(empty, first);
            empty[head + 5] = 4;
            checksum(empty + head, page);
            c.read = false;
            c.expected_us = 0;
            test_duration(empty, head + page, codec, c);
            free(empty);
        }
        size_t size = head + page * 100;
        uint8_t *large = malloc(size);
        CHECK(large != nullptr);
        if (large) {
            memcpy(large, data, head);
            for (unsigned i = 0; i < 100; i++) {
                uint8_t *dst = large + head + i * page;
                memcpy(dst, data + head, page);
                dst[5] = i == 99 ? 4 : 0;
                put64(dst + 6, (uint64_t)(i + 1) * 48000);
                put32(dst + 18, i + 2);
                checksum(dst, page);
            }
            c.read = false;
            c.compressed_bytes = 4096;
            c.expected_us = 99993500;
            c.budget = 1;
            test_duration(large, size, codec, c);
            c.mode = 13;
            test_duration(large, size, codec, c);
            c.mode = 15;
            test_duration(large, size, codec, c);
            free(large);
        }
    }
    free(data);
}

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    CHECK(LND_ConfigFindKey("http.probe_duration") == LND_CFG_HTTP_PROBE_DURATION);
    CHECK(LND_ConfigGet(LND_CFG_HTTP_PROBE_DURATION) == 1);
    CHECK(LND_ConfigSet(LND_CFG_HTTP_PROBE_DURATION, 2) == LND_ERR_INVALID_ARG);
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.flags = LND_HTTP_PROBE_DURATION | LND_HTTP_NO_PROBE_DURATION;
    CHECK(!LND_HttpOpen("http://test/audio", &options) && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
#if LND_MODULE_OPUS_DECODER
    test_fixture("opus", "audiosamples/tone.opus");
#endif
#if LND_MODULE_VORBIS_DECODER
    test_fixture("vorbis", "audiosamples/tone.ogg");
#endif
    LND_LibraryFree();
    CHECK(!live_allocations);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
