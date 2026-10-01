#include "codec_test.h"
#include "network/http/http.h"
#if LND_MODULE_METADATA_ID3V2
#include "lindar_metadata_id3v2.h"
#endif

static size_t tag(uint8_t *data, const uint8_t *text, size_t bytes, bool pts) {
    memcpy(data, "ID3\4\0\0\0\0\0\0", 10);
    size_t at = 10;
    memcpy(data + at, "TIT2\0\0\0\0\0\0", 10);
    data[at + 7] = (uint8_t)bytes;
    memcpy(data + at + 10, text, bytes);
    at += 10 + bytes;
    if (pts) {
        static const char owner[] = "com.apple.streaming.transportStreamTimestamp";
        memcpy(data + at, "PRIV\0\0\0\0\0\0", 10);
        data[at + 7] = sizeof owner + 8;
        memcpy(data + at + 10, owner, sizeof owner);
        uint64_t value = 90000;
        for (unsigned i = 0; i < 8; i++) data[at + 10 + sizeof owner + 7 - i] = (uint8_t)(value >> (i * 8));
        at += 10 + sizeof owner + 8;
    }
    size_t body = at - 10;
    for (unsigned i = 0; i < 4; i++) data[9 - i] = (uint8_t)((body >> (7 * i)) & 127);
    return at;
}

static void check_shared_id3(const uint8_t *data, size_t bytes, const char *expected) {
    lnd_http_session session = {0};
    lnd_http_metadata queued[2] = {0};
    session.options.buffer.event_count = 2;
    session.metadata = queued;
    lnd_http_id3(&session, data, bytes, 0);
    CHECK(expected ? session.metadata_count == 1 && !strcmp(queued[0].title, expected) : session.metadata_count == 0);
#if LND_MODULE_METADATA_ID3V2
    LND_METADATA *metadata = LND_MetadataCreate(nullptr);
    CHECK(metadata != nullptr);
    size_t consumed = 0;
    int32_t result = LND_MetadataId3v2Read(metadata, data, bytes, &consumed);
    CHECK(expected ? result == LND_OK && consumed == bytes : result != LND_OK);
    if (expected && result == LND_OK) {
        const char *value = LND_MetadataGetValue(metadata, "TITLE", 0);
        CHECK(value && !strcmp(value, expected));
    }
    LND_MetadataFree(metadata);
#endif
    lnd_http_metadata_clear(&session);
}

static void test_shared_id3(void) {
    uint8_t data[512];
    const uint8_t text[] = {0, 'A', 255, 0, 224};
    for (unsigned version = 3; version <= 4; version++)
        for (unsigned extended = 0; extended < 2; extended++)
            for (unsigned frame_unsync = 0; frame_unsync <= (version == 4); frame_unsync++) {
                size_t bytes = tag(data, text, sizeof text, false);
                data[3] = (uint8_t)version;
                data[5] = frame_unsync ? 0 : 128;
                if (version == 3) data[17]--;
                if (frame_unsync) data[19] = 2;
                if (extended) {
                    size_t size = version == 3 ? 10 : 6;
                    memmove(data + 10 + size, data + 10, bytes - 10);
                    memset(data + 10, 0, size);
                    data[13] = 6;
                    if (version == 4) data[14] = 1;
                    data[5] |= 64;
                    bytes += size;
                    data[9] = (uint8_t)(bytes - 10);
                }
                check_shared_id3(data, bytes, "A\xc3\xbf\xc3\xa0");
                uint8_t saved = data[6];
                data[6] = 128;
                check_shared_id3(data, bytes, nullptr);
                data[6] = saved;
                for (size_t n = 0; n < bytes; n++) check_shared_id3(data, n, nullptr);
            }
    const uint8_t invalid[][5] = {{3, 0xc0, 0x80}, {3, 0xed, 0xa0, 0x80}, {1, 255, 254, 0, 0xdc}, {2, 0xd8, 0, 0, 'x'}};
    const size_t lengths[] = {3, 4, 5, 5};
    for (unsigned i = 0; i < 4; i++) {
        size_t bytes = tag(data, invalid[i], lengths[i], false);
        check_shared_id3(data, bytes, nullptr);
    }
}

