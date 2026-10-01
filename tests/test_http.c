#include "codec_test.h"
#include "lindar_http.h"

typedef struct transfer {
    const uint8_t *data;
    size_t size, pos, chunk;
    unsigned polls, opens, closes;
    int mode;
    bool live, redirect;
    char agent[128];
} transfer;

static int32_t transport_open(void *user, const LND_HTTP_REQUEST *request, void **out) {
    transfer *t = user;
    CHECK(LND_HttpUpdate(0, 1) == LND_ERR_BUSY);
    t->opens++;
    t->polls = 0;
    t->pos = 0;
    t->redirect = t->mode == 1 && strstr(request->url, "/redirect");
    snprintf(t->agent, sizeof t->agent, "%s", request->user_agent ? request->user_agent : "");
    *out = t;
    return LND_OK;
}

static int32_t transport_poll(void *handle, LND_HTTP_RESPONSE *response, void *data, size_t capacity, size_t *written) {
    transfer *t = handle;
    *written = 0;
    *response = (LND_HTTP_RESPONSE){.status = 200, .headers_complete = true, .length_known = true, .content_length_bytes = t->size};
    if (t->redirect) {
        response->status = 302;
        snprintf(response->location, sizeof response->location, "/audio.wav");
        return LND_HTTP_DONE;
    }
    if (t->mode == 2) {
        response->status = 404;
        return LND_HTTP_DONE;
    }
    if (t->mode == 3) return LND_HTTP_PENDING;
    if (t->polls++ % 5 == 0) return LND_HTTP_PENDING;
    size_t take = t->size - t->pos;
    if (take > t->chunk) take = t->chunk;
    if (take > capacity) take = capacity;
    if (take) memcpy(data, t->data + t->pos, take);
    t->pos += take;
    *written = take;
    return t->pos == t->size && !t->live ? LND_HTTP_DONE : LND_HTTP_PENDING;
}

static void transport_close(void *handle) { ((transfer *)handle)->closes++; }
static const LND_HTTP_TRANSPORT transport_api = {.size = sizeof(LND_HTTP_TRANSPORT),
                                                 .capabilities = LND_HTTP_CAP_HTTP | LND_HTTP_CAP_MANUAL,
                                                 .open = transport_open,
                                                 .poll = transport_poll,
                                                 .close = transport_close};

static void test_play(const char *codec, const char *path, size_t chunk, int mode) {
    size_t bytes = 0;
    uint8_t *data = read_file(path, &bytes);
    if (!data) return;
    LND_ENCODED_SOURCE_OPTIONS file = {.codec_name = codec};
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &file);
    CHECK(reference != nullptr);
    if (!reference) {
        free(data);
        return;
    }
    transfer t = {.data = data, .size = bytes, .chunk = chunk, .mode = mode};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &transport_api;
    options.transport_user = &t;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.codec_name = codec;
    options.buffer.compressed_bytes = 4096;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/redirect", &options);
    CHECK(open != nullptr);
    LND_SOURCE *source = nullptr;
    int16_t actual[256 * 32], expected[256 * 32];
    uint64_t total = 0;
    for (unsigned i = 0; open && i < bytes * 3 + 10000; i++) {
        CHECK(LND_HttpUpdate(i, 16) == LND_OK);
        if (!source) {
            int32_t r = LND_HttpOpenTakeSource(open, &source);
            CHECK(r == LND_OK || r == LND_HTTP_PENDING);
            if (r < 0) break;
        }
        if (source) {
            LND_PCM pcm = {
                .data = actual, .frames = 256, .channels = LND_SourceGetChannels(source), .format = LND_FORMAT_S16, .layout = LND_LAYOUT_INTERLEAVED};
            unsigned before_render = allocations;
            int64_t got = LND_SourceReadPcm(source, &pcm, 0, 256);
            CHECK(allocations == before_render);
            CHECK(got >= 0);
            if (got < 0) break;
            if (got) {
                uint64_t want = LND_SourceRead(reference, expected, LND_FORMAT_S16, (uint64_t)got);
                CHECK(want == (uint64_t)got);
                if (want == (uint64_t)got) CHECK(!memcmp(actual, expected, (size_t)got * pcm.channels * 2));
                total += (uint64_t)got;
            }
            if (LND_SourceGetStatus(source) == LND_SOURCE_EOF) break;
        }
    }
    CHECK(source && total > 0);
    CHECK(!strcmp(t.agent, "Lindar"));
    CHECK(LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, 1) == 0);
    CHECK(t.opens == (mode == 1 ? 2u : 1u));
    LND_HTTP_INFO info;
    CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK && !strcmp(info.codec, codec));
    LND_HTTP_STATS stats;
    CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK);
    if (strcmp(codec, "aiff")) CHECK(stats.file_bytes == 0);
