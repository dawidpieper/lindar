#include "codec_test.h"
#include "lindar_decode.h"
#include "lindar_metadata_io.h"
#include "lindar_monitor.h"
#include "src/thread.h"
#include <math.h>

#if LND_MODULE_MONITOR
static void monitoring(void) {
    float samples[1024] = {0};
    LND_PCM pcm = {.data = samples, .frames = 1024, .channels = 1, .format = LND_FORMAT_F32};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 48000};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_NODE *input = LND_SourceEnsureNode(source), *bus = LND_NodeCreateBus(1, 48000);
    CHECK(LND_NodeConnect(input, bus) == LND_OK);
    CHECK(LND_NodeSetMonitoring(input, true) == LND_OK);
    CHECK(LND_NodeSetMonitoring(bus, true) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(bus);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &pcm, 0, 256) == 256);
    LND_NODE_STATS child, parent;
    CHECK(LND_NodeGetStats(input, &child) == LND_OK && child.enabled && child.calls > 0 && child.frames >= 256);
    CHECK(LND_NodeGetStats(bus, &parent) == LND_OK && parent.render_ns >= child.render_ns && parent.self_ns <= parent.render_ns);
    CHECK(parent.render_percent >= parent.self_percent && parent.peak_ns <= parent.render_ns);
    CHECK(LND_NodeResetStats(bus) == LND_OK);
    CHECK(LND_NodeGetStats(bus, &parent) == LND_OK && parent.calls == 0 && parent.enabled);
    CHECK(LND_NodeSetMonitoring(bus, false) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &pcm, 0, 256) == 256);
    CHECK(LND_NodeGetStats(bus, &parent) == LND_OK && parent.calls == 0 && !parent.enabled);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    CHECK(LND_NodeFree(bus) == LND_OK);
    CHECK(LND_NodeFree(input) == LND_OK);
    CHECK(LND_SoundFree(sound) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
}
#endif

#if LND_MODULE_METADATA_IO && LND_MODULE_METADATA_COMMENTS
static void expect_tags(LND_METADATA *m) {
    const char *title = LND_MetadataGetValue(m, "TITLE", 0), *artist = LND_MetadataGetValue(m, "ARTIST", 0), *album = LND_MetadataGetValue(m, "ALBUM", 0);
    CHECK(title && !strcmp(title, "Lindar"));
    CHECK(artist && !strcmp(artist, "Test"));
    CHECK(album && !strcmp(album, "Album"));
    const LND_METADATA_CHAPTER *chapter = LND_MetadataGetChapter(m, 0);
    CHECK(chapter && chapter->start_us == 1250000 && !strcmp(chapter->title, "Chapter"));
}
static void buffered_tags(const uint8_t *data, size_t bytes) {
    test_input input = {.data = data, .size = bytes}, rejected = input;
    LND_IO *io = LND_IoCreateInput(&input_procs, &input, bytes);
    LND_DECODER *decoder = LND_DecoderCreate(nullptr);
    CHECK(io && decoder);
    CHECK(LND_DecoderTakeIo(decoder, io) == LND_OK);
    LND_IO *other = LND_IoCreateInput(&input_procs, &rejected, bytes);
    CHECK(LND_DecoderTakeIo(decoder, other) == LND_ERR_STATE && !rejected.closes);
    CHECK(LND_IoFree(other) == LND_OK && rejected.closes == 1);
    unsigned reads_before = input.reads;
    uint64_t position = LND_IoGetPositionBytes(io);
    CHECK(LND_DecoderGetMetadataRevision(decoder) == 0 && input.reads == reads_before);
    LND_METADATA *metadata = LND_MetadataCreate(nullptr);
    CHECK(LND_DecoderCopyMetadata(decoder, metadata) == LND_METADATA_ERR_NOT_FOUND && input.reads == reads_before);
    CHECK(LND_DecoderLoadMetadata(decoder) == LND_OK);
    CHECK(input.reads > reads_before && LND_IoGetPositionBytes(io) == position);
    reads_before = input.reads;
    uint64_t revision = LND_DecoderGetMetadataRevision(decoder);
    CHECK(revision != 0 && input.reads == reads_before);
    CHECK(LND_DecoderLoadMetadata(decoder) == LND_OK && input.reads == reads_before);
    CHECK(LND_DecoderGetMetadataRevision(decoder) == revision);
    CHECK(LND_DecoderCopyMetadata(decoder, metadata) == LND_OK);
    expect_tags(metadata);
    CHECK(input.reads == reads_before);
    LND_MetadataFree(metadata);
    LND_DecoderFree(decoder);
    CHECK(input.closes == 1);
}

