#include "codec_test.h"
#include "lindar_http.h"
#include "lindar_http_curl.h"
#include "src/thread.h"
#include "lindar_metadata.h"

static void play(const char *base, const char *path, const char *reference_path, const char *ca, bool worker, int32_t error) {
    char url[4096];
    snprintf(url, sizeof url, "%s%s", base, path);
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    LND_HTTP_HEADER headers[] = {{.name = "Authorization", .value = "Bearer test"}, {.name = "X-Test", .value = "present"}};
    options.headers = headers;
    options.header_count = 2;
    options.execution = worker ? LND_HTTP_EXEC_WORKER : LND_HTTP_EXEC_MANUAL;
    options.ca_file = ca;
    if (!strcmp(path, "/ua")) options.transport = LND_HttpCurlGetTransport();
    if (!strcmp(path, "/ua")) options.user_agent = "LindarTest/2";
    if (!strcmp(path, "/no-agent")) options.flags |= LND_HTTP_NO_USER_AGENT;
    options.buffer.compressed_bytes = 8192;
    if (!strcmp(path, "/large.m4a") || !strcmp(path, "/large-file")) options.buffer.compressed_bytes = 4096;
    if (!strcmp(path, "/large-file")) options.codec_name = "aiff";
    if (!strcmp(path, "/no-file-cache")) options.file.max_bytes = 0;
    if (!strcmp(path, "/file-limit")) options.file.max_bytes = 5000;
    if (!strcmp(path, "/bad-cache-dir")) options.file.directory = "lindar-missing-directory";
    if (!strcmp(path, "/icy")) options.content_mode = LND_HTTP_FINITE;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    bool probe_duration = !strcmp(path, "/tone.opus") || !strcmp(path, "/tone.ogg");
    if (probe_duration) options.buffer.pcm_ms = 25;
    options.retry.delay_ms = 5;
    options.retry.attempts = 2;
    options.retry.receive_timeout_ms = 1000;
    LND_HTTP_OPEN *open = LND_HttpOpen(url, &options);
    CHECK(open != nullptr);
    size_t size = 0;
    uint8_t *file = reference_path ? read_file(reference_path, &size) : nullptr;
    LND_SOURCE *reference = file ? LND_SourceCreateEncodedMemory(file, size, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr) : nullptr;
    LND_SOURCE *source = nullptr;
    uint64_t deadline = lnd_time_ns() + 15000000000ull;
    uint64_t total = 0;
    unsigned metadata = 0;
    int32_t result = LND_OK;
    while (open && lnd_time_ns() < deadline) {
        if (!worker) CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, 32) == LND_OK);
        if (!source) {
            if (probe_duration) {
                LND_HTTP_INFO info;
                CHECK(LND_HttpOpenGetInfo(open, &info) == LND_OK);
                if (info.state != LND_HTTP_FAILED && info.length_kind != LND_LENGTH_EXACT) {
                    lnd_sleep_ms(1);
                    continue;
                }
                CHECK(info.length_kind == LND_LENGTH_EXACT && info.duration_us == 2000000 && info.seek_end_us == 2000000);
            }
            result = LND_HttpOpenTakeSource(open, &source);
            if (result < 0) break;
        }
        if (source) {
            int16_t actual[1024], expected[1024];
            uint32_t channels = LND_SourceGetChannels(source);
            CHECK(channels <= 2);
            LND_PCM pcm = {.data = actual, .channels = channels, .frames = 512, .format = LND_FORMAT_S16, .layout = LND_LAYOUT_INTERLEAVED};
            int64_t got = LND_SourceReadPcm(source, &pcm, 0, 512);
            if (got < 0) {
                result = (int32_t)got;
                break;
            }
            if (reference && got) {
                CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, (uint64_t)got) == (uint64_t)got);
                CHECK(!memcmp(actual, expected, (size_t)got * channels * 2));
            }
            total += (uint64_t)got;
            LND_HTTP_EVENT event;
            while (LND_SourcePollHttpEvent(source, &event) == LND_OK) {
                if (event.type == LND_HTTP_EVENT_METADATA) {
                    metadata++;
                    CHECK(event.estimated && event.position_us <= (int64_t)(total * 1000000 / LND_SourceGetSampleRateHz(source)));
                    CHECK(!strncmp(event.text, "Track ", 6));
                }
            }
            if (LND_SourceGetStatus(source) == LND_SOURCE_EOF) {
                result = LND_OK;
                break;
            }
            if (got) continue;
        }
        lnd_sleep_ms(1);
    }
    if (result < 0 && !source) {
        LND_HTTP_INFO info;
        if (LND_HttpOpenGetInfo(open, &info) == LND_OK) {
            printf("backend=%d status=%d\n", info.error.backend_code, info.error.http_status);
            if (!strcmp(path, "/tone.wav") && !strncmp(base, "https:", 6) && !ca) CHECK(info.error.backend_code == 60);
        }
    }
    printf("%s %s: %llu frames, result %d\n", worker ? "worker" : "manual", path, (unsigned long long)total, result);
    if (error)
        CHECK(result < 0);
    else {
        CHECK(result == LND_OK && source && LND_SourceGetStatus(source) == LND_SOURCE_EOF);
        CHECK(total > 80000);
        int16_t sample[32];
        CHECK(!LND_SourceRead(reference, sample, LND_FORMAT_S16, 1));
    }
    if (!strcmp(path, "/icy")) CHECK(metadata > 0);
