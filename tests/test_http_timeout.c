#include "codec_test.h"
#include "lindar_http.h"

enum { HEADERS, BODY, RETRY, REDIRECT, AUDIO, HLS, MISSING };

typedef struct network {
    const uint8_t *data;
    size_t size, available, position, chunk;
    unsigned mode, opens, closes;
    uint32_t retry_after_ms;
    bool redirect, playlist, check_reentry;
} network;

static int32_t begin(void *user, const LND_HTTP_REQUEST *request, void **out) {
    network *n = user;
    if (n->check_reentry) {
        CHECK(LND_SourceCreateHttp(request->url, 1, nullptr) == nullptr);
        CHECK(LND_ErrorGetLast() == LND_ERR_BUSY);
    }
    n->opens++;
    n->position = 0;
    n->redirect = n->mode == REDIRECT && strstr(request->url, "/redirect");
    n->playlist = n->mode == HLS && strstr(request->url, ".m3u8");
    *out = n;
    return LND_OK;
}

static int32_t poll(void *handle, LND_HTTP_RESPONSE *response, void *data, size_t capacity, size_t *written) {
    network *n = handle;
    *written = 0;
    *response = (LND_HTTP_RESPONSE){0};
    if (n->mode == HEADERS || (n->mode == HLS && !n->playlist)) return LND_HTTP_PENDING;
    *response = (LND_HTTP_RESPONSE){.status = 200, .headers_complete = true, .length_known = true, .content_length_bytes = n->size};
    if (n->mode == BODY) return LND_HTTP_PENDING;
    if (n->mode == MISSING) {
        response->status = 404;
        return LND_HTTP_DONE;
    }
    if (n->mode == RETRY) {
        response->status = 503;
        response->retry_after_ms = n->retry_after_ms;
        return LND_HTTP_DONE;
    }
    if (n->redirect) {
        response->status = 302;
        snprintf(response->location, sizeof response->location, "/audio.wav");
        return LND_HTTP_DONE;
    }
    size_t bytes = n->available - n->position;
    if (bytes > capacity) bytes = capacity;
    if (n->chunk && bytes > n->chunk) bytes = n->chunk;
    if (bytes) memcpy(data, n->data + n->position, bytes);
    n->position += bytes;
    *written = bytes;
    return n->position == n->size ? LND_HTTP_DONE : LND_HTTP_PENDING;
}

static void close_transfer(void *handle) { ((network *)handle)->closes++; }
static const LND_HTTP_TRANSPORT transport = {
    .size = sizeof(LND_HTTP_TRANSPORT), .capabilities = LND_HTTP_CAP_HTTP | LND_HTTP_CAP_MANUAL, .open = begin, .poll = poll, .close = close_transfer};
#if LND_OS_MODE
static void test_blocking(void) {
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    network n = {.mode = HEADERS, .check_reentry = true};
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.transport = &transport;
    options.transport_user = &n;
    options.open_timeout_ms = 1;
    options.retry.receive_timeout_ms = 0;
    unsigned baseline = live_allocations;
    LND_SOURCE *source = LND_SourceCreateHttp("http://test/blocked", 20, &options);
    CHECK(!source && LND_ErrorGetLast() == LND_HTTP_ERR_TIMEOUT);
    CHECK(n.opens == 1 && n.closes == 1 && live_allocations == baseline);
    CHECK(options.open_timeout_ms == 1);
    n.mode = MISSING;
    source = LND_SourceCreateHttp("http://test/missing", 1000, &options);
    CHECK(!source && LND_ErrorGetLast() == LND_ERR_IO);
    CHECK(n.opens == 2 && n.closes == 2 && live_allocations == baseline);
    CHECK(LND_SourceCreateHttp("file:///test", 1, &options) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    options.size = 0;
    CHECK(LND_SourceCreateHttp("http://test/invalid", 1, &options) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(live_allocations == baseline);
}

#if LND_MODULE_WAV_DECODER
static void test_blocking_audio(void) {
    size_t bytes;
    uint8_t *data = read_file("audiosamples/tone.wav", &bytes);
    if (!data) return;
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.transport = &transport;
    options.open_timeout_ms = 1;
    options.retry.receive_timeout_ms = 0;
    options.buffer.pcm_ms = 3000;
    unsigned baseline = live_allocations;
    for (unsigned mode = 0; mode < 3; mode++) {
        network n = {.data = data, .size = bytes, .available = mode == 2 ? 4096 : bytes, .mode = AUDIO};
        options.transport_user = &n;
        options.buffer.start_ms = mode == 1 ? 3000 : 500;
        LND_SOURCE *source = LND_SourceCreateHttp("http://test/audio.wav", mode == 2 ? 20 : 0, &options);
        if (mode == 2) {
            CHECK(!source && LND_ErrorGetLast() == LND_HTTP_ERR_TIMEOUT);
            CHECK(n.closes == n.opens && live_allocations == baseline);
            continue;
        }
        CHECK(source != nullptr);
        if (source) {
            LND_HTTP_INFO info;
            LND_HTTP_STATS stats;
            CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK);
            CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK);
            CHECK(stats.buffering_percent == 100 || info.state == LND_HTTP_DRAINING);
            CHECK(mode == 1 ? info.state == LND_HTTP_DRAINING : stats.buffered_frames >= info.sample_rate_hz / 2);
            int16_t samples[2];
            CHECK(LND_SourceRead(source, samples, LND_FORMAT_S16, 1) == 1);
            CHECK(LND_SourceFree(source) == LND_OK);
        }
        CHECK(LND_HttpUpdate(0, 1) == LND_OK);
        CHECK(n.closes == n.opens && live_allocations == baseline);
        CHECK(options.open_timeout_ms == 1);
    }
    options.buffer.start_ms = 0;
    for (int failure = 0; failure < 50; failure++) {
        network n = {.data = data, .size = bytes, .available = bytes, .mode = AUDIO};
        options.transport_user = &n;
        fail_allocation = failure;
        LND_SOURCE *source = LND_SourceCreateHttp("http://test/audio.wav", 1000, &options);
        fail_allocation = -1;
        if (source) CHECK(LND_SourceFree(source) == LND_OK);
        else CHECK(live_allocations == baseline && n.opens == n.closes);
        CHECK(LND_HttpUpdate(0, 1) == LND_OK);
        CHECK(live_allocations == baseline && n.opens == n.closes);
    }
    free(data);
}
#endif
#endif

