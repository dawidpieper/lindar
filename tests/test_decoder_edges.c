#include "lindar_graph.h"
#include "lindar_codecs.h"
#include "lindar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, reads, closes, zero_allocations;
static uint64_t length;
static int mode;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

static void *allocate(void *user, size_t bytes) {
    if (!bytes) zero_allocations++;
    return bytes <= 1048576 ? malloc(bytes) : nullptr;
}
static void *resize(void *user, void *memory, size_t bytes) { return bytes <= 1048576 ? realloc(memory, bytes) : nullptr; }
static void release(void *user, void *memory) { free(memory); }
static int32_t probe(LND_IO *io) { return 100; }
static int32_t open_decoder(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    *info = (LND_CODEC_INFO){.format = LND_FORMAT_S32, .channels = 2, .sample_rate_hz = 8000, .length_frames = length};
    *state = &mode;
    return LND_OK;
}
static uint64_t read_decoder(void *state, void *dst, uint64_t frames) {
    reads++;
    if (mode == 0) return frames + 1;
    if (mode == 1) return frames;
    int32_t *samples = dst;
    samples[0] = 1073741824;
    samples[1] = -1073741824;
    return 1;
}
static void close_decoder(void *state) { closes++; }

int main(void) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .realloc = resize, .free = release}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_CODEC codec = {.name = "edge-test", .probe = probe, .open = open_decoder, .read = read_decoder, .close = close_decoder};
    CHECK(LND_CodecRegister(&codec) == LND_OK);
    LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "edge-test"};
    const char input[] = "edge-test";
    length = UINT64_C(1) << 61;
    CHECK(!LND_SourceCreateEncodedMemory(input, sizeof input, LND_ENCODED_SOURCE_PRELOAD, &options));
    CHECK(LND_ErrorGetLast() == LND_ERR_OUT_OF_MEMORY && !reads && closes == 1 && !zero_allocations);
    length = 4;
    CHECK(!LND_SourceCreateEncodedMemory(input, sizeof input, LND_ENCODED_SOURCE_PRELOAD, &options));
    CHECK(LND_ErrorGetLast() == LND_ERR_IO && reads == 1 && closes == 2);
    mode = 1;
    length = 0;
    CHECK(!LND_SourceCreateEncodedMemory(input, sizeof input, LND_ENCODED_SOURCE_PRELOAD, &options));
    CHECK(LND_ErrorGetLast() == LND_ERR_OUT_OF_MEMORY && closes == 3);
    mode = 2;
    length = 4;
    reads = 0;
    LND_SOURCE *source = LND_SourceCreateEncodedMemory(input, sizeof input, LND_ENCODED_SOURCE_PRELOAD, &options);
    CHECK(source != nullptr && reads == 4 && closes == 4);
    CHECK(LND_SourceGetLengthFrames(source) == 4);
    int16_t pcm[8];
    CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 4) == 4);
    for (unsigned f = 0; f < 4; f++) CHECK(pcm[f * 2] == 16384 && pcm[f * 2 + 1] == -16384);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(LND_CodecUnregister(&codec) == LND_OK);
    LND_LibraryFree();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
