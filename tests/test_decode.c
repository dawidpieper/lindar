#include "codec_test.h"
#include "lindar_decode.h"

static void decode_file(const char *name, const char *codec, size_t chunk) {
    size_t size = 0;
    uint8_t *data = read_file(name, &size);
    if (!data) return;
    LND_ENCODED_SOURCE_OPTIONS file = {.codec_name = codec};
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(data, size, LND_ENCODED_SOURCE_LIGHTWEIGHT, &file);
    CHECK(reference != nullptr);
    if (!reference) {
        free(data);
        return;
    }
    uint32_t channels = LND_SourceGetChannels(reference);
    LND_DECODER_OPTIONS options = {.codec_name = codec, .input_bytes = 8192, .allow_buffered = true};
    LND_DECODER *decoder = LND_DecoderCreate(&options);
    CHECK(decoder != nullptr);
    size_t offset = 0;
    uint64_t frames = 0, different = 0;
    int max_difference = 0;
    int16_t out[256 * 32], expected[256 * 32];
    LND_PCM pcm = {.data = out, .frames = 256, .channels = channels, .format = LND_FORMAT_S16, .layout = LND_LAYOUT_INTERLEAVED};
    bool ended = false;
    for (unsigned iteration = 0; decoder && iteration < size * 3 + 10000; iteration++) {
        if (offset < size) {
            size_t n = size - offset < chunk ? size - offset : chunk;
            int64_t fed = LND_DecoderFeed(decoder, data + offset, n);
            CHECK(fed >= 0);
            if (fed < 0) break;
            offset += (size_t)fed;
        } else if (!ended) {
            CHECK(LND_DecoderEnd(decoder) == LND_OK);
            ended = true;
        }
        int64_t got = LND_DecoderReadPcm(decoder, &pcm, 0, 256);
        CHECK(got >= 0);
        if (got < 0) break;
        if (got) {
            uint64_t want = LND_SourceRead(reference, expected, LND_FORMAT_S16, (uint64_t)got);
            CHECK(want == (uint64_t)got);
            if (want == (uint64_t)got) {
                for (size_t j = 0; j < (size_t)got * channels; j++) {
                    int delta = abs(out[j] - expected[j]);
                    if (delta) {
                        if (!different) printf("first mismatch %s at %llu: %d vs %d\n", name, (unsigned long long)(frames * channels + j), out[j], expected[j]);
                        different++;
                        if (delta > max_difference) max_difference = delta;
                    }
                }
            }
            frames += (uint64_t)got;
        }
        if (LND_DecoderGetStatus(decoder) == LND_SOURCE_EOF) break;
    }
    CHECK(LND_DecoderGetStatus(decoder) == LND_SOURCE_EOF);
    CHECK(frames > 0);
    CHECK(different == 0);
    printf("different=%llu max=%d\n", (unsigned long long)different, max_difference);
    CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, 1) == 0);
    printf("%s chunk=%zu frames=%llu\n", codec, chunk, (unsigned long long)frames);
    LND_DecoderFree(decoder);
    CHECK(LND_SourceFree(reference) == LND_OK);
    free(data);
}

typedef struct input_state {
    unsigned step, closed;
} input_state;
static int64_t input_read(void *user, void *dst, size_t size) {
    input_state *s = user;
    if (s->step++ == 0) return 0;
    if (s->step == 2) {
        *(uint8_t *)dst = 42;
        return 1;
    }
    return LND_READ_EOF;
}
static void input_close(void *user) { ((input_state *)user)->closed++; }

#if LND_MODULE_WAV_DECODER
static void test_wav_info(void) {
    uint8_t header[36] = {
        'R', 'I', 'F', 'F', 28, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
        0x80, 0xbb, 0, 0, 0, 0x77, 1, 0, 2, 0, 16, 0,
    };
    static const struct { size_t offset; uint8_t value; } cases[] = {{22, 0}, {22, 255}, {32, 0}, {34, 0}};
    LND_DECODER_OPTIONS options = {.codec_name = "wav", .input_bytes = 4096};
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        uint8_t data[sizeof header];
        memcpy(data, header, sizeof data);
        data[cases[i].offset] = cases[i].value;
        LND_DECODER *decoder = LND_DecoderCreate(&options);
        CHECK(decoder != nullptr);
        if (!decoder) continue;
        CHECK(LND_DecoderFeed(decoder, data, sizeof data) == sizeof data);
        CHECK(LND_DecoderEnd(decoder) == LND_OK);
        CHECK(LND_DecoderStep(decoder) == LND_SOURCE_READY);
        CHECK(LND_DecoderStep(decoder) == LND_ERR_FORMAT);
        LND_CODEC_INFO info;
        CHECK(LND_DecoderGetInfo(decoder, &info) == LND_ERR_FORMAT);
        LND_DecoderFree(decoder);
    }
}
#endif

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
#if LND_MODULE_WAV_DECODER
    test_wav_info();
#endif
    input_state input = {0};
    LND_IO_STREAM_INPUT_PROCS procs = {.read = input_read, .close = input_close};
    LND_IO *io = LND_IoCreateStream(&procs, &input, LND_IO_SIZE_UNKNOWN);
    uint8_t byte = 0;
    CHECK(io && !LND_IoCanSeek(io));
    CHECK(LND_IoReadSome(io, &byte, 1) == 0 && LND_IoGetStatus(io) == LND_SOURCE_WAITING);
    CHECK(LND_IoReadSome(io, &byte, 1) == 1 && byte == 42 && LND_IoGetPositionBytes(io) == 1);
    CHECK(LND_IoReadSome(io, &byte, 1) == 0 && LND_IoGetStatus(io) == LND_SOURCE_EOF);
    LND_IoFree(io);
    CHECK(input.closed == 1);
    const size_t chunks[] = {1, 7, 257, 4096};
    for (size_t i = 0; i < sizeof chunks / sizeof *chunks; i++) {
#if LND_MODULE_WAV_DECODER
        decode_file("audiosamples/tone.wav", "wav", chunks[i]);
#endif
#if LND_MODULE_MP3_DECODER
        decode_file("audiosamples/tone.mp3", "mp3", chunks[i]);
#endif
#if LND_MODULE_OPUS_DECODER
        decode_file("audiosamples/tone.opus", "opus", chunks[i]);
#endif
#if LND_MODULE_VORBIS_DECODER
        decode_file("audiosamples/tone.ogg", "vorbis", chunks[i]);
#endif
#if LND_MODULE_FLAC_DECODER
        decode_file("audiosamples/tone.flac", "flac", chunks[i]);
#endif
#if LND_MODULE_AAC_DECODER
        decode_file("audiosamples/tone.aac", "aac", chunks[i]);
        decode_file("audiosamples/tone.he.aac", "aac", chunks[i]);
#endif
    }
#if LND_MODULE_SPEEX_DECODER
    decode_file("audiosamples/tone.spx", "speex", 7);
#endif
    LND_LibraryFree();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