#if LND_MODULE_HTTP_FILE
    if (!strcmp(codec, "aiff")) CHECK(stats.file_bytes == bytes);
#endif
    CHECK(LND_HttpOpenFree(open) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(LND_HttpUpdate(UINT32_MAX, 1) == LND_OK);
    CHECK(t.closes == t.opens);
    CHECK(LND_SourceFree(reference) == LND_OK);
    free(data);
}

static void test_control(void) {
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    transfer t = {.mode = 2};
    options.transport = &transport_api;
    options.transport_user = &t;
    options.execution = LND_HTTP_EXEC_MANUAL;
    char agent[] = "Testing";
    options.user_agent = agent;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/error", &options);
    CHECK(open != nullptr);
    agent[0] = 'X';
    CHECK(LND_HttpUpdate(UINT32_MAX, 8) == LND_OK);
    LND_HTTP_INFO info;
    CHECK(LND_HttpOpenGetInfo(open, &info) == LND_OK && info.state == LND_HTTP_FAILED && info.error.http_status == 404);
    CHECK(!strcmp(t.agent, "Testing"));
    LND_SOURCE *source;
    CHECK(LND_HttpOpenTakeSource(open, &source) < 0 && !source);
    CHECK(LND_HttpOpenFree(open) == LND_OK);
    open = LND_HttpOpen("http://test/cancel", &options);
    CHECK(open != nullptr);
    CHECK(LND_HttpOpenCancel(open) == LND_OK);
    CHECK(LND_HttpUpdate(UINT32_MAX, 1) == LND_OK);
    CHECK(LND_HttpOpenGetInfo(open, &info) == LND_OK && info.state == LND_HTTP_CANCELLED);
    CHECK(LND_HttpOpenFree(open) == LND_OK);
    CHECK(LND_HttpOpen("file:///test", &options) == nullptr);
    options.user_agent = "bad\r\nInjected: 1";
    CHECK(LND_HttpOpen("http://test/", &options) == nullptr);
}

static void test_live_reconnect(const char *path, uint64_t frames) {
    static uint64_t clock = 20000000000ull;
    clock += 30000;
    size_t bytes;
    uint8_t *data = read_file(path, &bytes);
    if (!data) return;
    transfer t = {.data = data, .size = bytes, .chunk = 16384};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &transport_api;
    options.transport_user = &t;
    options.content_mode = LND_HTTP_LIVE;
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    options.retry.delay_ms = 1;
    options.retry.attempts = 1;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/live", &options);
    LND_SOURCE *source = nullptr;
    uint64_t total = 0;
    for (uint32_t i = 0; open && i < 10000; i++) {
        LND_HttpUpdate(clock + i, 16);
        if (!source && LND_HttpOpenTakeSource(open, &source) < 0) break;
        if (!source) continue;
        int16_t out[1024];
        LND_PCM pcm = {.data = out, .frames = 512, .channels = LND_SourceGetChannels(source), .format = LND_FORMAT_S16};
        int64_t got = LND_SourceReadPcm(source, &pcm, 0, 512);
        if (got < 0) break;
        total += (uint64_t)got;
    }
    printf("live %s: frames=%llu opens=%u\n", path, (unsigned long long)total, t.opens);
    CHECK(source != nullptr && total == frames * 2);
    CHECK(t.opens == 2);
    if (source) LND_SourceFree(source);
    if (open) LND_HttpOpenFree(open);
    LND_HttpUpdate(clock + 10000, 1);
    CHECK(t.opens == t.closes);
    free(data);
}

