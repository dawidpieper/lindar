#include "lindar_graph.h"
#include "lindar_pcm_float.h"
#include "lindar.h"
#include "lindar_buffers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, allocations;
static bool deny_alloc;
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

static void test_input(int32_t format, int32_t layout) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, format) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, layout) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .free = release}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    int32_t samples[] = {1073741824, -536870912, 536870912};
    LND_PCM pcm = {.data = samples, .channels = 1, .frames = 3, .format = LND_FORMAT_S32};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 16000};
    LND_SOURCE *source = LND_SourceCreate(&config);
    CHECK(source != nullptr);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(sound != nullptr);
    LND_NODE *input = LND_SourceEnsureNode(source);
    CHECK(input && LND_NodeGetType(input) == LND_NODE_PCM_INPUT);
    CHECK(LND_SoundEnsureNode(sound) == input && LND_SourceEnsureNode(source) == input);
    CHECK(LND_NodeEnsureSource(input) == source && LND_NodeEnsureSound(input, nullptr) == sound);
    CHECK(LND_SourceFree(source) == LND_ERR_BUSY && LND_SoundFree(sound) == LND_ERR_BUSY);
    LND_NODE *bus = LND_NodeCreateBus(1, 16000);
    CHECK(bus != nullptr);
    CHECK(LND_SoundEnsureNode(nullptr) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_SoundGetOutput(nullptr) == nullptr);
    CHECK(LND_SoundSetOutput(sound, bus) == LND_OK && LND_SoundGetOutput(sound) == bus);
    CHECK(LND_SoundSetOutput(sound, input) == LND_ERR_CYCLE && LND_SoundGetOutput(sound) == bus);
    LND_SOUND *mixed = LND_NodeEnsureSound(bus, nullptr);
    CHECK(mixed && LND_SoundGetLengthFrames(mixed) == 3);
    LND_RENDERER *renderer = LND_RendererCreateNode(bus);
    CHECK(renderer != nullptr);
    CHECK(LND_SoundSetLoop(mixed, true) == LND_OK && LND_SoundGetLoop(sound));
    CHECK(LND_SoundPlay(mixed) == LND_OK && LND_SoundGetState(sound) == LND_SOUND_PLAYING);
    int32_t data[7];
    LND_PCM output = {.data = data, .channels = 1, .frames = 7, .format = LND_FORMAT_S32};
    deny_alloc = true;
    unsigned before = allocations;
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 7) == LND_OK);
    for (unsigned n = 0; n < 7; n++) CHECK(data[n] == samples[n % 3]);
    CHECK(LND_SoundGetPositionFrames(mixed) == 1 && LND_SoundGetPositionFrames(sound) == 1);
    CHECK(LND_SoundSetPause(mixed, true) == LND_OK && LND_SoundGetState(sound) == LND_SOUND_PAUSED);
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 7) == LND_OK);
    for (unsigned n = 0; n < 7; n++) CHECK(data[n] == 0);
    CHECK(LND_SoundGetPositionFrames(sound) == 1);
    CHECK(LND_SoundSeekFrames(mixed, 2) == LND_OK && LND_SoundGetPositionFrames(sound) == 2);
    CHECK(LND_SoundPlay(mixed) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 1) == LND_OK && data[0] == samples[2]);
    CHECK(LND_SoundStop(mixed) == LND_OK && LND_SoundGetPositionFrames(sound) == 0);
    CHECK(allocations == before);
    CHECK(LND_NodeFree(input) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    LND_LibraryFree();
    deny_alloc = false;
}

static void test_resample(void) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    float data[64];
    for (unsigned n = 0; n < 64; n++) data[n] = 0.5f;
    LND_PCM pcm = {.data = data, .channels = 1, .frames = 64, .format = LND_FORMAT_F32};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 16000, .block_frames = 32};
    LND_SOURCE *source = LND_SourceCreate(&config);
    CHECK(source != nullptr);
    LND_SOUND *sound = LND_SourceEnsureSound(source, &(LND_SOUND_CONFIG){.sample_rate_hz = 32000, .flags = LND_SOUND_RESAMPLE_LINEAR});
    CHECK(sound && LND_SoundGetSource(sound) == source && LND_SoundGetSampleRateHz(sound) == 32000 && LND_SoundGetLengthFrames(sound) == 128);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    float output[64];
    CHECK(LND_SoundReadF32(sound, output, 64) == 64);
    for (unsigned n = 16; n < 64; n++) CHECK(output[n] > 0.49f && output[n] < 0.51f);
    LND_LibraryFree();
}

static void test_offline(uint32_t sample_rate_hz) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, LND_LAYOUT_PLANAR) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    int16_t data[64];
    for (unsigned n = 0; n < 64; n++) data[n] = (int16_t)(n * 256 - 8192);
    LND_PCM pcm = {.data = data, .channels = 1, .frames = 64, .format = LND_FORMAT_S16};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 16000};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(source, &(LND_SOUND_CONFIG){.sample_rate_hz = sample_rate_hz, .flags = LND_SOUND_RESAMPLE_LINEAR});
    CHECK(sound != nullptr);
    LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_S16, 1, 16000, 64);
    CHECK(buffer != nullptr);
    memcpy(LND_BufferGetData(buffer), data, sizeof data);
    LND_SOURCE *reference_source = LND_SourceCreateBuffer(buffer);
    LND_SOUND *reference = LND_SourceEnsureSound(reference_source, &(LND_SOUND_CONFIG){.sample_rate_hz = sample_rate_hz, .flags = LND_SOUND_RESAMPLE_LINEAR});
    CHECK(reference != nullptr);
    unsigned total = 0;
    float output[13], expected[13];
    for (unsigned n = 0; n < 20; n++) {
        uint64_t got = LND_SoundReadF32(sound, output, 13);
        uint64_t want = LND_SoundReadF32(reference, expected, 13);
        CHECK(got == want);
        for (unsigned f = 0; f < got && f < want; f++) CHECK(output[f] == expected[f]);
        total += (unsigned)got;
        CHECK(LND_SoundGetPositionFrames(sound) == LND_SoundGetPositionFrames(reference));
        if (got < 13) break;
    }
    CHECK(total >= 62 && total <= 128);
    CHECK(LND_SoundReadF32(sound, output, 13) == 0);
    CHECK(LND_SoundSeekFrames(sound, 20) == LND_OK);
    CHECK(LND_SoundSeekFrames(reference, 20) == LND_OK);
    CHECK(LND_SoundReadF32(sound, output, 13) == 13);
    CHECK(LND_SoundReadF32(reference, expected, 13) == 13);
    for (unsigned f = 0; f < 13; f++) CHECK(output[f] == expected[f]);
    uint64_t pos = LND_SoundGetPositionFrames(sound);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_SoundGetPositionFrames(sound) == pos);
    LND_LibraryFree();
}

int main(void) {
    for (int32_t format = LND_FORMAT_U8; format <= LND_FORMAT_F64; format++)
        for (int32_t layout = 0; layout <= 1; layout++) test_input(format, layout);
    test_resample();
    test_offline(16000);
    test_offline(32000);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
