#include "codec_test.h"
#include "lindar_http.h"

typedef struct resource {
    const char *name;
    const uint8_t *data;
    size_t bytes;
} resource;

typedef struct network {
    resource entries[32];
    uint32_t count;
    uint32_t requests;
    uint32_t closes;
    uint64_t now;
    const char *versions[2];
    uint32_t playlist_reads;
    unsigned mode;
} network;

typedef struct fetch {
    network *network;
    resource *resource;
    size_t position;
    unsigned polls;
} fetch;

static int32_t begin(void *user, const LND_HTTP_REQUEST *request, void **out) {
    network *n = user;
    CHECK(!strcmp(request->user_agent, "HlsTest"));
    const char *name = strrchr(request->url, '/');
    if (!name) return LND_ERR_IO;
    name++;
    size_t length = strcspn(name, "?");
    for (uint32_t i = 0; i < n->count; i++) {
        if (strlen(n->entries[i].name) != length || memcmp(name, n->entries[i].name, length)) continue;
        if (n->versions[0] && !strcmp(n->entries[i].name, "live.m3u8")) {
            const char *text = n->versions[n->playlist_reads++ ? 1 : 0];
            n->entries[i].data = (const uint8_t *)text;
            n->entries[i].bytes = strlen(text);
        }
        fetch *f = calloc(1, sizeof *f);
        if (!f) return LND_ERR_OUT_OF_MEMORY;
        f->network = n;
        f->resource = &n->entries[i];
        n->requests++;
        *out = f;
        return LND_OK;
    }
    return LND_ERR_IO;
}

static int32_t poll(void *handle, LND_HTTP_RESPONSE *response, void *data, size_t capacity, size_t *written) {
    fetch *f = handle;
    *response = (LND_HTTP_RESPONSE){.status = 200, .headers_complete = true, .length_known = true, .content_length_bytes = f->resource->bytes};
    if (strstr(f->resource->name, ".m3u8")) snprintf(response->content_type, sizeof response->content_type, "application/vnd.apple.mpegurl");
    *written = 0;
    if (f->network->mode == 5 && f->network->playlist_reads == 1 && !strcmp(f->resource->name, "whole.aac")) {
        response->status = 404;
        return LND_HTTP_DONE;
    }
    if (f->network->mode == 6 && f->network->requests == 2) return LND_HTTP_PENDING;
    if (++f->polls % 3 == 0) return LND_HTTP_PENDING;
    size_t n = f->resource->bytes - f->position;
    if (n > capacity) n = capacity;
    if (n > 113) n = 113;
    memcpy(data, f->resource->data + f->position, n);
    f->position += n;
    *written = n;
    return f->position == f->resource->bytes ? LND_HTTP_DONE : LND_HTTP_PENDING;
}

static void finish(void *handle) {
    fetch *f = handle;
    f->network->closes++;
    free(f);
}

static const LND_HTTP_TRANSPORT transport = {
    .size = sizeof(LND_HTTP_TRANSPORT), .capabilities = LND_HTTP_CAP_HTTP | LND_HTTP_CAP_MANUAL, .open = begin, .poll = poll, .close = finish};

