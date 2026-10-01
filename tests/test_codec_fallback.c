#include "codec_test.h"

static unsigned attempts, closes;
static int32_t failure = LND_ERR_FORMAT;
static int32_t probe(LND_IO *io) {
    char h[4];
    return LND_IoRead(io, h, 4) == 4 && !memcmp(h, "fall", 4) ? 300 : 0;
}
static int32_t reject(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    CHECK(LND_IoGetPositionBytes(io) == 0);
    CHECK(*state == nullptr && info->channels == 0);
    char h[4];
    LND_IoRead(io, h, 4);
    attempts++;
    return failure;
}
static int32_t accept(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    CHECK(LND_IoGetPositionBytes(io) == 0);
    *info = (LND_CODEC_INFO){.format = LND_FORMAT_S16, .channels = 1, .sample_rate_hz = 8000, .length_frames = 2};
    *state = io;
    return LND_OK;
}
static uint64_t decode(void *state, void *dst, uint64_t frames) { return LND_IoRead(state, dst, (size_t)frames * 2) / 2; }
static void close_decoder(void *state) { closes++; }
static const LND_CODEC first = {.name = "reject", .probe = probe, .open = reject, .read = decode, .close = close_decoder};
static const LND_CODEC second = {.name = "fallback", .flags = LND_CODEC_FLAG_SYSTEM, .probe = probe, .open = accept, .read = decode, .close = close_decoder};

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    CHECK(LND_CodecRegister(&first) == LND_OK);
    CHECK(LND_CodecRegister(&second) == LND_OK);
    CHECK(LND_CodecRegister(&first) == LND_OK);
    LND_CODEC duplicate = first;
    CHECK(LND_CodecRegister(&duplicate) == LND_ERR_INVALID_ARG);
    duplicate.name = "";
    CHECK(LND_CodecRegister(&duplicate) == LND_ERR_INVALID_ARG);
    CHECK(LND_CodecFind("reject") == &first && !LND_CodecFind("missing") && !LND_CodecFind(nullptr));
    CHECK(!strcmp(LND_CodecGetName(&first), "reject"));
    CHECK(LND_CodecGetFlags(&second) == LND_CODEC_FLAG_SYSTEM);
    CHECK(!LND_CodecGetName(nullptr) && !LND_CodecGetExtensions(nullptr) && !LND_CodecGetFlags(nullptr));
    LND_CODEC_STREAM stream = {0};
    LND_CODEC custom = first;
    custom.stream = &stream;
    custom.stream_identifiers = "custom;mp4a.40.*";
    CHECK(!strcmp(LND_CodecGetStreamIdentifiers(&custom), custom.stream_identifiers));
    CHECK(LND_CodecSupportsStream(&custom, "custom") && LND_CodecSupportsStream(&custom, "mp4a.40.2"));
    CHECK(!LND_CodecSupportsStream(&custom, "custom.extra") && !LND_CodecSupportsStream(&custom, "mp4a.4"));
    CHECK(!LND_CodecSupportsStream(&custom, "") && !LND_CodecSupportsStream(&custom, nullptr));
    CHECK(!LND_CodecSupportsStream(&first, "custom") && !LND_CodecSupportsStream(nullptr, "custom"));
    const char data[] = "fall";
    for (unsigned graph = 0; graph <= LND_MODULE_GRAPH; graph++) {
        uint32_t flags = graph ? 0 : LND_ENCODED_SOURCE_LIGHTWEIGHT;
        attempts = 0;
        LND_SOURCE *source = LND_SourceCreateEncodedMemory(data, 4, flags, nullptr);
        CHECK(source != nullptr && attempts == 1);
        if (source) {
            CHECK(LND_SourceGetCodec(source) == &second);
            int16_t pcm[2];
            CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 2) == 2);
            CHECK(LND_SourceFree(source) == LND_OK);
        }
        LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "reject"};
        attempts = 0;
        CHECK(LND_SourceCreateEncodedMemory(data, 4, flags, &options) == nullptr);
        CHECK(attempts == 1 && LND_ErrorGetLast() == LND_ERR_FORMAT);
        options.codec_name = "fallback";
        CHECK(LND_ConfigSet(LND_CFG_CODECS_SYSTEM, 0) == LND_OK);
        CHECK(LND_SourceCreateEncodedMemory(data, 4, flags, &options) == nullptr);
        CHECK(LND_ConfigSet(LND_CFG_CODECS_SYSTEM, 1) == LND_OK);
        failure = LND_ERR_OUT_OF_MEMORY;
        CHECK(LND_SourceCreateEncodedMemory(data, 4, flags, nullptr) == nullptr);
        CHECK(LND_ErrorGetLast() == LND_ERR_OUT_OF_MEMORY);
        failure = LND_ERR_FORMAT;
    }
    CHECK(closes == 1 + LND_MODULE_GRAPH);
    CHECK(LND_CodecUnregister(&first) == LND_OK);
    CHECK(LND_CodecUnregister(&second) == LND_OK);
    CHECK(!LND_CodecFind("reject") && !LND_CodecFind("fallback"));
    LND_LibraryFree();
    CHECK(live_allocations == 0);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
