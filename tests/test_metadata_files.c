#include "metadata_test.h"

static void compare_pcm(const tag_bytes *a, const tag_bytes *b) {
    const uint8_t *pcm_a = nullptr, *pcm_b = nullptr;
    size_t size_a = 0, size_b = 0;
    for (size_t p = 12; p + 8 <= a->size;) {
        uint32_t n = tag_get32(a->data + p + 4);
        if (!memcmp(a->data + p, "data", 4)) {
            pcm_a = a->data + p + 8;
            size_a = n;
            break;
        }
        p += 8 + n + (n & 1u);
    }
    for (size_t p = 12; p + 8 <= b->size;) {
        uint32_t n = tag_get32(b->data + p + 4);
        if (!memcmp(b->data + p, "data", 4)) {
            pcm_b = b->data + p + 8;
            size_b = n;
            break;
        }
        p += 8 + n + (n & 1u);
    }
    CHECK(pcm_a && pcm_b && size_a == size_b && !memcmp(pcm_a, pcm_b, size_a));
}

#if LND_MODULE_METADATA_WAVE
static void wave(void) {
    LND_METADATA *m = LND_MetadataCreate(nullptr), *copy = LND_MetadataCreate(nullptr);
    CHECK(LND_MetadataSetValue(m, "TITLE", "Muzyka 🎵") == LND_OK);
    LND_METADATA_CHAPTER c = {
        .id = "1", .title = "Rozdział", .start_us = 125000, .end_us = 250000, .start_offset_bytes = LND_METADATA_UNKNOWN, .end_offset_bytes = LND_METADATA_UNKNOWN};
    CHECK(LND_MetadataSetChapter(m, &c) == LND_OK);
    const uint8_t bext[] = {1, 2, 3, 4, 0, 9};
    LND_METADATA_BLOB blob = {.format = LND_METADATA_WAVE, .key = "bext", .data = bext, .size = sizeof bext};
    CHECK(LND_MetadataAddBlob(m, &blob) == LND_OK);
    void *chunks = nullptr;
    size_t size = 0;
    CHECK(LND_MetadataWaveCreateBuffer(m, 48000, 0, &chunks, &size) == LND_OK);
    tag_bytes original = tag_wave(chunks, size);
    LND_MetadataBufferFree(chunks);
    CHECK(LND_MetadataWaveRead(copy, original.data, original.size) == LND_OK);
    tag_expect(copy, "TITLE", 0, "Muzyka 🎵");
    const LND_METADATA_CHAPTER *chapter = LND_MetadataGetChapter(copy, 0);
    CHECK(chapter && chapter->start_us == 125000 && chapter->end_us == 250000 && !strcmp(chapter->title, c.title));
    CHECK(LND_MetadataGetBlobCount(copy) == 2);
    for (size_t n = 0; n < original.size; ++n) CHECK(LND_MetadataWaveRead(copy, original.data, n) < 0);
    CHECK(LND_MetadataSetValue(copy, "TITLE", "Changed") == LND_OK);
#if LND_MODULE_METADATA_IO
    LND_IO *input = LND_IoOpenMemory(original.data, original.size);
    CHECK(LND_IoSeekBytes(input, 7) == LND_OK);
    tag_bytes output = {0};
    LND_IO *io = LND_IoCreateOutput(&tag_output_procs, &output);
    CHECK(LND_MetadataWriteIo(copy, input, io, nullptr) == LND_OK);
    CHECK(LND_IoGetPositionBytes(input) == 7 && output.size >= 12 && tag_get32(output.data + 4) == output.size - 8);
    LND_IoFree(io);
    LND_IoFree(input);
    compare_pcm(&original, &output);
    CHECK(LND_MetadataReadMemory(m, output.data, output.size, LND_METADATA_AUTO) == LND_OK);
    tag_expect(m, "TITLE", 0, "Changed");
    free(output.data);
#endif
#if LND_MODULE_METADATA_ID3V2
    CHECK(LND_MetadataSetValue(copy, "CUSTOM", "value") == LND_OK);
    c.url = "https://example.com/chapter";
    CHECK(LND_MetadataSetChapter(copy, &c) == LND_OK);
    CHECK(LND_MetadataWaveCreateBuffer(copy, 48000, 0, &chunks, &size) == LND_OK);
    tag_bytes embedded = tag_wave(chunks, size);
    LND_MetadataBufferFree(chunks);
    CHECK(LND_MetadataWaveRead(m, embedded.data, embedded.size) == LND_OK);
    tag_expect(m, "CUSTOM", 0, "value");
    chapter = LND_MetadataGetChapter(m, 0);
    CHECK(chapter && !strcmp(chapter->url, c.url));
    CHECK(LND_MetadataSetValue(m, "CUSTOM", "edited") == LND_OK);
    CHECK(LND_MetadataWaveCreateBuffer(m, 48000, 0, &chunks, &size) == LND_OK);
    tag_bytes edited = tag_wave(chunks, size);
    LND_MetadataBufferFree(chunks);
    CHECK(LND_MetadataWaveRead(copy, edited.data, edited.size) == LND_OK);
    tag_expect(copy, "CUSTOM", 0, "edited");
    free(embedded.data);
    free(edited.data);
#endif
    free(original.data);
    LND_MetadataFree(m);
    LND_MetadataFree(copy);
}
#endif

