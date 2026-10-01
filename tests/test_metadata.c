#include "metadata_test.h"

static void grouped_fields(void) {
    for (unsigned replace = 0; replace < 2; replace++) {
        LND_METADATA *m = LND_MetadataCreate(nullptr);
        CHECK(m != nullptr);
        for (unsigned i = 0; i < 1024; i++) {
            char value[16];
            snprintf(value, sizeof value, "%u", i);
            LND_METADATA_FIELD field = {.key = i % 3 ? "KEEP" : "DROP", .value = value};
            CHECK(LND_MetadataAddField(m, &field) == LND_OK);
        }
        const LND_METADATA_FIELD *alias = LND_MetadataGetField(m, 1023);
        if (replace) {
            fail_allocation = 0;
            CHECK(LND_MetadataSetValue(m, alias->key, alias->value) == LND_ERR_OUT_OF_MEMORY);
            fail_allocation = -1;
            CHECK(LND_MetadataGetFieldCount(m) == 1024);
            CHECK(LND_MetadataSetValue(m, alias->key, alias->value) == LND_OK);
        } else {
            fail_allocation = 0;
            CHECK(LND_MetadataRemove(m, alias->key) == LND_OK);
            fail_allocation = -1;
        }
        unsigned index = 0;
        for (unsigned i = 0; i < 1024; i++) {
            if (!(i % 3)) continue;
            char value[16];
            snprintf(value, sizeof value, "%u", i);
            const LND_METADATA_FIELD *field = LND_MetadataGetField(m, index++);
            CHECK(field && !strcmp(field->key, "KEEP") && !strcmp(field->value, value));
        }
        CHECK(LND_MetadataGetFieldCount(m) == index + replace);
        if (replace) tag_expect(m, "DROP", 0, "1023");
        CHECK(LND_MetadataRemove(m, "keep") == LND_OK && LND_MetadataGetFieldCount(m) == replace);
        CHECK(LND_MetadataRemove(m, "drop") == LND_OK && !LND_MetadataGetFieldCount(m));
        LND_MetadataFree(m);
    }
}