#if LND_MODULE_METADATA_COMMENTS
    if (!strncmp(path, "/comments.", 10) && source) {
        LND_METADATA *tags = LND_MetadataCreate(nullptr);
        CHECK(LND_SourceCopyMetadata(source, tags) == LND_OK);
        const char *title = LND_MetadataGetValue(tags, "TITLE", 0), *artist = LND_MetadataGetValue(tags, "ARTIST", 0), *album = LND_MetadataGetValue(tags, "ALBUM", 0);
        CHECK(title && !strcmp(title, "Lindar") && artist && !strcmp(artist, "Test") && album && !strcmp(album, "Album"));
        const LND_METADATA_CHAPTER *chapter = LND_MetadataGetChapter(tags, 0);
        CHECK(chapter && chapter->start_us == 1250000 && !strcmp(chapter->title, "Chapter"));
        LND_MetadataFree(tags);
        LND_HTTP_STATS stats;
        CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK && stats.content_size_known && stats.content_bytes == size && stats.download_complete && stats.buffering_percent == 100);
    }
#endif

    if (!strcmp(path, "/large-file") && source) {
        LND_HTTP_STATS stats;
        CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK);
        CHECK(stats.file_bytes > 8 * 1024 * 1024 && stats.buffered_bytes < 65536);
    }
    if (!strcmp(path, "/large.m4a") && source && reference) {
        for (unsigned seek = 0; seek < 2; seek++) {
            int64_t target = seek ? 0 : 1000000;
            uint64_t id = 0;
            CHECK(LND_SourceSeekHttpMicroseconds(source, target, &id) == LND_OK && id);
            CHECK(LND_SourceSeekFrames(reference, 0) == LND_OK);
            uint32_t sample_rate_hz = LND_SourceGetSampleRateHz(source), channels = LND_SourceGetChannels(source);
            int16_t actual[1024], expected[1024];
            for (uint64_t left = (uint64_t)target * sample_rate_hz / 1000000; left;) {
                uint64_t take = left < 512 ? left : 512;
                CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, take) == take);
                left -= take;
            }
            unsigned max_delta = 0;
            uint64_t square_error = 0;
            uint64_t got_total = 0, until = lnd_time_ns() + 10000000000ull;
            while (lnd_time_ns() < until) {
                if (!worker) LND_HttpUpdate(lnd_time_ns() / 1000000, 32);
                uint64_t got = LND_SourceRead(source, actual, LND_FORMAT_S16, 512);
                CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, got) == got);
                for (size_t i = 0; i < (size_t)got * channels; i++) {
                    unsigned delta = (unsigned)abs((int)actual[i] - expected[i]);
                    if (delta > max_delta) max_delta = delta;
                    square_error += (uint64_t)delta * delta;
                }
                got_total += got;
                if (LND_SourceGetStatus(source) == LND_SOURCE_EOF) break;
                if (!got) lnd_sleep_ms(1);
            }
            printf("seek=%u max_delta=%u mean_square=%.6f\n", seek, max_delta, got_total ? (double)square_error / (got_total * channels) : 0);
            CHECK(max_delta <= (seek ? 0u : 16u));
            CHECK(square_error <= got_total * channels * 4);
            CHECK(got_total == total - (uint64_t)target * sample_rate_hz / 1000000);
        }
        LND_HTTP_STATS stats;
        CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK && stats.received_bytes < 1024 * 1024);
    }
    if (source) LND_SourceFree(source);
    if (reference) LND_SourceFree(reference);
    LND_HttpOpenFree(open);
    LND_HttpUpdate(lnd_time_ns() / 1000000, 1);
    free(file);
}