#if LND_MODULE_METADATA_IO && LND_MODULE_METADATA_COMMENTS
static size_t ogg_page_size(const uint8_t *p, size_t bytes) {
    if (bytes < 27 || memcmp(p, "OggS", 4) || bytes < 27u + p[26]) return 0;
    size_t n = 27 + p[26];
    for (unsigned i = 0; i < p[26]; ++i) n += p[27 + i];
    return n <= bytes ? n : 0;
}

static void compare_ogg(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    size_t ap = 0, bp = 0;
    unsigned pages_a = 0, pages_b = 0;
    for (; ap < an;) {
        size_t n = ogg_page_size(a + ap, an - ap);
        CHECK(n != 0);
        if (!n) return;
        if (pages_a++ && (a[ap + 6] || a[ap + 7] || a[ap + 8] || a[ap + 9]) && a[ap + 6] != 255) break;
        ap += n;
    }
    for (; bp < bn;) {
        size_t n = ogg_page_size(b + bp, bn - bp);
        CHECK(n != 0);
        if (!n) return;
        if (pages_b++ && (b[bp + 6] || b[bp + 7] || b[bp + 8] || b[bp + 9]) && b[bp + 6] != 255) break;
        bp += n;
    }
    while (ap < an && bp < bn) {
        size_t na = ogg_page_size(a + ap, an - ap), nb = ogg_page_size(b + bp, bn - bp);
        CHECK(na && nb && na == nb);
        if (!na || !nb || na != nb) return;
        CHECK(!memcmp(a + ap, b + bp, 18));
        CHECK(!memcmp(a + ap + 26, b + bp + 26, na - 26));
        ap += na;
        bp += nb;
    }
    CHECK(ap == an && bp == bn);
}