static void model(void) {
    LND_METADATA_LIMITS limits = {.bytes = 128, .fields = 2, .chapters = 1, .blobs = 1};
    LND_METADATA *m = LND_MetadataCreate(&limits);
    CHECK(m != nullptr);
    LND_METADATA_FIELD field = {"artist", "one"};
    CHECK(LND_MetadataAddField(m, &field) == LND_OK);
    field.value = "two";
    CHECK(LND_MetadataAddField(m, &field) == LND_OK);
    CHECK(LND_MetadataAddField(m, &field) == LND_METADATA_ERR_LIMIT);
    CHECK(LND_MetadataSetValue(m, "ARTIST", LND_MetadataGetValue(m, "ARTIST", 1)) == LND_OK);
    tag_expect(m, "artist", 0, "two");
    CHECK(LND_MetadataGetFieldCount(m) == 1);
    CHECK(LND_MetadataAddField(m, LND_MetadataGetField(m, 0)) == LND_OK);
    CHECK(LND_MetadataRemoveField(m, 0) == LND_OK);
    CHECK(LND_MetadataSetValue(m, "A=B", "value") == LND_OK);
    CHECK(LND_MetadataRemove(m, "A=B") == LND_OK);
    CHECK(LND_MetadataSetValue(m, "BAD\nKEY", "bad") == LND_ERR_INVALID_ARG);
    CHECK(LND_MetadataSetValue(m, "TITLE", "\xc0\xaf") == LND_ERR_INVALID_ARG);
    fail_allocation = 0;
    CHECK(LND_MetadataSetValue(m, "ARTIST", "replacement") == LND_ERR_OUT_OF_MEMORY);
    fail_allocation = -1;
    tag_expect(m, "ARTIST", 0, "two");
    CHECK(LND_MetadataSetValue(m, "ARTIST", "replacement") == LND_OK);
    CHECK(LND_MetadataSetVendor(m, "custom") == LND_OK);
    char text[200];
    memset(text, 'x', sizeof text);
    text[199] = 0;
    CHECK(LND_MetadataSetValue(m, "TITLE", text) == LND_METADATA_ERR_LIMIT);
    LND_METADATA_CHAPTER c = {
        .id = "id", .title = "chapter", .start_us = 1000, .end_us = 2000, .start_offset_bytes = LND_METADATA_UNKNOWN, .end_offset_bytes = LND_METADATA_UNKNOWN};
    CHECK(LND_MetadataSetChapter(m, &c) == LND_OK);
    CHECK(LND_MetadataSetChapter(m, LND_MetadataGetChapter(m, 0)) == LND_OK);
    c.id = "second";
    CHECK(LND_MetadataSetChapter(m, &c) == LND_METADATA_ERR_LIMIT);
    c.id = "id";
    c.end_us = 1;
    CHECK(LND_MetadataSetChapter(m, &c) == LND_ERR_INVALID_ARG);
    LND_METADATA *clone = LND_MetadataClone(m);
    CHECK(clone && !strcmp(LND_MetadataGetVendor(clone), "custom"));
    CHECK(LND_MetadataRemoveChapter(m, LND_MetadataGetChapter(m, 0)->id) == LND_OK);
    CHECK(LND_MetadataGetChapterCount(clone) == 1);
    int16_t samples[2] = {1, 2};
    LND_PCM pcm = {.data = samples, .frames = 2, .channels = 1, .format = LND_FORMAT_S16};
    LND_SOURCE_CONFIG source_config = {.pcm = &pcm, .sample_rate_hz = 48000, .channels = 1};
    LND_SOURCE *source = LND_SourceCreate(&source_config);
    CHECK(source != nullptr);
    if (source) {
        CHECK(LND_SourceSetMetadata(source, m) == LND_OK);
        CHECK(LND_SourceGetMetadataStatus(source) == LND_OK);
        CHECK(LND_SourceCopyMetadata(source, clone) == LND_OK);
        tag_expect(clone, "ARTIST", 0, "replacement");
        fail_allocation = 0;
        CHECK(LND_SourceSetMetadata(source, m) == LND_ERR_OUT_OF_MEMORY);
        fail_allocation = -1;
        CHECK(LND_SourceCopyMetadata(source, clone) == LND_OK);
        int16_t output[2];
        unsigned before = allocations;
        CHECK(LND_SourceRead(source, output, LND_FORMAT_S16, 2) == 2);
        CHECK(allocations == before && !memcmp(samples, output, sizeof samples));
        LND_METADATA_LIMITS tiny_limits = {.bytes = 1};
        LND_METADATA *tiny = LND_MetadataCreate(&tiny_limits);
        CHECK(LND_SourceCopyMetadata(source, tiny) == LND_METADATA_ERR_LIMIT);
        LND_MetadataFree(tiny);
        CHECK(LND_SourceFree(source) == LND_OK);
        CHECK(LND_SourceGetMetadataStatus(source) == LND_ERR_INVALID_ARG);
    }
    LND_MetadataClear(m);
    CHECK(!LND_MetadataGetFieldCount(m) && !LND_MetadataGetChapterCount(m));
    CHECK(LND_MetadataGetValue(m, "ARTIST", 0) == nullptr);
    LND_MetadataFree(clone);
    LND_MetadataFree(m);
}

#if LND_MODULE_METADATA_ID3V1
static void id3v1(void) {
    LND_METADATA *m = LND_MetadataCreate(nullptr);
    uint8_t tag[128] = {'T', 'A', 'G'};
    memcpy(tag + 3, "Caf\xe9 ", 5);
    memcpy(tag + 33, "Artist", 6);
    memcpy(tag + 63, "Album", 5);
    memcpy(tag + 93, "2001", 4);
    memcpy(tag + 97, "Comment", 7);
    tag[126] = 17;
    tag[127] = 17;
    CHECK(LND_MetadataId3v1Read(m, tag, 128) == LND_OK);
    tag_expect(m, "TITLE", 0, "Café");
    tag_expect(m, "GENRE", 0, "Rock");
    tag_expect(m, "TRACKNUMBER", 0, "17");
    CHECK(LND_MetadataGetVersion(m) == 11);
    void *out = nullptr;
    size_t size;
    CHECK(LND_MetadataId3v1CreateBuffer(m, 0, &out, &size) == LND_OK);
    CHECK(size == 128 && ((uint8_t *)out)[126] == 17);
    LND_MetadataBufferFree(out);
    CHECK(LND_MetadataSetValue(m, "TITLE", "Żółw") == LND_OK);
    CHECK(LND_MetadataId3v1CreateBuffer(m, 0, &out, &size) == LND_ERR_UNSUPPORTED && !out);
    CHECK(LND_MetadataId3v1CreateBuffer(m, LND_METADATA_DROP_UNSUPPORTED, &out, &size) == LND_OK);
    LND_MetadataBufferFree(out);
    for (size_t n = 0; n < 128; ++n) CHECK(LND_MetadataId3v1Read(m, tag, n) == LND_ERR_FORMAT);
    tag[125] = 'x';
    CHECK(LND_MetadataId3v1Read(m, tag, 128) == LND_OK);
    CHECK(LND_MetadataGetVersion(m) == 10 && !LND_MetadataGetValue(m, "TRACKNUMBER", 0));
    LND_MetadataFree(m);
}
#endif

