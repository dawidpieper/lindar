#include "codec_test.h"
#include "network/http/http_hls/playlist.h"

static void test_urls(void) {
    const char *base = "https://example.test/a/b/list.m3u8?token=1";
    const char *pairs[][2] = {{"../c/./one.aac", "https://example.test/a/c/one.aac"},
                              {"/root.aac", "https://example.test/root.aac"},
                              {"//cdn.test/one.aac", "https://cdn.test/one.aac"},
                              {"?token=2", "https://example.test/a/b/list.m3u8?token=2"},
                              {".", "https://example.test/a/b/"},
                              {"..", "https://example.test/a/"},
                              {"a/..", "https://example.test/a/b/"},
                              {"https://cdn.test/a/../two.aac", "https://cdn.test/two.aac"}};
    char url[4096];
    for (size_t i = 0; i < sizeof pairs / sizeof *pairs; i++) {
        CHECK(lnd_http_url_resolve(base, pairs[i][0], url, sizeof url) == LND_OK);
        CHECK(!strcmp(url, pairs[i][1]));
    }
    CHECK(lnd_http_url_resolve(base, "file:///etc/passwd", url, sizeof url) < 0);
    CHECK(!lnd_http_same_origin(base, "https://other.test/"));
    CHECK(lnd_http_same_origin(base, "HTTPS://EXAMPLE.TEST:443/a"));
    CHECK(!lnd_http_same_origin(base, "https://example.test:444/a"));
    CHECK(!lnd_http_url_valid("http://test:99999/a"));
}

static void test_media(void) {
    const char *data = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:41\n"
                       "#EXT-X-PROGRAM-DATE-TIME:2026-09-11T14:23:45.123456+02:00\n"
                       "#EXT-X-MAP:URI=\"init.mp4\",BYTERANGE=\"128@0\"\n"
                       "#EXT-X-KEY:METHOD=AES-128,URI=\"../key\"\n"
                       "#EXTINF:5.123456,first\n#EXT-X-BYTERANGE:100@128\nmedia.mp4\n"
                       "#EXT-X-DISCONTINUITY\n#EXTINF:5.000001,second\n#EXT-X-BYTERANGE:120\nmedia.mp4\n#EXT-X-ENDLIST\n";
    lnd_hls_playlist p = {0};
    CHECK(lnd_hls_parse((const uint8_t *)data, strlen(data), "https://test/a/live.m3u8", 32, &p) == LND_OK);
    if (p.count == 2) {
        CHECK(p.end && p.sequence == 41 && p.duration_us == 10123457);
        CHECK(p.segments[0].sequence == 41 && p.segments[1].sequence == 42);
        CHECK(p.segments[0].iv[15] == 41 && p.segments[1].iv[15] == 42);
        CHECK(p.segments[1].discontinuity == 1);
        CHECK(p.segments[0].program_us == 1789129425123456ll);
        CHECK(p.segments[1].program_us - p.segments[0].program_us == 5123456);
        CHECK(p.segments[1].range.offset == 228);
        CHECK(!strcmp(p.segments[0].map, "https://test/a/init.mp4"));
        CHECK(!strcmp(p.segments[0].key, "https://test/key"));
    } else
        CHECK(false);
    lnd_hls_playlist_free(&p);
}

static void test_master(void) {
    const char *data = "#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"Polski\",LANGUAGE=\"pl\",DEFAULT=YES,URI=\"pl.m3u8\"\n"
                       "#EXT-X-STREAM-INF:BANDWIDTH=96000,CODECS=\"mp4a.40.2\",AUDIO=\"audio\"\nlow.m3u8\n"
                       "#EXT-X-STREAM-INF:BANDWIDTH=256000,CODECS=\"mp4a.40.2\"\nhigh.m3u8\n";
    lnd_hls_playlist p = {0};
    CHECK(lnd_hls_parse((const uint8_t *)data, strlen(data), "https://test/main.m3u8", 32, &p) == LND_OK);
    CHECK(p.variant_count == 2 && p.audio_count == 1);
    if (p.variant_count == 2) CHECK(p.variants[0].bitrate_bps == 96000 && !strcmp(p.variants[1].url, "https://test/high.m3u8"));
    if (p.audio_count) CHECK(p.audio[0].default_track && !strcmp(p.audio[0].language, "pl"));
    lnd_hls_playlist_free(&p);
}