static void tags(const char *file) {
    size_t bytes;
    uint8_t *data = read_file(file, &bytes);
    buffered_tags(data, bytes);
    LND_METADATA *m = LND_MetadataCreate(nullptr);
    CHECK(LND_MetadataReadMemory(m, data, bytes, LND_METADATA_AUTO) == LND_OK);
    expect_tags(m);
    LND_ENCODED_SOURCE_OPTIONS file_options = {.metadata_flags = LND_ENCODED_SOURCE_REQUIRE_METADATA};
    LND_SOURCE *source = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &file_options);
    CHECK(source != nullptr);
    CHECK(LND_SourceCopyMetadata(source, m) == LND_OK);
    expect_tags(m);
    LND_SOURCE_INFO info;
    CHECK(LND_SourceGetInfo(source, &info) == LND_OK && info.bitrate_bps > 0 && info.bitrate_estimated);
    CHECK(LND_SourceFree(source) == LND_OK);
    LND_DECODER_OPTIONS options = {.input_bytes = 8192};
    LND_DECODER *d = LND_DecoderCreate(&options);
    float samples[1024];
    LND_PCM pcm = {.data = samples, .frames = 1024, .channels = 1, .format = LND_FORMAT_F32};
    size_t offset = 0;
    for (unsigned i = 0; i < bytes * 2 && (LND_DecoderLoadMetadata(d), !LND_DecoderGetMetadataRevision(d)); ++i) {
        size_t n = bytes - offset < 7 ? bytes - offset : 7;
        int64_t fed = LND_DecoderFeed(d, data + offset, n);
        if (fed < 0) {
            CHECK(fed >= 0);
            break;
        }
        offset += (size_t)fed;
        int64_t got = LND_DecoderReadPcm(d, &pcm, 0, 1024);
        if (got < 0) {
            CHECK(got >= 0);
            break;
        }
    }
    CHECK(LND_DecoderGetMetadataRevision(d) != 0);
    CHECK(LND_DecoderLoadMetadata(d) == LND_OK);
    CHECK(LND_DecoderCopyMetadata(d, m) == LND_OK);
    expect_tags(m);
    LND_DecoderFree(d);
    LND_MetadataFree(m);
    free(data);
}
#endif

#if LND_MODULE_FFMPEG_STREAM
static void container(const char *file) {
    size_t bytes;
    uint8_t *data = read_file(file, &bytes);
    LND_DECODER_OPTIONS options = {.input_bytes = 4096};
    LND_DECODER *d = LND_DecoderCreate(&options);
    CHECK(d != nullptr);
    size_t offset = 0;
    uint64_t frames = 0, deadline = lnd_time_ns() + 10000000000ULL;
    bool early = false, ended = false;
    float samples[1024];
    LND_PCM pcm = {.data = samples, .frames = 1024, .channels = 1, .format = LND_FORMAT_F32};
    while (lnd_time_ns() < deadline) {
        if (offset < bytes) {
            size_t n = bytes - offset < 1021 ? bytes - offset : 1021;
            int64_t fed = LND_DecoderFeed(d, data + offset, n);
            if (fed < 0) {
                printf("%s feed=%lld\n", file, (long long)fed);
                CHECK(fed >= 0);
                break;
            }
            offset += (size_t)fed;
        } else if (!ended) {
            CHECK(LND_DecoderEnd(d) == LND_OK);
            ended = true;
        }
        int64_t got = LND_DecoderReadPcm(d, &pcm, 0, 1024);
        if (got < 0) {
            printf("%s read=%lld\n", file, (long long)got);
            CHECK(got >= 0);
            break;
        }
        if (got) {
            frames += (uint64_t)got;
            early |= offset < bytes;
            for (int64_t i = 0; i < got; ++i)
                if (!isfinite(samples[i])) {
                    CHECK(isfinite(samples[i]));
                    break;
                }
        }
        if (LND_DecoderGetStatus(d) == LND_SOURCE_EOF) break;
        lnd_sleep_ms(1);
    }
    printf("%s: frames=%llu early=%d status=%d\n", file, (unsigned long long)frames, early, LND_DecoderGetStatus(d));
    CHECK(early && frames > 180000 && frames < 200000);
    CHECK(LND_DecoderGetStatus(d) == LND_SOURCE_EOF);
#if LND_MODULE_METADATA
    LND_METADATA *m = LND_MetadataCreate(nullptr);
    CHECK(LND_DecoderLoadMetadata(d) == LND_OK);
    CHECK(LND_DecoderCopyMetadata(d, m) == LND_OK);
    const char *title = LND_MetadataGetValue(m, "TITLE", 0);
    CHECK(title && !strcmp(title, "Lindar"));
    LND_MetadataFree(m);
#endif
    LND_DecoderFree(d);
    d = LND_DecoderCreate(&options);
    CHECK(LND_DecoderFeed(d, data, bytes < 4096 ? bytes : 4096) > 0);
    CHECK(LND_DecoderStep(d) >= 0);
    lnd_sleep_ms(20);
    uint64_t start = lnd_time_ns();
    LND_DecoderFree(d);
    CHECK(lnd_time_ns() - start < 1000000000ULL);
    free(data);
}
#endif

int main(void) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
#if LND_MODULE_MONITOR
    monitoring();
#endif
#if LND_MODULE_METADATA_IO && LND_MODULE_METADATA_COMMENTS && LND_MODULE_VORBIS_DECODER && LND_MODULE_FLAC_DECODER
    tags("audiosamples/comments.ogg");
    tags("audiosamples/comments.flac");
#if LND_MODULE_OPUS_DECODER
    tags("audiosamples/comments.opus");
#endif
#endif
#if LND_MODULE_FFMPEG_STREAM
    container("audiosamples/tone.webm");
    container("audiosamples/tone.mka");
    container("audiosamples/stream.wma");
#endif
    LND_LibraryFree();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