static void test_blocking(const char *base, bool worker) {
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.execution = worker ? LND_HTTP_EXEC_WORKER : LND_HTTP_EXEC_MANUAL;
    LND_HTTP_HEADER header = {.name = "Authorization", .value = "Bearer test"};
    options.headers = &header;
    options.header_count = 1;
    char url[4096];
    snprintf(url, sizeof url, "%s/tone.wav", base);
    LND_SOURCE *source = LND_SourceCreateHttp(url, 5000, &options);
    CHECK(source != nullptr);
    if (source) {
        LND_HTTP_STATS stats;
        CHECK(LND_SourceGetHttpStats(source, &stats) == LND_OK && stats.buffering_percent == 100);
        CHECK(stats.buffered_frames >= LND_SourceGetSampleRateHz(source) / 2);
        int16_t samples[2];
        CHECK(LND_SourceRead(source, samples, LND_FORMAT_S16, 1) == 1);
        CHECK(LND_SourceFree(source) == LND_OK);
        CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, 1) == LND_OK);
    }
    unsigned baseline = live_allocations;
    snprintf(url, sizeof url, "%s/stall", base);
    source = LND_SourceCreateHttp(url, 50, &options);
    CHECK(!source && LND_ErrorGetLast() == LND_HTTP_ERR_TIMEOUT);
    CHECK(live_allocations == baseline);
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    for (unsigned worker = 0; worker < 2; worker++) {
        test_blocking(argv[1], worker != 0);
        play(argv[1], "/tone.wav", "audiosamples/tone.wav", nullptr, worker != 0, 0);
        play(argv[1], "/icy", "audiosamples/tone.mp3", nullptr, worker != 0, 0);
        play(argv[1], "/chunked", "audiosamples/tone.mp3", nullptr, worker != 0, 0);
        play(argv[1], "/ua", "audiosamples/tone.wav", nullptr, worker != 0, 0);
        play(argv[1], "/no-agent", "audiosamples/tone.wav", nullptr, worker != 0, 0);
        play(argv[2], "/tone.wav", nullptr, nullptr, worker != 0, LND_ERR_IO);
        play(argv[1], "/cookie", "audiosamples/tone.wav", nullptr, worker != 0, 0);
        play(argv[1], "/cross", "audiosamples/tone.wav", nullptr, worker != 0, 0);
        play(argv[1], "/resume", "audiosamples/tone.wav", nullptr, worker != 0, 0);
        play(argv[1], "/resume-twice", "audiosamples/tone.wav", nullptr, worker != 0, 0);
        play(argv[1], "/changed", nullptr, nullptr, worker != 0, LND_ERR_IO);
        play(argv[1], "/retry", "audiosamples/tone.wav", nullptr, worker != 0, 0);
        play(argv[1], "/missing", nullptr, nullptr, worker != 0, LND_ERR_IO);
        play(argv[1], "/truncated", nullptr, nullptr, worker != 0, LND_ERR_IO);
        play(argv[2], "/tone.wav", "audiosamples/tone.wav", argv[3], worker != 0, 0);
#if LND_MODULE_OPUS_DECODER
        play(argv[1], "/tone.opus", "audiosamples/tone.opus", nullptr, worker != 0, 0);
#if LND_MODULE_METADATA_COMMENTS
        play(argv[1], "/comments.opus", "audiosamples/comments.opus", nullptr, worker != 0, 0);
#endif
        play(argv[1], "/chunked-opus", "audiosamples/tone.opus", nullptr, worker != 0, 0);
        play(argv[2], "/tone.opus", "audiosamples/tone.opus", argv[3], worker != 0, 0);
#endif
#if LND_MODULE_FLAC_DECODER
        play(argv[1], "/tone.flac", "audiosamples/tone.flac", nullptr, worker != 0, 0);
#if LND_MODULE_METADATA_COMMENTS
        play(argv[1], "/comments.flac", "audiosamples/comments.flac", nullptr, worker != 0, 0);
#endif
#endif
#if LND_MODULE_VORBIS_DECODER
        play(argv[1], "/tone.ogg", "audiosamples/tone.ogg", nullptr, worker != 0, 0);
#if LND_MODULE_METADATA_COMMENTS
        play(argv[1], "/comments.ogg", "audiosamples/comments.ogg", nullptr, worker != 0, 0);
#endif
#endif
#if LND_MODULE_AIFF_DECODER && LND_MODULE_HTTP_FILE
        play(argv[1], "/tone.aiff", "audiosamples/tone.aiff", nullptr, worker != 0, 0);
        play(argv[1], "/large-file", "audiosamples/tone.aiff", nullptr, worker != 0, 0);
        play(argv[1], "/chunked-file", "audiosamples/tone.aiff", nullptr, worker != 0, 0);
        play(argv[1], "/file-resume", "audiosamples/tone.aiff", nullptr, worker != 0, 0);
        play(argv[1], "/no-file-cache", nullptr, nullptr, worker != 0, LND_ERR_UNSUPPORTED);
        play(argv[1], "/file-limit", nullptr, nullptr, worker != 0, LND_ERR_UNSUPPORTED);
        play(argv[1], "/bad-cache-dir", nullptr, nullptr, worker != 0, LND_ERR_IO);
#endif
#if LND_MODULE_AAC_DECODER
        play(argv[1], "/tone.aac", "audiosamples/tone.aac", nullptr, worker != 0, 0);
        play(argv[1], "/large.m4a", "audiosamples/tone.m4a", nullptr, worker != 0, 0);
        play(argv[1], "/tone.m4a", "audiosamples/tone.m4a", nullptr, worker != 0, 0);
        play(argv[1], "/hls-master.m3u8", "audiosamples/tone.aac", nullptr, worker != 0, 0);
        play(argv[1], "/hls-ts/index.m3u8", "audiosamples/tone.aac", nullptr, worker != 0, 0);
        play(argv[1], "/hls-mp4/index.m3u8", "audiosamples/tone.aac", nullptr, worker != 0, 0);
        play(argv[1], "/hls-aes/index.m3u8", "audiosamples/tone.aac", nullptr, worker != 0, 0);
#endif
    }
    for (unsigned worker = 0; worker < 2; worker++) {
        char url[4096];
        snprintf(url, sizeof url, "%s/stall", argv[1]);
        LND_HTTP_OPTIONS timeout;
        LND_HttpOptionsInit(&timeout);
        timeout.execution = worker ? LND_HTTP_EXEC_WORKER : LND_HTTP_EXEC_MANUAL;
        timeout.open_timeout_ms = 50;
        LND_HTTP_OPEN *open = LND_HttpOpen(url, &timeout);
        CHECK(open != nullptr);
        LND_HTTP_INFO info = {0};
        uint64_t deadline = lnd_time_ns() + 3000000000ull;
        while (open && info.state != LND_HTTP_FAILED && lnd_time_ns() < deadline) {
            if (!worker) CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, 8) == LND_OK);
            CHECK(LND_HttpOpenGetInfo(open, &info) == LND_OK);
            lnd_sleep_ms(1);
        }
        CHECK(info.state == LND_HTTP_FAILED && info.error.code == LND_HTTP_ERR_TIMEOUT && !info.error.retryable);
        CHECK(LND_HttpOpenFree(open) == LND_OK);
    }
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.execution = LND_HTTP_EXEC_WORKER;
    for (unsigned i = 0; i < 16; i++) {
        char url[4096];
        snprintf(url, sizeof url, "%s/slow", argv[1]);
        LND_HTTP_OPEN *open = LND_HttpOpen(url, &options);
        LND_SOURCE *source = nullptr;
        CHECK(open != nullptr);
        uint64_t deadline = lnd_time_ns() + 3000000000ull;
        while (open && !source && lnd_time_ns() < deadline) {
            int32_t r = LND_HttpOpenTakeSource(open, &source);
            if (r < 0) break;
            lnd_sleep_ms(1);
        }
        CHECK(source != nullptr);
        if (source) CHECK(LND_SourceFree(source) == LND_OK);
        CHECK(LND_HttpOpenFree(open) == LND_OK);
        CHECK(LND_HttpUpdate(lnd_time_ns() / 1000000, 1) == LND_OK);
    }
    for (unsigned i = 0; i < 32; i++) {
        char url[4096];
        snprintf(url, sizeof url, "%s/stall", argv[1]);
        LND_HTTP_OPEN *open = LND_HttpOpen(url, &options);
        CHECK(open != nullptr);
        if (i % 2) CHECK(LND_HttpOpenCancel(open) == LND_OK);
        if (i % 3) CHECK(LND_HttpOpenFree(open) == LND_OK);
    }
    lnd_sleep_ms(30);
    uint64_t start = lnd_time_ns();
    LND_LibraryFree();
    CHECK(lnd_time_ns() - start < 3000000000ull);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