#if LND_MODULE_METADATA_ID3V2
static void id3v2(void) {
    LND_METADATA *m = LND_MetadataCreate(nullptr), *copy = LND_MetadataCreate(nullptr);
    tag_bytes frames = {0};
    const uint8_t utf16[] = {1, 0xff, 0xfe, 'A', 0, 0x3c, 0xd8, 0xb5, 0xdf};
    tag_frame(&frames, "TT2", 2, utf16, sizeof utf16);
    tag_bytes tag = tag_id3(&frames, 2, 0);
    CHECK(LND_MetadataId3v2Read(m, tag.data, tag.size, nullptr) == LND_OK);
    tag_expect(m, "TITLE", 0, "A🎵");
    free(tag.data);
    free(frames.data);
    frames = (tag_bytes){0};
    const uint8_t title[] = {3, 'O', 'n', 'e', 0, 'T', 'w', 'o'};
    const uint8_t custom[] = {3, 'M', 'Y', '_', 'K', 'E', 'Y', 0, 'v', 'a', 'l'};
    const uint8_t comment[] = {2, 'p', 'o', 'l', 0, 'd', 0, 0, 0, 'T', 0x01, 0x42};
    const uint8_t art[] = {0, 'i', 'm', 'a', 'g', 'e', '/', 'p', 'n', 'g', 0, 3, 0, 0x89, 'P', 'N', 'G'};
    tag_frame(&frames, "TIT2", 4, title, sizeof title);
    tag_frame(&frames, "TXXX", 4, custom, sizeof custom);
    tag_frame(&frames, "COMM", 4, comment, sizeof comment);
    tag_frame(&frames, "APIC", 4, art, sizeof art);
    tag = tag_id3(&frames, 4, 0);
    size_t consumed;
    CHECK(LND_MetadataId3v2Read(m, tag.data, tag.size, &consumed) == LND_OK && consumed == tag.size);
    tag_expect(m, "TITLE", 0, "One");
    tag_expect(m, "TITLE", 1, "Two");
    tag_expect(m, "MY_KEY", 0, "val");
    tag_expect(m, "COMMENT", 0, "Tł");
    const LND_METADATA_FIELD *field = LND_MetadataGetField(m, 3);
    CHECK(field && !strcmp(field->language, "pol") && !strcmp(field->description, "d"));
    CHECK(LND_MetadataGetBlobCount(m) == 1);
    void *out = nullptr;
    size_t size;
    CHECK(LND_MetadataId3v2CreateBuffer(m, 4, 0, &out, &size) == LND_OK);
    CHECK(LND_MetadataId3v2Read(copy, out, size, nullptr) == LND_OK);
    tag_expect(copy, "TITLE", 1, "Two");
    const LND_METADATA_BLOB *blob = LND_MetadataGetBlob(copy, 0);
    CHECK(blob && blob->size == sizeof art && !memcmp(blob->data, art, sizeof art));
    LND_MetadataBufferFree(out);
    CHECK(LND_MetadataId3v2CreateBuffer(m, 3, 0, &out, &size) == LND_ERR_UNSUPPORTED);
    for (size_t n = 0; n < tag.size; ++n) CHECK(LND_MetadataId3v2Read(m, tag.data, n, nullptr) == LND_ERR_FORMAT);
    tag_expect(m, "TITLE", 1, "Two");
    bool succeeded = false;
    for (int fail = 0; fail < 100 && !succeeded; ++fail) {
        unsigned live = live_allocations;
        fail_allocation = fail;
        int32_t r = LND_MetadataId3v2Read(copy, tag.data, tag.size, nullptr);
        fail_allocation = -1;
        CHECK(r == LND_OK || r == LND_ERR_OUT_OF_MEMORY);
        if (r)
            CHECK(live_allocations == live);
        else
            succeeded = true;
    }
    CHECK(succeeded);
    free(tag.data);
    free(frames.data);
    frames = (tag_bytes){0};
    const uint8_t latin[] = {0, 0xff, 0xe0, 'X'};
    tag_frame(&frames, "TIT2", 3, latin, sizeof latin);
    tag_bytes escaped = {0};
    for (size_t i = 0; i < frames.size; ++i) {
        tag_byte(&escaped, frames.data[i]);
        if (frames.data[i] == 255) tag_byte(&escaped, 0);
    }
    tag = tag_id3(&escaped, 3, 0x80);
    CHECK(LND_MetadataId3v2Read(m, tag.data, tag.size, nullptr) == LND_OK);
    tag_expect(m, "TITLE", 0, "ÿàX");
    free(tag.data);
    free(escaped.data);
    free(frames.data);
    frames = (tag_bytes){0};
    const uint8_t toc[] = {'a', 0, 3, 1, 'a', 0};
    tag_frame(&frames, "CTOC", 4, toc, sizeof toc);
    tag = tag_id3(&frames, 4, 0);
    CHECK(LND_MetadataId3v2Read(m, tag.data, tag.size, nullptr) == LND_ERR_CYCLE);
    tag_expect(m, "TITLE", 0, "ÿàX");
    free(tag.data);
    free(frames.data);
    LND_MetadataClear(m);
    LND_METADATA_CHAPTER chapter = {.id = "café",
                                    .title = "Początek 🎵",
                                    .url = "https://example.com",
                                    .start_us = 1234000,
                                    .end_us = 4567000,
                                    .start_offset_bytes = LND_METADATA_UNKNOWN,
                                    .end_offset_bytes = LND_METADATA_UNKNOWN};
    CHECK(LND_MetadataSetChapter(m, &chapter) == LND_OK);
    for (unsigned version = 3; version <= 4; ++version) {
        CHECK(LND_MetadataId3v2CreateBuffer(m, version, 0, &out, &size) == LND_OK);
        CHECK(LND_MetadataId3v2Read(copy, out, size, nullptr) == LND_OK);
        const LND_METADATA_CHAPTER *c = LND_MetadataGetChapter(copy, 0);
        CHECK(c && c->start_us == chapter.start_us && c->end_us == chapter.end_us && !strcmp(c->title, chapter.title) && !strcmp(c->url, chapter.url));
        CHECK(LND_MetadataGetBlobCount(copy) == 1);
        CHECK(LND_MetadataRemoveChapter(copy, chapter.id) == LND_OK);
        CHECK(!LND_MetadataGetBlobCount(copy));
        LND_MetadataBufferFree(out);
    }
    LND_MetadataFree(copy);
    LND_MetadataFree(m);
}
#endif