#if LND_MODULE_AAC_DECODER
static void playback(unsigned mode) {
    bool master = mode != 0, switched = false;
    size_t bytes = 0;
    uint8_t *data = read_file("audiosamples/tone.aac", &bytes);
    if (!data) return;
    size_t cuts[4] = {0};
    size_t at = 0;
    uint32_t frames = 0;
    while (at + 7 <= bytes) {
        size_t size = (size_t)(data[at + 3] & 3) << 11 | (size_t)data[at + 4] << 3 | data[at + 5] >> 5;
        CHECK(size >= 7 && size <= bytes - at);
        if (size < 7 || size > bytes - at) break;
        at += size;
        if (++frames == 30) cuts[1] = at;
        if (frames == 60) cuts[2] = at;
    }
    cuts[3] = bytes;
    CHECK(frames > 60 && at == bytes);
    char playlist[1024];
    snprintf(playlist, sizeof playlist,
             "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:11\n"
             "#EXTINF:%.9f,\none.aac\n#EXTINF:%.9f,\ntwo.aac\n#EXTINF:%.9f,\nthree.aac\n#EXT-X-ENDLIST\n",
             30.0 * 1024 / 44100, 30.0 * 1024 / 44100, (frames - 60.0) * 1024 / 44100);
    const char *master_text = "#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",LANGUAGE=\"pl\",NAME=\"Polski\",URI=\"audio.m3u8\"\n"
                              "#EXT-X-STREAM-INF:BANDWIDTH=128000,CODECS=\"mp4a.40.2\",AUDIO=\"a\"\nmissing.m3u8\n";
    if (mode == 2)
        master_text = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=64000,CODECS=\"mp4a.40.2\"\naudio.m3u8\n"
                      "#EXT-X-STREAM-INF:BANDWIDTH=256000,CODECS=\"mp4a.40.2\"\nhigh.m3u8\n";
    LND_CODEC custom;
    if (mode == 3) {
        custom = *LND_CodecFind("aac");
        custom.name = "hls-custom";
        custom.stream_identifiers = "lindar.test.audio";
        CHECK(LND_CodecRegister(&custom) == LND_OK);
        master_text = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=128000,CODECS=\"avc1.42E01E, lindar.test.audio\"\naudio.m3u8\n";
    }
    network n = {.count = mode == 2 ? 6 : 5};
    n.entries[0] = (resource){"master.m3u8", (const uint8_t *)master_text, strlen(master_text)};
    n.entries[1] = (resource){"audio.m3u8", (const uint8_t *)playlist, strlen(playlist)};
    n.entries[2] = (resource){"one.aac", data, cuts[1]};
    n.entries[3] = (resource){"two.aac", data + cuts[1], cuts[2] - cuts[1]};
    n.entries[4] = (resource){"three.aac", data + cuts[2], bytes - cuts[2]};
    n.entries[5] = (resource){"high.m3u8", (const uint8_t *)playlist, strlen(playlist)};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &transport;
    options.transport_user = &n;
    options.user_agent = "HlsTest";
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    options.hls.preferred_language = "pl";
    if (mode == 2) options.hls.max_bitrate_bps = 64000;
    if (mode == 3) options.codec_name = custom.name;
    LND_HTTP_OPEN *open = LND_HttpOpen(master ? "http://test/master.m3u8" : "http://test/audio.m3u8", &options);
    CHECK(open != nullptr);
    if (mode == 3) CHECK(LND_CodecUnregister(&custom) == LND_OK);
    LND_SOURCE *source = nullptr;
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
    CHECK(reference != nullptr);
    int16_t out[514], expected[514];
    uint64_t total = 0;
    for (uint32_t i = 0; i < 100000; i++) {
        CHECK(LND_HttpUpdate(n.now++, 16) == LND_OK);
        if (!source) {
            int32_t r = LND_HttpOpenTakeSource(open, &source);
            CHECK(r == LND_OK || r == LND_HTTP_PENDING);
            if (r < 0) break;
        }
        if (!source) continue;
        uint32_t channels = LND_SourceGetChannels(source);
        CHECK(channels <= 2);
        LND_PCM pcm = {.data = out, .frames = 257, .channels = channels, .format = LND_FORMAT_S16, .layout = LND_LAYOUT_INTERLEAVED};
        int64_t got = LND_SourceReadPcm(source, &pcm, 0, 257);
        CHECK(got >= 0);
        if (got < 0) break;
        uint64_t count = LND_SourceRead(reference, expected, LND_FORMAT_S16, (uint64_t)got);
        CHECK(count == (uint64_t)got);
        if (count == (uint64_t)got) CHECK(!memcmp(out, expected, (size_t)got * channels * 2));
        total += (uint64_t)got;
        if (mode == 2 && got && !switched) {
            CHECK(LND_SourceSetHttpVariant(source, 256000, nullptr) == LND_OK);
            switched = true;
        }
        if (LND_SourceGetStatus(source) == LND_SOURCE_EOF) break;
    }
    CHECK(total == (uint64_t)frames * 1024);
    CHECK(LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    LND_HTTP_INFO info;
    CHECK(LND_SourceGetHttpInfo(source, &info) == LND_OK && info.hls && !info.live && info.seekable);
    if (mode == 2) {
        CHECK(switched && n.requests > 5);
        CHECK(info.bitrate_bps == 256000);
    } else
        CHECK(n.requests == (master ? 5u : 4u));
    uint64_t id = 0;
    CHECK(LND_SourceSeekHttpMicroseconds(source, 0, &id) == LND_OK && id);
    CHECK(LND_SourceSeekFrames(reference, 0) == LND_OK);
    bool played = false, completed = false;
    for (uint32_t i = 0; i < 10000 && !played; i++) {
        CHECK(LND_HttpUpdate(n.now++, 16) == LND_OK);
        LND_HTTP_EVENT event;
        while (LND_SourcePollHttpEvent(source, &event) == LND_OK)
            if (event.type == LND_HTTP_EVENT_SEEK && event.request_id == id) completed = true;
        uint64_t got = LND_SourceRead(source, out, LND_FORMAT_S16, 257);
        if (got) {
            CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, got) == got);
            CHECK(!memcmp(out, expected, (size_t)got * LND_SourceGetChannels(source) * 2));
            played = true;
        }
    }
    CHECK(played && completed);
    CHECK(LND_HttpOpenFree(open) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(LND_SourceFree(reference) == LND_OK);
    CHECK(LND_HttpUpdate(n.now++, 1) == LND_OK);
    CHECK(n.requests == n.closes);
    free(data);
}
#endif