static uint64_t now = 60000000000ull;

static LND_HTTP_OPEN *open_stream(network *n, uint32_t timeout, uint32_t start_ms) {
    now += 1000000;
    CHECK(LND_HttpUpdate(now, 1) == LND_OK);
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    CHECK(options.open_timeout_ms == 0);
    options.open_timeout_ms = timeout;
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.transport = &transport;
    options.transport_user = n;
    options.retry.receive_timeout_ms = 0;
    options.retry.delay_ms = 1;
    options.retry.attempts = 100;
    options.buffer.start_ms = start_ms;
    options.buffer.pcm_ms = 3000;
    LND_HTTP_OPEN *open = LND_HttpOpen(n->mode == HLS ? "http://test/index.m3u8" : "http://test/redirect", &options);
    CHECK(open != nullptr);
    return open;
}

static void release_stream(LND_HTTP_OPEN *open, LND_SOURCE *source, network *n) {
    if (source) CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(LND_HttpOpenFree(open) == LND_OK);
    CHECK(LND_HttpUpdate(now + 10000, 1) == LND_OK);
    CHECK(n->opens == n->closes);
}

static void test_wait(unsigned mode, uint32_t timeout, uint32_t retry_after_ms) {
    network n = {.mode = mode, .retry_after_ms = retry_after_ms};
    LND_HTTP_OPEN *open = open_stream(&n, timeout, 500);
    for (unsigned tick = 0; tick < 10; tick++) CHECK(LND_HttpUpdate(now + tick, 1) == LND_OK);
    LND_HTTP_INFO info;
    CHECK(LND_HttpOpenGetInfo(open, &info) == LND_OK && info.state != LND_HTTP_FAILED);
    unsigned requests = n.opens;
    CHECK(LND_HttpUpdate(now + 10, 1) == LND_OK);
    CHECK(LND_HttpOpenGetInfo(open, &info) == LND_OK);
    LND_SOURCE *source = nullptr;
    if (timeout) {
        CHECK(info.state == LND_HTTP_FAILED && info.error.code == LND_HTTP_ERR_TIMEOUT && !info.error.retryable);
        CHECK(LND_HttpOpenTakeSource(open, &source) == LND_HTTP_ERR_TIMEOUT && !source);
        CHECK(n.opens == requests && n.closes == n.opens);
    } else {
        CHECK(LND_HttpUpdate(now + 100000, 1) == LND_OK);
        CHECK(LND_HttpOpenGetInfo(open, &info) == LND_OK);
        CHECK(info.state != LND_HTTP_FAILED);
        CHECK(LND_HttpOpenTakeSource(open, &source) == LND_HTTP_PENDING && !source);
    }
    if (mode == RETRY) CHECK(n.opens == (retry_after_ms ? 1 : 4));
    release_stream(open, source, &n);
}