#if LND_MODULE_OPUS_DECODER
static void test_opus_wait(void) {
    size_t bytes;
    uint8_t *data = read_file("audiosamples/tone.opus", &bytes);
    if (!data) return;
    transfer t = {.data = data, .size = bytes, .chunk = 7, .live = true};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &transport_api;
    options.transport_user = &t;
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.content_mode = LND_HTTP_FINITE;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    options.buffer.compressed_bytes = 4096;
    options.retry.receive_timeout_ms = 0;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/opus", &options);
    LND_SOURCE *source = nullptr;
    uint64_t total = 0;
    bool progressive = false;
    unsigned waited = 0;
    for (uint32_t i = 0; open && i < bytes * 2 + 10000; i++) {
        LND_HttpUpdate(30000000000ull + i, 16);
        if (!source && LND_HttpOpenTakeSource(open, &source) < 0) break;
        if (!source) continue;
        int16_t samples[1024];
        LND_PCM pcm = {.data = samples, .frames = 512, .channels = LND_SourceGetChannels(source), .format = LND_FORMAT_S16};
        int64_t got = LND_SourceReadPcm(source, &pcm, 0, 512);
        CHECK(got >= 0);
        if (got < 0) break;
        if (got && t.pos < t.size) progressive = true;
        total += (uint64_t)got;
        if (t.pos == t.size && !got && t.live) {
            CHECK(LND_SourceGetStatus(source) == LND_SOURCE_WAITING);
            if (++waited == 20) t.live = false;
        }
        if (LND_SourceGetStatus(source) == LND_SOURCE_EOF) break;
    }
    CHECK(progressive && waited == 20 && total == 96000);
    CHECK(source && LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    if (source) LND_SourceFree(source);
    if (open) LND_HttpOpenFree(open);
    LND_HttpUpdate(30000100000ull, 1);
    CHECK(t.opens == 1 && t.closes == 1);
    free(data);
}
#endif

static void test_failures(const char *path) {
    size_t bytes = 0;
    uint8_t *data = read_file(path, &bytes);
    if (!data) return;
    static uint64_t clock = 40000000000ull;
    clock += 30000;
    LND_HttpUpdate(clock, 1);
    unsigned baseline = live_allocations;
    for (int fail = 0; fail < 100; fail++) {
        transfer t = {.data = data, .size = bytes, .chunk = 16384};
        LND_HTTP_OPTIONS options;
        LND_HttpOptionsInit(&options);
        options.transport = &transport_api;
        options.transport_user = &t;
        options.execution = LND_HTTP_EXEC_MANUAL;
        options.buffer.start_ms = options.buffer.resume_ms = 0;
        options.buffer.compressed_bytes = 4096;
        fail_allocation = fail;
        LND_HTTP_OPEN *open = LND_HttpOpen("http://test/tone.wav", &options);
        LND_SOURCE *source = nullptr;
        if (open) {
            for (unsigned i = 0; i < 500 && !source; i++) {
                LND_HttpUpdate(clock + i, 16);
                int32_t r = LND_HttpOpenTakeSource(open, &source);
                if (r < 0) break;
            }
        }
        fail_allocation = -1;
        if (source) LND_SourceFree(source);
        if (open) LND_HttpOpenFree(open);
        LND_HttpUpdate(clock + 1000, 1);
        CHECK(t.opens == t.closes);
        CHECK(live_allocations == baseline);
        if (live_allocations != baseline) {
            printf("allocation fault %d leaked\n", fail);
            break;
        }
    }
    free(data);
}

static int32_t unsupported_open(LND_IO *io, LND_CODEC_INFO *info, void **state) { return LND_ERR_UNSUPPORTED; }
static uint64_t unsupported_read(void *state, void *data, uint64_t frames) { return 0; }
static void unsupported_close(void *state) {}

static void test_unsupported_file(void) {
    static const LND_CODEC codec = {.name = "http-unsupported", .open = unsupported_open, .read = unsupported_read, .close = unsupported_close};
    CHECK(LND_CodecRegister(&codec) == LND_OK);
    uint8_t data[8192] = {0};
    for (unsigned large = 0; large < 2; large++) {
        transfer t = {.data = data, .size = large ? sizeof data : 64, .chunk = 1024};
        LND_HTTP_OPTIONS options;
        LND_HttpOptionsInit(&options);
        options.transport = &transport_api;
        options.transport_user = &t;
        options.codec_name = codec.name;
        options.execution = LND_HTTP_EXEC_MANUAL;
        options.buffer.compressed_bytes = 4096;
        LND_HTTP_OPEN *open = LND_HttpOpen("http://test/unsupported", &options);
        CHECK(open != nullptr);
        LND_SOURCE *source = nullptr;
        int32_t result = LND_HTTP_PENDING;
        for (unsigned i = 0; open && result == LND_HTTP_PENDING && i < 100; i++) {
            CHECK(LND_HttpUpdate(50000000000ull + large * 1000 + i, 8) == LND_OK);
            result = LND_HttpOpenTakeSource(open, &source);
        }
        CHECK(result == LND_ERR_UNSUPPORTED && !source);
        if (source) LND_SourceFree(source);
        if (open) LND_HttpOpenFree(open);
        LND_HttpUpdate(50000000500ull + large * 1000, 1);
        CHECK(t.opens == t.closes);
    }
    CHECK(LND_CodecUnregister(&codec) == LND_OK);
}

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
#if LND_MODULE_WAV_DECODER
    test_play("wav", "audiosamples/tone.wav", 1, 0);
    test_play("wav", "audiosamples/tone.wav", 4096, 1);
#endif
#if LND_MODULE_MP3_DECODER
    test_play("mp3", "audiosamples/tone.mp3", 7, 0);
#endif
#if LND_MODULE_FFMPEG_DECODER
    test_play("ffmpeg", "audiosamples/tone.http.ac3", 7, 0);
#endif
#if LND_MODULE_OPUS_DECODER
    test_play("opus", "audiosamples/tone.opus", 1, 0);
    test_play("opus", "audiosamples/tone.opus", 7, 1);
#endif
#if LND_MODULE_FLAC_DECODER
    test_play("flac", "audiosamples/tone.flac", 7, 0);
#endif
#if LND_MODULE_VORBIS_DECODER
    test_play("vorbis", "audiosamples/tone.ogg", 4096, 0);
#endif
#if LND_MODULE_AAC_DECODER
    test_play("aac", "audiosamples/tone.aac", 7, 0);
#endif
#if LND_MODULE_AIFF_DECODER && LND_MODULE_HTTP_FILE
    test_play("aiff", "audiosamples/tone.aiff", 4096, 0);
#endif
    test_control();
#if LND_MODULE_WAV_DECODER
    test_live_reconnect("audiosamples/tone.wav", 88200);
#endif
#if LND_MODULE_OPUS_DECODER
    test_live_reconnect("audiosamples/tone.opus", 96000);
    test_opus_wait();
#endif
#if LND_MODULE_WAV_DECODER
    test_failures("audiosamples/tone.wav");
#endif
#if LND_MODULE_HTTP_FILE && LND_MODULE_AIFF_DECODER
    test_failures("audiosamples/tone.aiff");
#endif
    test_unsupported_file();
    LND_LibraryFree();
    CHECK(!live_allocations);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