static const char *forced_codec;

static void containers(const char *folder, const char *reference_path, const char *extension) {
    network n = {0};
    char names[32][64], path[256];
    snprintf(path, sizeof path, "audiosamples/%s/index.m3u8", folder);
    size_t bytes;
    uint8_t *playlist = read_file(path, &bytes);
    CHECK(playlist != nullptr);
    if (!playlist) return;
    n.entries[n.count++] = (resource){"index.m3u8", playlist, bytes};
    if (!strcmp(extension, "m4s")) {
        snprintf(path, sizeof path, "audiosamples/%s/init.mp4", folder);
        uint8_t *init = read_file(path, &bytes);
        CHECK(init != nullptr);
        n.entries[n.count++] = (resource){"init.mp4", init, bytes};
    }
    for (uint32_t i = 0; i < 4; i++) {
        snprintf(names[i], sizeof names[i], "seg%u.%s", i, extension);
        snprintf(path, sizeof path, "audiosamples/%s/%s", folder, names[i]);
        uint8_t *segment = read_file(path, &bytes);
        CHECK(segment != nullptr);
        n.entries[n.count++] = (resource){names[i], segment, bytes};
    }
    uint8_t *file = read_file(reference_path, &bytes);
    LND_ENCODED_SOURCE_OPTIONS reference_options = {.codec_name = forced_codec};
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(file, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &reference_options);
    CHECK(reference != nullptr);
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &transport;
    options.transport_user = &n;
    options.user_agent = "HlsTest";
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    options.codec_name = forced_codec;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/index.m3u8", &options);
    CHECK(open != nullptr);
    LND_SOURCE *source = nullptr;
    int16_t out[514], expected[514];
    uint64_t total = 0, differences = 0;
    for (uint32_t i = 0; i < 100000; i++) {
        CHECK(LND_HttpUpdate(n.now++, 16) == LND_OK);
        if (!source) {
            int32_t r = LND_HttpOpenTakeSource(open, &source);
            if (r < 0) {
                printf("%s open: %d\n", folder, r);
                CHECK(false);
                break;
            }
        }
        if (!source) continue;
        LND_PCM pcm = {.data = out, .frames = 257, .channels = LND_SourceGetChannels(source), .format = LND_FORMAT_S16, .layout = LND_LAYOUT_INTERLEAVED};
        int64_t got = LND_SourceReadPcm(source, &pcm, 0, 257);
        if (got < 0) {
            printf("%s read: %lld\n", folder, (long long)got);
            CHECK(false);
            break;
        }
        uint64_t count = LND_SourceRead(reference, expected, LND_FORMAT_S16, (uint64_t)got);
        CHECK(count == (uint64_t)got);
        if (count == (uint64_t)got) {
            for (size_t j = 0; j < (size_t)got * pcm.channels; j++)
                if (out[j] != expected[j]) differences++;
        }
        total += (uint64_t)got;
        if (LND_SourceGetStatus(source) == LND_SOURCE_EOF) break;
    }
    uint64_t remaining = LND_SourceRead(reference, expected, LND_FORMAT_S16, 257);
    printf("%s: %llu frames, %llu differing samples, %llu remaining\n", folder, (unsigned long long)total, (unsigned long long)differences,
           (unsigned long long)remaining);
    CHECK(total > 80000 && !differences && !remaining);
    CHECK(source && LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    LND_HttpOpenFree(open);
    LND_SourceFree(source);
    LND_SourceFree(reference);
    LND_HttpUpdate(n.now++, 1);
    CHECK(n.requests == n.closes);
    for (uint32_t i = 0; i < n.count; i++) free((void *)n.entries[i].data);
    free(file);
}

#if LND_MODULE_AAC_DECODER
static void live_playlists(unsigned mode) {
    size_t bytes;
    uint8_t *data = read_file("audiosamples/tone.aac", &bytes);
    size_t cut = 0, half = 0;
    unsigned frames = 0;
    for (size_t at = 0; at + 7 <= bytes;) {
        at += (size_t)(data[at + 3] & 3) << 11 | (size_t)data[at + 4] << 3 | data[at + 5] >> 5;
        if (++frames == 22) half = at;
        if (frames == 44) cut = at;
    }
    char first[1024], second[2048];
    double part = 22.0 * 1024 / 44100, full = 44.0 * 1024 / 44100, tail = (frames - 44.0) * 1024 / 44100;
    if (mode == 2 || mode == 3) {
        snprintf(first, sizeof first,
                 "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
                 "#EXT-X-PART-INF:PART-TARGET=0.6\n#EXT-X-PART:DURATION=%.9f,URI=\"part0.aac\",INDEPENDENT=YES\n"
                 "#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"part1.aac\"\n",
                 part);
        snprintf(second, sizeof second,
                 "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
                 "#EXT-X-PART-INF:PART-TARGET=0.6\n#EXT-X-PART:DURATION=%.9f,URI=\"part0.aac\",INDEPENDENT=YES\n"
                 "#EXT-X-PART:DURATION=%.9f,URI=\"part1.aac\"\n#EXTINF:%.9f,\nwhole.aac\n"
                 "#EXTINF:%.9f,\ntail.aac\n#EXT-X-ENDLIST\n",
                 part, part, full, tail);
    } else {
        snprintf(first, sizeof first,
                 "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
                 "#EXT-X-SERVER-CONTROL:CAN-SKIP-UNTIL=12\n#EXTINF:%.9f,\nwhole.aac\n",
                 full);
        if (mode == 1)
            snprintf(second, sizeof second,
                     "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
                     "#EXT-X-SKIP:SKIPPED-SEGMENTS=1\n#EXTINF:%.9f,\ntail.aac\n#EXT-X-ENDLIST\n",
                     tail);
        else
            snprintf(second, sizeof second,
                     "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
                     "#EXTINF:%.9f,\nwhole.aac\n#EXTINF:%.9f,\ntail.aac\n#EXT-X-ENDLIST\n",
                     full, tail);
    }
    if (mode == 3)
        snprintf(second, sizeof second,
                 "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
                 "#EXTINF:%.9f,\nwhole.aac\n#EXTINF:%.9f,\ntail.aac\n#EXT-X-ENDLIST\n",
                 full, tail);
    if (mode == 4)
        snprintf(first, sizeof first,
                 "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
                 "#EXT-X-SKIP:SKIPPED-SEGMENTS=1\n#EXTINF:%.9f,\ntail.aac\n",
                 tail);
    network n = {.count = 5, .versions = {first, second}, .mode = mode, .now = 10000000000ull + mode * 1000000ull};
    n.entries[0] = (resource){"live.m3u8", (const uint8_t *)first, strlen(first)};
    n.entries[1] = (resource){"whole.aac", data, cut};
    n.entries[2] = (resource){"tail.aac", data + cut, bytes - cut};
    n.entries[3] = (resource){"part0.aac", data, half};
    n.entries[4] = (resource){"part1.aac", data + half, cut - half};
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &transport;
    options.transport_user = &n;
    options.user_agent = "HlsTest";
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.hls.low_latency = mode == 2 || mode == 3;
    if (mode == 6) {
        options.retry.receive_timeout_ms = 5;
        options.retry.delay_ms = 1;
    }
    options.buffer.start_ms = options.buffer.resume_ms = 0;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://test/live.m3u8", &options);
    LND_SOURCE *source = nullptr, *reference = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
    CHECK(open && reference);
    uint64_t total = 0, differences = 0;
    for (unsigned i = 0; i < 100000; i++) {
        CHECK(LND_HttpUpdate(n.now++, 16) == LND_OK);
        if (!source) {
            int32_t r = LND_HttpOpenTakeSource(open, &source);
            if (r < 0) {
                printf("live %u open %d\n", mode, r);
                CHECK(false);
                break;
            }
        }
        if (!source) continue;
        int16_t output[512], expected[512];
        LND_PCM pcm = {.data = output, .frames = 256, .channels = 2, .format = LND_FORMAT_S16};
        int64_t got = LND_SourceReadPcm(source, &pcm, 0, 256);
        if (got < 0) {
            printf("live %u read %lld\n", mode, (long long)got);
            CHECK(false);
            break;
        }
        CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, (uint64_t)got) == (uint64_t)got);
        for (size_t j = 0; j < (size_t)got * 2; j++) differences += output[j] != expected[j];
        total += (uint64_t)got;
        if (LND_SourceGetStatus(source) == LND_SOURCE_EOF) break;
    }
    printf("live %u: %llu frames, %llu differing, %u requests\n", mode, (unsigned long long)total, (unsigned long long)differences, n.requests);
    CHECK(total == (uint64_t)frames * 1024 && !differences);
    CHECK(source && LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    CHECK(n.playlist_reads == 2);
    if (mode == 2) CHECK(n.requests == 5);
    LND_HttpOpenFree(open);
    LND_SourceFree(source);
    LND_SourceFree(reference);
    LND_HttpUpdate(n.now++, 1);
    CHECK(n.requests == n.closes);
    free(data);
}
#endif

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
#if LND_MODULE_AAC_DECODER
    playback(false);
    playback(true);
    playback(2);
    playback(3);
    live_playlists(0);
    live_playlists(1);
    live_playlists(2);
    live_playlists(3);
    live_playlists(4);
    live_playlists(5);
    live_playlists(6);
    containers("hls-ts", "audiosamples/tone.aac", "ts");
    containers("hls-mp4", "audiosamples/tone.aac", "m4s");
    LND_CODEC alias = *LND_CodecFind("aac");
    alias.name = "registered-aac";
    CHECK(LND_CodecRegister(&alias) == LND_OK);
    forced_codec = alias.name;
    containers("hls-mp4", "audiosamples/tone.aac", "m4s");
    forced_codec = nullptr;
    CHECK(LND_CodecUnregister(&alias) == LND_OK);

#endif
#if LND_MODULE_OPUS_DECODER
    containers("hls-opus", "audiosamples/tone.opus", "m4s");
#endif
#if LND_MODULE_FLAC_DECODER
    containers("hls-flac", "audiosamples/tone.flac", "m4s");
#endif
#if LND_MODULE_FFMPEG_DECODER
    forced_codec = "ffmpeg";
    containers("hls-mp4", "audiosamples/tone.aac", "m4s");
    containers("hls-opus", "audiosamples/tone.opus", "m4s");
    containers("hls-flac", "audiosamples/tone.flac", "m4s");
    containers("hls-alac", "audiosamples/tone.http-alac.m4a", "m4s");
    containers("hls-ac3", "audiosamples/tone.http.ac3", "ts");
#endif
    LND_LibraryFree();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