#if LND_MODULE_METADATA_COMMENTS
static void opus(void) {
    LND_METADATA *m = LND_MetadataCreate(nullptr), *copy = LND_MetadataCreate(nullptr);
    tag_bytes packet = {0};
    tag_append(&packet, "OpusTags", 8);
    tag_u32(&packet, 4, false);
    tag_append(&packet, "Test", 4);
    const char *values[] = {"title=Title", "ARTIST=one", "artist=two", "CHAPTER003=01:02:03.123456", "CHAPTER003NAME=Name", "CHAPTER001=00:00:00.001"};
    tag_u32(&packet, sizeof values / sizeof *values, false);
    for (unsigned i = 0; i < sizeof values / sizeof *values; ++i) {
        tag_u32(&packet, (uint32_t)strlen(values[i]), false);
        tag_append(&packet, values[i], strlen(values[i]));
    }
    const uint8_t trailing[] = {1, 7, 0, 9};
    tag_append(&packet, trailing, sizeof trailing);
    CHECK(LND_MetadataOpusRead(m, packet.data, packet.size) == LND_OK);
    tag_expect(m, "ARTIST", 1, "two");
    CHECK(LND_MetadataGetChapterCount(m) == 2 && LND_MetadataGetFieldCount(m) == 3);
    const LND_METADATA_CHAPTER *c = LND_MetadataGetChapter(m, 1);
    CHECK(c && c->start_us == UINT64_C(3723123456) && !strcmp(c->title, "Name"));
    CHECK(LND_MetadataGetBlobCount(m) == 1);
    CHECK(LND_MetadataFindChapter(m, 999) == LND_METADATA_ERR_NOT_FOUND);
    CHECK(LND_MetadataFindChapter(m, 1000) == 0);
    CHECK(LND_MetadataFindChapter(m, UINT64_MAX) == 1);
    void *out = nullptr;
    size_t size;
    CHECK(LND_MetadataOpusCreateBuffer(m, 0, &out, &size) == LND_OK);
    CHECK(size >= 4 && !memcmp((uint8_t *)out + size - 4, trailing, 4));
    CHECK(LND_MetadataOpusRead(copy, out, size) == LND_OK);
    CHECK(LND_MetadataGetChapterCount(copy) == 2);
    tag_expect(copy, "ARTIST", 1, "two");
    LND_MetadataBufferFree(out);
    for (size_t n = 0; n < packet.size - sizeof trailing; ++n) CHECK(LND_MetadataOpusRead(copy, packet.data, n) < 0);
    unsigned live = live_allocations;
    for (int fail = 0; fail < 80; ++fail) {
        fail_allocation = fail;
        int32_t r = LND_MetadataOpusRead(copy, packet.data, packet.size);
        fail_allocation = -1;
        CHECK(r == LND_OK || r == LND_ERR_OUT_OF_MEMORY);
        CHECK(live_allocations == live);
        if (!r) break;
    }
    LND_METADATA_LIMITS limits = {.bytes = 64};
    LND_METADATA *small = LND_MetadataCreate(&limits);
    CHECK(LND_MetadataOpusRead(small, packet.data, packet.size) == LND_METADATA_ERR_LIMIT);
    packet.data[12] = 0xc0;
    CHECK(LND_MetadataOpusRead(copy, packet.data, packet.size) == LND_ERR_FORMAT);
    free(packet.data);
    LND_MetadataFree(small);
    LND_MetadataFree(copy);
    LND_MetadataFree(m);
}
#endif

