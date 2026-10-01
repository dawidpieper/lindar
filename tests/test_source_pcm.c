#include "lindar_graph.h"
#include "lindar_buffers.h"
#include "lnd_modules.h"
#if LND_MODULE_CODECS
#include "lindar_codecs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, allocations;
static bool deny_alloc;
static unsigned position;
static int32_t samples[258];
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)
static void *allocate(void *user, size_t bytes) {
    allocations++;
    return deny_alloc ? nullptr : malloc(bytes);
}
static void release(void *user, void *memory) { free(memory); }
static int64_t read_samples(void *user, void *dst, uint64_t frames) {
    size_t n = frames < 129 - position ? (size_t)frames : 129 - position;
    memcpy(dst, samples + position * 2, n * 2 * sizeof(int32_t));
    position += (unsigned)n;
    return n;
}
static int32_t seek_samples(void *user, uint64_t frame) {
    if (frame > 129) return LND_ERR_INVALID_ARG;
    position = (unsigned)frame;
    return LND_OK;
}
#if LND_MODULE_CODECS
static int32_t open_samples(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    position = 0;
    *info = (LND_CODEC_INFO){.format = LND_FORMAT_S32, .channels = 2, .sample_rate_hz = 16000, .length_frames = 129, .seekable = true};
    *state = &position;
    return LND_OK;
}
static void close_samples(void *state) {}
static uint64_t decode_samples(void *user, void *dst, uint64_t frames) { return (uint64_t)read_samples(user, dst, frames); }
#endif

static void verify(LND_SOURCE *source) {
    CHECK(source != nullptr);
    if (!source) return;
    unsigned before = allocations;
    deny_alloc = true;
    LND_PCM empty = {.channels = 2, .format = LND_FORMAT_F32};
    CHECK(LND_SourceReadPcm(source, &empty, 0, 0) == 0);
    empty.layout = LND_LAYOUT_PLANAR;
    CHECK(LND_SourceReadPcm(source, &empty, 0, 0) == 0);
    CHECK(LND_SourceGetPositionFrames(source) == 0);
    int32_t left[131], right[131];
    for (unsigned n = 0; n < 131; n++)
        left[n] = right[n] = 0x13579bdf;
    void *planes[] = {left, right};
    LND_PCM output = {.planes = planes, .frames = 131, .channels = 2, .format = LND_FORMAT_S32, .layout = LND_LAYOUT_PLANAR};
    CHECK(LND_SourceReadPcm(source, &output, 1, 13) == 13);
    CHECK(LND_SourceReadPcm(source, &output, 14, 116) == 116);
    CHECK(LND_SourceReadPcm(source, &output, 130, 1) == 0);
    CHECK(LND_SourceGetPositionFrames(source) == 129);
    for (unsigned n = 0; n < 129; n++)
        CHECK(left[n + 1] == samples[n * 2] && right[n + 1] == samples[n * 2 + 1]);
    CHECK(left[0] == 0x13579bdf && right[0] == 0x13579bdf && left[130] == 0x13579bdf && right[130] == 0x13579bdf);
    CHECK(LND_SourceSeekFrames(source, 5) == LND_OK);
    int32_t packed[12];
    CHECK(LND_SourceRead(source, packed, LND_FORMAT_S32, 6) == 6);
    CHECK(memcmp(packed, samples + 10, sizeof packed) == 0);
    CHECK(allocations == before);
    deny_alloc = false;
    CHECK(LND_SourceFree(source) == LND_OK);
}

static void run(int32_t layout, bool threaded) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, threaded ? LND_MODE_MANUAL : LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S32) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, layout) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .free = release}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_S32, 2, 16000, 129);
    CHECK(buffer != nullptr);
    memcpy(LND_BufferGetData(buffer), samples, sizeof samples);
    verify(LND_SourceCreateBuffer(buffer));
    LND_BufferFree(buffer);
    LND_SOURCE_PROCS procs = {.read = read_samples, .seek = seek_samples, .length_frames = 129};
    position = 0;
    verify(LND_SourceCreateProc(&procs, nullptr, LND_FORMAT_S32, 2, 16000, 0));
#if LND_MODULE_CODECS
    LND_CODEC codec = {.name = "source-pcm", .open = open_samples, .read = decode_samples, .seek = seek_samples, .close = close_samples};
    CHECK(LND_CodecRegister(&codec) == LND_OK);
    LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = codec.name};
    static const char input[] = "source-pcm";
    verify(LND_SourceCreateEncodedMemory(input, sizeof input, 0, &options));
    CHECK(LND_CodecUnregister(&codec) == LND_OK);
#endif
    LND_LibraryFree();
}

int main(void) {
    for (unsigned n = 0; n < 258; n++)
        samples[n] = n & 1 ? -(int32_t)(n * 7907 + 1073741825) : (int32_t)(n * 7919 + 1073741825);
    for (int32_t layout = 0; layout <= 1; layout++) {
        run(layout, false);
#if LND_THREADS && LND_MODULE_STREAM
        run(layout, true);
#endif
    }
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