static void opus_file(void) {
    size_t bytes;
    uint8_t *source = read_file("audiosamples/tone.opus", &bytes);
    if (!source) return;
    LND_METADATA *m = LND_MetadataCreate(nullptr), *copy = LND_MetadataCreate(nullptr);
    CHECK(LND_MetadataReadMemory(m, source, bytes, LND_METADATA_AUTO) == LND_OK);
    char *large = malloc(140001);
    memset(large, 'Q', 140000);
    large[140000] = 0;
    CHECK(LND_MetadataSetValue(m, "TITLE", large) == LND_OK);
    LND_METADATA_CHAPTER c = {.id = "intro",
                              .title = "Chapter",
                              .start_us = 1000000,
                              .end_us = LND_METADATA_UNKNOWN,
                              .start_offset_bytes = LND_METADATA_UNKNOWN,
                              .end_offset_bytes = LND_METADATA_UNKNOWN};
    CHECK(LND_MetadataSetChapter(m, &c) == LND_OK);
    LND_IO *input = LND_IoOpenMemory(source, bytes);
    CHECK(LND_IoSeekBytes(input, 19) == LND_OK);
    tag_bytes output = {0};
    LND_IO *io = LND_IoCreateOutput(&tag_output_procs, &output);
    CHECK(LND_MetadataWriteIo(m, input, io, nullptr) == LND_OK);
    CHECK(LND_IoGetPositionBytes(input) == 19);
    LND_IoFree(io);
    LND_IoFree(input);
    compare_ogg(source, bytes, output.data, output.size);
#if LND_MODULE_OPUS_DECODER
    LND_SOURCE *original_audio = LND_SourceCreateEncodedMemory(source, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
    LND_SOURCE *edited_audio = LND_SourceCreateEncodedMemory(output.data, output.size, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
    CHECK(original_audio && edited_audio);
    if (original_audio && edited_audio) {
        CHECK(LND_SourceGetLengthFrames(original_audio) == LND_SourceGetLengthFrames(edited_audio));
        uint32_t channels = LND_SourceGetChannels(original_audio);
        CHECK(channels == LND_SourceGetChannels(edited_audio) && channels <= 8);
        int16_t original_pcm[2048], edited_pcm[2048];
        if (channels && channels <= 8)
            for (;;) {
                uint64_t a = LND_SourceRead(original_audio, original_pcm, LND_FORMAT_S16, 2048 / channels);
                uint64_t b = LND_SourceRead(edited_audio, edited_pcm, LND_FORMAT_S16, 2048 / channels);
                CHECK(a == b);
                if (a != b || !a) break;
                CHECK(!memcmp(original_pcm, edited_pcm, (size_t)a * channels * sizeof(int16_t)));
            }
    }
    if (original_audio) CHECK(LND_SourceFree(original_audio) == LND_OK);
    if (edited_audio) CHECK(LND_SourceFree(edited_audio) == LND_OK);
#endif
    CHECK(LND_MetadataReadMemory(copy, output.data, output.size, LND_METADATA_AUTO) == LND_OK);
    tag_expect(copy, "TITLE", 0, large);
    CHECK(LND_MetadataGetChapterCount(copy) == 1);
    CHECK(LND_MetadataSetValue(copy, "TITLE", "short") == LND_OK);
    input = LND_IoOpenMemory(output.data, output.size);
    tag_bytes smaller = {0};
    io = LND_IoCreateOutput(&tag_output_procs, &smaller);
    CHECK(LND_MetadataWriteIo(copy, input, io, nullptr) == LND_OK);
    LND_IoFree(input);
    LND_IoFree(io);
    compare_ogg(source, bytes, smaller.data, smaller.size);
    CHECK(LND_MetadataReadMemory(m, smaller.data, smaller.size, LND_METADATA_AUTO) == LND_OK);
    tag_expect(m, "TITLE", 0, "short");
    source[22] ^= 1;
    CHECK(LND_MetadataReadMemory(m, source, bytes, LND_METADATA_AUTO) == LND_ERR_FORMAT);
    tag_expect(m, "TITLE", 0, "short");
    free(smaller.data);
    free(output.data);
    free(source);
    free(large);
    LND_MetadataFree(m);
    LND_MetadataFree(copy);
}
#endif

#if LND_MODULE_METADATA_IO && LND_MODULE_METADATA_ID3V2
static void mp3_file(void) {
    size_t bytes;
    uint8_t *source = read_file("audiosamples/tone.mp3", &bytes);
    if (!source) return;
    LND_METADATA *m = LND_MetadataCreate(nullptr), *copy = LND_MetadataCreate(nullptr);
    CHECK(LND_MetadataSetValue(m, "TITLE", "Changed title") == LND_OK);
    LND_IO *input = LND_IoOpenMemory(source, bytes);
    tag_bytes output = {0};
    LND_IO *io = LND_IoCreateOutput(&tag_output_procs, &output);
    LND_METADATA_WRITE_OPTIONS options = {.format = LND_METADATA_ID3V2};
    CHECK(LND_MetadataWriteIo(m, input, io, &options) == LND_OK);
    LND_IoFree(input);
    LND_IoFree(io);
    CHECK(LND_MetadataReadMemory(copy, output.data, output.size, LND_METADATA_AUTO) == LND_OK);
    tag_expect(copy, "TITLE", 0, "Changed title");
    size_t input_start = 0, input_end = bytes, output_start = 0;
    if (bytes > 10 && !memcmp(source, "ID3", 3)) {
        input_start = 10 + ((size_t)source[6] << 21) + ((size_t)source[7] << 14) + ((size_t)source[8] << 7) + source[9];
        if (source[3] == 4 && (source[5] & 16)) input_start += 10;
    }
    if (bytes >= 128 && !memcmp(source + bytes - 128, "TAG", 3)) input_end -= 128;
    output_start = 10 + ((size_t)output.data[6] << 21) + ((size_t)output.data[7] << 14) + ((size_t)output.data[8] << 7) + output.data[9];
    CHECK(output.size - output_start == input_end - input_start);
    CHECK(!memcmp(source + input_start, output.data + output_start, input_end - input_start));
    free(output.data);
    free(source);
    LND_MetadataFree(m);
    LND_MetadataFree(copy);
}
#endif

#if LND_MODULE_OUTPUT && LND_MODULE_METADATA_IO
static void encoder(const char *name) {
    LND_METADATA *m = LND_MetadataCreate(nullptr), *copy = LND_MetadataCreate(nullptr);
    CHECK(LND_MetadataSetValue(m, "TITLE", "Recording") == LND_OK);
    LND_METADATA_CHAPTER c = {
        .id = "1", .title = "Chapter", .start_us = 0, .end_us = LND_METADATA_UNKNOWN, .start_offset_bytes = LND_METADATA_UNKNOWN, .end_offset_bytes = LND_METADATA_UNKNOWN};
    CHECK(LND_MetadataSetChapter(m, &c) == LND_OK);
    LND_ENCODER_PARAMS params = {.encoder_name = name, .sample_rate_hz = 48000, .channels = 1, .format = LND_FORMAT_S16, .metadata = m};
    tag_bytes bytes = {0};
    LND_OUTPUT *out = LND_OutputCreateProc(&tag_output_procs, &bytes, &params);
    CHECK(out != nullptr);
    if (!out) {
        LND_MetadataFree(m);
        LND_MetadataFree(copy);
        return;
    }
    CHECK(LND_MetadataSetValue(m, "TITLE", "Later edit") == LND_OK);
    CHECK(LND_OutputCopyMetadata(out, copy) == LND_OK);
    tag_expect(copy, "TITLE", 0, "Recording");
    int16_t pcm[4800] = {0};
    CHECK(LND_OutputWrite(out, pcm, LND_FORMAT_S16, 4800) == LND_OK);
    CHECK(LND_OutputFree(out) == LND_OK);
    CHECK(LND_MetadataReadMemory(m, bytes.data, bytes.size, LND_METADATA_AUTO) == LND_OK);
    tag_expect(m, "TITLE", 0, "Recording");
    CHECK(LND_MetadataGetChapterCount(m) == 1);
#if LND_MODULE_CODECS
    LND_ENCODED_SOURCE_OPTIONS options = {.metadata_flags = LND_ENCODED_SOURCE_REQUIRE_METADATA};
    for (unsigned mode = 0; mode < (LND_MODULE_GRAPH ? 2u : 1u); ++mode) {
        LND_SOURCE *source = LND_SourceCreateEncodedMemory(bytes.data, bytes.size, mode ? LND_ENCODED_SOURCE_DIRECT : LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
        CHECK(source != nullptr);
        if (source) {
            CHECK(LND_SourceGetMetadataStatus(source) == LND_OK);
            CHECK(LND_SourceCopyMetadata(source, copy) == LND_OK);
            tag_expect(copy, "TITLE", 0, "Recording");
            CHECK(LND_SourceSetMetadata(source, nullptr) == LND_OK);
            CHECK(LND_SourceGetMetadataStatus(source) == LND_METADATA_ERR_NOT_FOUND);
            CHECK(LND_SourceSetMetadata(source, m) == LND_OK);
            CHECK(LND_MetadataSetValue(m, "TITLE", "Independent") == LND_OK);
            CHECK(LND_SourceCopyMetadata(source, copy) == LND_OK);
            tag_expect(copy, "TITLE", 0, "Recording");
            CHECK(LND_MetadataSetValue(m, "TITLE", "Recording") == LND_OK);
            CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 4800) > 0);
            CHECK(LND_SourceFree(source) == LND_OK);
        }
    }
#endif
    free(bytes.data);
    LND_MetadataFree(m);
    LND_MetadataFree(copy);
}
#endif

#if LND_MODULE_METADATA_IO && LND_MODULE_METADATA_WAVE
static void rf64(void) {
    LND_METADATA *m = LND_MetadataCreate(nullptr);
    CHECK(LND_MetadataSetValue(m, "TITLE", "RF64") == LND_OK);
    void *chunks = nullptr;
    size_t n = 0;
    CHECK(LND_MetadataWaveCreateBuffer(m, 48000, 0, &chunks, &n) == LND_OK);
    tag_bytes small = tag_wave(chunks, n), large = {0};
    LND_MetadataBufferFree(chunks);
    tag_append(&large, "RF64", 4);
    tag_u32(&large, UINT32_MAX, false);
    tag_append(&large, "WAVEds64", 8);
    tag_u32(&large, 28, false);
    tag_u32(&large, (uint32_t)(small.size + 28), false);
    tag_u32(&large, 0, false);
    tag_u32(&large, 256, false);
    tag_u32(&large, 0, false);
    tag_u32(&large, 128, false);
    tag_u32(&large, 0, false);
    tag_u32(&large, 0, false);
    tag_append(&large, small.data + 12, small.size - 12);
    for (size_t pos = 48; pos + 8 <= large.size;) {
        uint32_t bytes = tag_get32(large.data + pos + 4);
        if (!memcmp(large.data + pos, "data", 4)) {
            memset(large.data + pos + 4, 255, 4);
            break;
        }
        pos += 8 + bytes + (bytes & 1u);
    }
    CHECK(LND_MetadataWaveRead(m, large.data, large.size) == LND_OK);
    CHECK(LND_MetadataGetVersion(m) == 64);
    tag_expect(m, "TITLE", 0, "RF64");
    CHECK(LND_MetadataSetValue(m, "TITLE", "RF64 changed title") == LND_OK);
    LND_IO *input = LND_IoOpenMemory(large.data, large.size);
    CHECK(LND_IoSeekBytes(input, 10) == LND_OK);
    tag_bytes result = {0};
    LND_IO *output = LND_IoCreateOutput(&tag_output_procs, &result);
    CHECK(LND_MetadataWriteIo(m, input, output, nullptr) == LND_OK);
    CHECK(LND_IoGetPositionBytes(input) == 10);
    CHECK(!memcmp(result.data, "RF64", 4) && tag_get32(result.data + 20) == result.size - 8 && tag_get32(result.data + 28) == 256);
    CHECK(!memcmp(result.data + result.size - 256, large.data + large.size - 256, 256));
    CHECK(LND_MetadataReadMemory(m, result.data, result.size, LND_METADATA_AUTO) == LND_OK);
    tag_expect(m, "TITLE", 0, "RF64 changed title");
    LND_IoFree(output);
    LND_IoFree(input);
    free(result.data);
    free(large.data);
    free(small.data);
    LND_MetadataFree(m);
}
#endif

#if LND_MODULE_METADATA_IO
static int64_t never_read(void *user, void *data, size_t size) {
    (void)data;
    (void)size;
    ++*(unsigned *)user;
    return 0;
}
static size_t short_write(void *user, const void *data, size_t size) { return tag_write(user, data, size > 3 ? 3 : size); }
static void io_contract(void) {
    LND_METADATA *m = LND_MetadataCreate(nullptr);
    CHECK(LND_MetadataSetValue(m, "TITLE", "previous") == LND_OK);
    unsigned reads = 0;
    LND_IO_STREAM_INPUT_PROCS stream = {.read = never_read};
    LND_IO *input = LND_IoCreateStream(&stream, &reads, 100);
    CHECK(input != nullptr);
    CHECK(LND_MetadataReadIo(m, input, LND_METADATA_AUTO) == LND_ERR_UNSUPPORTED);
    CHECK(!reads);
    tag_expect(m, "TITLE", 0, "previous");
    LND_IoFree(input);
#if LND_MODULE_METADATA_ID3V2
    void *tag = nullptr;
    size_t size = 0;
    CHECK(LND_MetadataId3v2CreateBuffer(m, 4, 0, &tag, &size) == LND_OK);
    test_input reader = {.data = tag, .size = size, .limit = 3};
    input = LND_IoCreateInput(&input_procs, &reader, size);
    CHECK(LND_IoSeekBytes(input, 3) == LND_OK);
    CHECK(LND_MetadataReadIo(m, input, LND_METADATA_AUTO) == LND_OK);
    CHECK(LND_IoGetPositionBytes(input) == 3 && !reader.closes);
    tag_bytes bytes = {0};
    LND_IO_OUTPUT_PROCS procs = {.write = short_write};
    LND_IO *output = LND_IoCreateOutput(&procs, &bytes);
    CHECK(LND_MetadataWriteIo(m, input, input, nullptr) == LND_ERR_INVALID_ARG);
    CHECK(LND_MetadataWriteIo(m, input, output, nullptr) == LND_OK);
    CHECK(bytes.size == size && !memcmp(bytes.data, tag, size));
    CHECK(LND_IoGetPositionBytes(input) == 3 && !reader.closes);
    CHECK(LND_MetadataWriteIo(m, input, output, nullptr) == LND_ERR_INVALID_ARG);
    LND_IoFree(output);
    LND_IoFree(input);
    CHECK(reader.closes == 1);
    LND_MetadataBufferFree(tag);
    free(bytes.data);
#endif
    LND_MetadataFree(m);
}
#endif

int main(void) {
#if LND_MODULE_METADATA_IO
    CHECK(!LND_MetadataFormatCanRead(LND_METADATA_AUTO));
    CHECK(!LND_MetadataFormatCanWrite(-1));
    CHECK(LND_MetadataFormatCanRead(LND_METADATA_ID3V1) == !!LND_MODULE_METADATA_ID3V1);
    CHECK(LND_MetadataFormatCanWrite(LND_METADATA_ID3V2) == !!LND_MODULE_METADATA_ID3V2);
    CHECK(LND_MetadataFormatCanRead(LND_METADATA_WAVE) == !!LND_MODULE_METADATA_WAVE);
    CHECK(LND_MetadataFormatCanWrite(LND_METADATA_OPUS) == !!LND_MODULE_METADATA_COMMENTS);
    CHECK(LND_MetadataFormatCanRead(LND_METADATA_FLAC) == !!LND_MODULE_METADATA_COMMENTS);
    CHECK(!LND_MetadataFormatCanWrite(LND_METADATA_FLAC));
#endif
    test_init(LND_LAYOUT_INTERLEAVED);
#if LND_MODULE_METADATA_IO
    io_contract();
#endif
#if LND_MODULE_METADATA_IO && LND_MODULE_METADATA_WAVE
    rf64();
#endif
#if LND_MODULE_METADATA_WAVE
    wave();
#endif
#if LND_MODULE_METADATA_IO && LND_MODULE_METADATA_COMMENTS
    opus_file();
#endif
#if LND_MODULE_METADATA_IO && LND_MODULE_METADATA_ID3V2
    mp3_file();
#endif
#if LND_MODULE_OUTPUT && LND_MODULE_METADATA_IO
#if LND_MODULE_WAV_ENCODER && LND_MODULE_METADATA_WAVE
    encoder("wav");
#endif
#if LND_MODULE_OPUS_ENCODER && LND_MODULE_METADATA_COMMENTS
    encoder("opus");
#endif
#if LND_MODULE_MP3_ENCODER && LND_MODULE_METADATA_ID3V2
    encoder("mp3");
#endif
#endif
    LND_LibraryFree();
    CHECK(!live_allocations);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