#if LND_MODULE_WAV_DECODER
static void test_audio(const uint8_t *data, size_t bytes, unsigned mode, bool complete, uint32_t start_ms) {
    network n = {.data = data, .size = bytes, .available = complete ? bytes : 4096, .mode = mode, .chunk = mode == REDIRECT ? 1 : 0};
    LND_HTTP_OPEN *open = open_stream(&n, 100, start_ms);
    LND_SOURCE *source = nullptr;
    for (unsigned tick = 0; tick < 90; tick++) {
        CHECK(LND_HttpUpdate(now + tick, 32) == LND_OK);
        if (!source) CHECK(LND_HttpOpenTakeSource(open, &source) >= 0);
    }
    LND_HTTP_INFO info;
    CHECK(LND_HttpOpenGetInfo(open, &info) == (source ? LND_ERR_STATE : LND_OK));
    if (source) CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK);
    bool succeeds = complete || !start_ms;
    printf("opening: mode=%u start=%u complete=%u state=%d bytes=%zu/%zu\n", mode, start_ms, complete, info.state, n.position, n.size);
    CHECK(succeeds || mode == REDIRECT || (source && info.state == LND_HTTP_BUFFERING));
    CHECK(!complete || info.state == LND_HTTP_READY || info.state == LND_HTTP_DRAINING);
    CHECK(n.position > 0);
    CHECK(LND_HttpUpdate(now + 100, 32) == LND_OK);
    if (source) CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK);
    else CHECK(LND_HttpOpenGetInfo(open, &info) == LND_OK);
    if (succeeds) {
        CHECK(source && info.state != LND_HTTP_FAILED);
        if (!complete && source) {
            int16_t samples[2048];
            for (unsigned i = 0; i < 8; i++) LND_SourceRead(source, samples, LND_FORMAT_S16, 1024);
            CHECK(LND_SourceGetStatus(source) == LND_SOURCE_WAITING);
            CHECK(LND_HttpUpdate(now + 200, 32) == LND_OK);
            CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK && info.state == LND_HTTP_BUFFERING);
        }
    } else {
        CHECK(info.state == LND_HTTP_FAILED && info.error.code == LND_HTTP_ERR_TIMEOUT);
        if (source) {
            float samples[2];
            LND_PCM pcm = {.data = samples, .frames = 1, .channels = 2, .format = LND_FORMAT_F32};
            CHECK(LND_SourceReadPcm(source, &pcm, 0, 1) == LND_HTTP_ERR_TIMEOUT);
            LND_HTTP_STATS stats;
            CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK && stats.buffered_frames == 0);
            LND_HTTP_EVENT event;
            bool reported = false;
            while (LND_SourcePollHttpEvent(source, &event) == LND_OK)
                if (event.type == LND_HTTP_EVENT_ERROR && event.result == LND_HTTP_ERR_TIMEOUT) reported = true;
            CHECK(reported);
        }
    }
    if (mode == REDIRECT) CHECK(n.opens == 2);
    release_stream(open, source, &n);
}
#endif

#if LND_MODULE_HTTP_HLS
static void test_hls(void) {
    const char playlist[] = "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXTINF:1,\npart.aac\n#EXT-X-ENDLIST\n";
    network n = {.mode = HLS, .data = (const uint8_t *)playlist, .size = sizeof playlist - 1, .available = sizeof playlist - 1};
    LND_HTTP_OPEN *open = open_stream(&n, 10, 500);
    CHECK(LND_HttpUpdate(now, 32) == LND_OK);
    CHECK(LND_HttpUpdate(now + 9, 32) == LND_OK);
    CHECK(n.opens == 2 && n.closes == 1);
    CHECK(LND_HttpUpdate(now + 10, 32) == LND_OK);
    LND_HTTP_INFO info;
    CHECK(LND_HttpOpenGetInfo(open, &info) == LND_OK && info.state == LND_HTTP_FAILED && info.error.code == LND_HTTP_ERR_TIMEOUT);
    CHECK(n.opens == 2 && n.closes == 2);
    release_stream(open, nullptr, &n);
}
#endif

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    CHECK(!strcmp(LND_ErrorGetString(LND_HTTP_ERR_TIMEOUT), "HTTP opening timed out"));
#if LND_OS_MODE
    test_blocking();
#if LND_MODULE_WAV_DECODER
    test_blocking_audio();
#endif
#else
    CHECK(LND_SourceCreateHttp("http://test/", 1, nullptr) == nullptr && LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
#endif
    test_wait(HEADERS, 0, 0);
    test_wait(HEADERS, 10, 0);
    test_wait(BODY, 10, 0);
    test_wait(RETRY, 10, 0);
    test_wait(RETRY, 10, 10000);
#if LND_MODULE_WAV_DECODER
    size_t bytes;
    uint8_t *data = read_file("audiosamples/tone.wav", &bytes);
    if (data) {
        test_audio(data, bytes, AUDIO, false, 500);
        test_audio(data, bytes, REDIRECT, false, 500);
        test_audio(data, bytes, AUDIO, false, 0);
        test_audio(data, bytes, AUDIO, true, 0);
        test_audio(data, bytes, AUDIO, true, 3000);
        free(data);
    }
#endif
#if LND_MODULE_HTTP_HLS
    test_hls();
#endif
    LND_LibraryFree();
    CHECK(!live_allocations);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