#if LND_MODULE_METADATA_ID3V2
static void extended_id3(void) {
    LND_METADATA *m = LND_MetadataCreate(nullptr);
    for (unsigned version = 3; version <= 4; ++version) {
        tag_bytes frames = {0};
        if (version == 3) {
            tag_u32(&frames, 6, true);
            tag_byte(&frames, 0);
            tag_byte(&frames, 0);
            tag_u32(&frames, 0, true);
        } else {
            const uint8_t extended[] = {0, 0, 0, 6, 1, 0};
            tag_append(&frames, extended, sizeof extended);
        }
        const uint8_t title[] = {0, 'T', 'i', 't', 'l', 'e'};
        tag_frame(&frames, "TIT2", version, title, sizeof title);
        tag_bytes tag = tag_id3(&frames, version, version == 4 ? 0x50 : 0x40);
        if (version == 4) {
            tag_append(&tag, "3DI", 3);
            tag_append(&tag, tag.data + 3, 7);
        }
        size_t consumed = 0;
        CHECK(LND_MetadataId3v2Read(m, tag.data, tag.size, &consumed) == LND_OK);
        CHECK(consumed == tag.size);
        tag_expect(m, "TITLE", 0, "Title");
        if (version == 4) {
            tag.data[tag.size - 1] ^= 1;
            CHECK(LND_MetadataId3v2Read(m, tag.data, tag.size, nullptr) == LND_ERR_FORMAT);
        }
        tag.data[10] = 127;
        CHECK(LND_MetadataId3v2Read(m, tag.data, tag.size, nullptr) == LND_ERR_FORMAT);
        free(tag.data);
        free(frames.data);
    }
    tag_bytes frames = {0};
    const uint8_t body[] = {0, 0, 0, 4, 0, 255, 0, 0xe0, 'X'};
    tag_frame(&frames, "TIT2", 4, body, sizeof body);
    frames.data[9] = 3;
    tag_bytes tag = tag_id3(&frames, 4, 0);
    CHECK(LND_MetadataId3v2Read(m, tag.data, tag.size, nullptr) == LND_OK);
    tag_expect(m, "TITLE", 0, "ÿàX");
    tag.data[12] = 0;
    CHECK(LND_MetadataId3v2Read(m, tag.data, tag.size, nullptr) == LND_ERR_FORMAT);
    free(tag.data);
    free(frames.data);
    LND_MetadataFree(m);
}
#endif

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    model();
    grouped_fields();
#if LND_MODULE_METADATA_ID3V1
    id3v1();
#endif
#if LND_MODULE_METADATA_ID3V2
    id3v2();
    extended_id3();
#endif
#if LND_MODULE_METADATA_COMMENTS
    opus();
#endif
    LND_LibraryFree();
    CHECK(!live_allocations);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