static void test_low_latency(void) {
    const char *data = "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:10\n"
                       "#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES,PART-HOLD-BACK=1.5,CAN-SKIP-UNTIL=12\n"
                       "#EXT-X-PART-INF:PART-TARGET=0.5\n#EXT-X-SKIP:SKIPPED-SEGMENTS=2\n"
                       "#EXT-X-PART:DURATION=0.5,URI=\"part0.m4s\",INDEPENDENT=YES\n"
                       "#EXT-X-PART:DURATION=0.5,URI=\"part1.m4s\"\n"
                       "#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"part2.m4s\"\n"
                       "#EXT-X-RENDITION-REPORT:URI=\"other.m3u8\",LAST-MSN=12,LAST-PART=1\n";
    lnd_hls_playlist p = {0};
    CHECK(lnd_hls_parse((const uint8_t *)data, strlen(data), "https://test/live.m3u8", 32, &p) == LND_OK);
    CHECK(p.blocking && p.count == 2 && p.part_target_us == 500000 && p.part_hold_back_us == 1500000);
    if (p.count == 2) CHECK(p.segments[0].sequence == 12 && p.segments[1].part == 1 && p.segments[1].start_us == 500000);
    CHECK(p.preload && !strcmp(p.preload, "https://test/part2.m4s"));
    CHECK(p.report_count == 1 && p.reports[0].sequence == 12 && p.reports[0].part == 1);
    lnd_hls_playlist_free(&p);
}

static void test_invalid(void) {
    const char *cases[] = {
        "#EXTM3U\n#EXT-X-TARGETDURATION:9223372036854\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-SERVER-CONTROL:HOLD-BACK=-1\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-PROGRAM-DATE-TIME:2025-02-29T12:00:00Z\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"key\"\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:1,\n#EXT-X-BYTERANGE:2\na.aac\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:1,\nfile:///etc/passwd\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:18446744073709551616\n",
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:1,\n",
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        lnd_hls_playlist p = {0};
        CHECK(lnd_hls_parse((const uint8_t *)cases[i], strlen(cases[i]), "https://test/a.m3u8", 32, &p) < 0);
    }
    int64_t time;
    CHECK(lnd_hls_seconds("1.000001", &time) && time == 1000001);
    CHECK(lnd_hls_seconds("-1.5", &time) && time == -1500000);
    CHECK(!lnd_hls_seconds("999999999999999999999", &time));
}

static void test_growth_and_rollback(void) {
    char text[32768];
    for (unsigned kind = 0; kind < 3; kind++) {
        size_t bytes = (size_t)snprintf(text, sizeof text, "#EXTM3U\n%s", kind == 2 ? "#EXT-X-TARGETDURATION:6\n#EXTINF:1,\na.aac\n" : "");
        for (unsigned i = 0; i < 70; i++) {
            const char *pattern = kind == 0 ? "#EXT-X-STREAM-INF:BANDWIDTH=96000,CODECS=\"opus\",AUDIO=\"a\"\nv%u.m3u8\n"
                                  : kind == 1 ? "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"n%u\",LANGUAGE=\"pl\",URI=\"a.m3u8\"\n"
                                              : "#EXT-X-RENDITION-REPORT:URI=\"v%u.m3u8\",LAST-MSN=1,LAST-PART=0\n";
            bytes += (size_t)snprintf(text + bytes, sizeof text - bytes, pattern, i);
        }
        if (kind == 1) bytes += (size_t)snprintf(text + bytes, sizeof text - bytes, "#EXT-X-STREAM-INF:BANDWIDTH=96000\na.m3u8\n");
        lnd_hls_playlist playlist = {0};
        CHECK(lnd_hls_parse((const uint8_t *)text, bytes, "https://test/index.m3u8", 96, &playlist) == LND_OK);
        CHECK(kind == 0 ? playlist.variant_count == 70 && playlist.variant_capacity == 96
              : kind == 1 ? playlist.audio_count == 70 && playlist.audio_capacity == 96
                          : playlist.report_count == 70 && playlist.report_capacity == 96);
        lnd_hls_playlist_free(&playlist);
        CHECK(lnd_hls_parse((const uint8_t *)text, bytes, "https://test/index.m3u8", 32, &playlist) == LND_ERR_FORMAT);
        bool success = false;
        unsigned baseline = live_allocations;
        for (int fault = 0; fault < 600 && !success; fault++) {
            lnd_hls_playlist result = {.duration_us = 1234};
            fail_allocation = fault;
            int32_t status = lnd_hls_parse((const uint8_t *)text, bytes, "https://test/index.m3u8", 96, &result);
            fail_allocation = -1;
            if (status == LND_OK) {
                success = true;
                lnd_hls_playlist_free(&result);
            } else {
                CHECK(status == LND_ERR_OUT_OF_MEMORY);
                CHECK(result.duration_us == 1234 && !result.segments && !result.variants && !result.audio && !result.reports);
            }
            CHECK(live_allocations == baseline);
        }
        CHECK(success);
    }
}

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    test_urls();
    test_media();
    test_master();
    test_low_latency();
    test_invalid();
    test_growth_and_rollback();
    LND_LibraryFree();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
