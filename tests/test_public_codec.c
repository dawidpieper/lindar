#include "lindar_codecs.h"
#include "lindar_decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned closed;
static void *allocate(void *user, size_t bytes) {
    (void)user;
    return malloc(bytes);
}
static void *resize(void *user, void *memory, size_t bytes) {
    (void)user;
    return realloc(memory, bytes);
}
static void release(void *user, void *memory) {
    (void)user;
    free(memory);
}
static int32_t open_file(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    (void)io;
    (void)info;
    (void)state;
    return LND_ERR_UNSUPPORTED;
}
static uint64_t read_file(void *state, void *dst, uint64_t frames) {
    (void)state;
    (void)dst;
    (void)frames;
    return LND_CODEC_READ_ERROR;
}
static void close_stream(void *state) {
    closed++;
    free(state);
}
static bool probe(const uint8_t *data, size_t bytes) { return bytes >= 4 && !memcmp(data, "TEST", 4); }
static void *create(void) { return calloc(1, sizeof(bool)); }
static int32_t step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    bool *opened = state;
    *used = 0;
    *pcm = (LND_PCM){0};
    if (!*opened) {
        if (bytes < 4) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        if (!probe(data, bytes)) return LND_ERR_FORMAT;
        *opened = true;
        data += 4;
        bytes -= 4;
        *used = 4;
        *info = (LND_CODEC_INFO){.format = LND_FORMAT_U8, .channels = 1, .sample_rate_hz = 8000};
    }
    *used += bytes;
    *pcm = (LND_PCM){.data = (void *)data, .frames = bytes, .channels = 1, .format = LND_FORMAT_U8, .layout = LND_LAYOUT_INTERLEAVED};
    return bytes ? LND_SOURCE_READY : end ? LND_SOURCE_EOF : LND_SOURCE_WAITING;
}
static const LND_CODEC_STREAM stream = {.probe = probe, .create = create, .step = step, .close = close_stream};
static const LND_CODEC codec = {.name = "public-test", .extensions = "test", .open = open_file, .read = read_file, .close = close_stream, .stream = &stream};
#define REQUIRE(x)                                                                                                                                             \
    do {                                                                                                                                                       \
        if (!(x)) {                                                                                                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                                                                         \
            return 1;                                                                                                                                          \
        }                                                                                                                                                      \
    } while (0)

int main(void) {
    REQUIRE(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    REQUIRE(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_U8) == LND_OK);
    REQUIRE(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .realloc = resize, .free = release}) == LND_OK);
    REQUIRE(LND_LibraryInit() == LND_OK);
    REQUIRE(LND_CodecRegister(&codec) == LND_OK);
    LND_DECODER_OPTIONS options = {.codec_name = codec.name, .input_bytes = 4096, .block_frames = 16};
    LND_DECODER *decoder = LND_DecoderCreate(&options);
    REQUIRE(decoder);
    REQUIRE(LND_DecoderStep(decoder) == LND_SOURCE_WAITING);
    uint8_t data[68] = {'T', 'E', 'S', 'T'}, output[64] = {0};
    for (unsigned i = 4; i < sizeof data; i++) data[i] = (uint8_t)(i * 7);
    REQUIRE(LND_DecoderFeed(decoder, data, 3) == 3);
    REQUIRE(LND_DecoderStep(decoder) == LND_SOURCE_WAITING);
    REQUIRE(LND_DecoderFeed(decoder, data + 3, 65) == 65);
    REQUIRE(LND_DecoderStep(decoder) == LND_SOURCE_READY);
    REQUIRE(LND_DecoderGetCodec(decoder) == &codec);
    LND_CODEC_INFO info;
    REQUIRE(LND_DecoderGetInfo(decoder, &info) == LND_OK && info.sample_rate_hz == 8000 && info.channels == 1 && !info.length_known);
    LND_PCM pcm = {.data = output, .frames = 64, .channels = 1, .format = LND_FORMAT_U8, .layout = LND_LAYOUT_INTERLEAVED};
    REQUIRE(LND_DecoderReadPcm(decoder, &pcm, 0, 13) == 13);
    REQUIRE(LND_DecoderReadPcm(decoder, &pcm, 13, 51) == 51);
    REQUIRE(!memcmp(output, data + 4, sizeof output));
    REQUIRE(LND_DecoderStep(decoder) == LND_SOURCE_WAITING);
    REQUIRE(LND_DecoderEnd(decoder) == LND_OK);
    REQUIRE(LND_DecoderStep(decoder) == LND_SOURCE_EOF);
    REQUIRE(LND_DecoderFeed(decoder, data, 1) == LND_ERR_STATE);
    LND_DecoderFree(decoder);
    REQUIRE(closed == 1);
    REQUIRE(LND_CodecUnregister(&codec) == LND_OK);
    LND_LibraryFree();
    return 0;
}