#if LND_MODULE_METADATA_ID3V2
static void test_full_model(void) {
    LND_METADATA *m = LND_MetadataCreate(nullptr);
    CHECK(LND_MetadataSetValue(m, "TITLE", "Tagged") == LND_OK);
    CHECK(LND_MetadataSetValue(m, "ALBUM", "Album") == LND_OK);
    LND_METADATA_CHAPTER chapter = {.id = "first", .title = "Chapter", .start_us = 1250000, .end_us = LND_METADATA_UNKNOWN,
        .start_offset_bytes = LND_METADATA_UNKNOWN, .end_offset_bytes = LND_METADATA_UNKNOWN};
    CHECK(LND_MetadataSetChapter(m, &chapter) == LND_OK);
    void *data;
    size_t bytes;
    CHECK(LND_MetadataId3v2CreateBuffer(m, 4, 0, &data, &bytes) == LND_OK);
    lnd_http_metadata queued[2] = {0};
    LND_HTTP_EVENT events[2] = {0};
    lnd_http_session session = {.metadata = queued, .events = events};
    session.options.buffer.event_count = 2;
    session.info.sample_rate_hz = 48000;
    lnd_http_id3(&session, data, bytes, 1000000);
    CHECK(session.metadata_count == 1 && !session.tags);
    lnd_store(&session.played, 48000);
    lnd_http_metadata_update(&session);
    CHECK(session.tags != nullptr && session.event_count == 1);
    const char *album = LND_MetadataGetValue(session.tags, "ALBUM", 0);
    CHECK(album && !strcmp(album, "Album"));
    const LND_METADATA_CHAPTER *actual = LND_MetadataGetChapter(session.tags, 0);
    CHECK(actual && actual->start_us == chapter.start_us && !strcmp(actual->title, chapter.title));
    lnd_http_metadata_clear(&session);
    LND_MetadataFree(session.tags);
    LND_MetadataFree(m);
    LND_MetadataBufferFree(data);
}
#endif
int main(void) {

    test_init(LND_LAYOUT_INTERLEAVED);
    test_shared_id3();
#if LND_MODULE_METADATA_ID3V2
    test_full_model();
#endif
    lnd_http_session s = {0};
    s.options.buffer.event_count = 8;
    LND_HTTP_EVENT events[8] = {0};
    lnd_http_metadata metadata[8] = {0};
    s.events = events;
    s.metadata = metadata;
    s.info.sample_rate_hz = 48000;
    uint8_t data[256];
    const uint8_t title[] = {3, 'T', 'r', 'a', 'c', 'k', ' ', 'A'};
    size_t bytes = tag(data, title, sizeof title, true);
    lnd_http_id3(&s, data, bytes, 1000000);
    CHECK(s.metadata_count == 1 && !s.event_count && !*s.info.title);
    lnd_http_metadata_update(&s);
    CHECK(!s.event_count);
    lnd_store(&s.played, 47999);
    lnd_http_metadata_update(&s);
    CHECK(!s.event_count);
    lnd_store(&s.played, 48000);
    lnd_http_metadata_update(&s);
    CHECK(s.event_count == 1 && !s.metadata_count);
    CHECK(!strcmp(s.info.title, "Track A") && events[0].position_us == 1000000 && !events[0].estimated);
    const uint8_t utf16[] = {1, 255, 254, 0x3d, 0xd8, 0, 0xde};
    bytes = tag(data, utf16, sizeof utf16, false);
    lnd_http_id3(&s, data, bytes, 1000000);
    lnd_http_metadata_update(&s);
    CHECK(s.event_count == 2 && !strcmp(events[1].text, "\xf0\x9f\x98\x80") && events[1].estimated);
    const uint8_t latin[] = {0, 'C', 'a', 'f', 0xe9};
    bytes = tag(data, latin, sizeof latin, false);
    lnd_http_id3(&s, data, bytes, 2000000);
    CHECK(s.metadata_count == 1);
    lnd_http_metadata_update(&s);
    CHECK(s.event_count == 2);
    lnd_store(&s.played, 96000);
    lnd_http_metadata_update(&s);
    CHECK(s.event_count == 3 && !strcmp(events[2].text, "Caf\xc3\xa9"));
    for (size_t n = 0; n < bytes; n++) lnd_http_id3(&s, data, n, 3000000);
    CHECK(!s.metadata_count);
    for (unsigned i = 0; i < 10; i++) lnd_http_metadata_at(&s, "Overflow", 3000000, false);
    CHECK(s.metadata_count == 8 && s.stats.events_lost == 2);
    lnd_http_metadata_clear(&s);
#if LND_MODULE_METADATA
    LND_MetadataFree(s.tags);
#endif
    LND_LibraryFree();
    CHECK(live_allocations == 0);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
