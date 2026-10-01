#include "lindar_graph.h"
#include "lindar_buffers.h"
#include "lindar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, allocations;
static bool deny_alloc;
static unsigned live;
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
    void *memory = deny_alloc ? nullptr : malloc(bytes);
    if (memory) live++;
    return memory;
}
static void release(void *user, void *memory) {
    if (memory) live--;
    free(memory);
}

static int32_t quantize(int64_t value, int32_t format) {
    int32_t clipped = (int32_t)(value > INT32_MAX ? INT32_MAX : value < INT32_MIN ? INT32_MIN : value);
    unsigned char data[8];
    LND_PCM source = {.data = &clipped, .frames = 1, .channels = 1, .format = LND_FORMAT_S32};
    LND_PCM native = {.data = data, .frames = 1, .channels = 1, .format = format};
    CHECK(LND_PcmConvert(&native, 0, &source, 0, 1) == LND_OK);
    CHECK(LND_PcmConvert(&source, 0, &native, 0, 1) == LND_OK);
    return clipped;
}

static void fill(LND_RENDERER *renderer, int32_t *data, size_t count, bool planar) {
    int32_t left[25], right[25];
    memset(left, 0x6a, sizeof left);
    memset(right, 0x6a, sizeof right);
    void *planes[] = {left, right};
    LND_PCM pcm = {
        .data = data, .planes = planes, .frames = 25, .channels = 2, .format = LND_FORMAT_S32, .layout = planar ? LND_LAYOUT_PLANAR : LND_LAYOUT_INTERLEAVED};
    CHECK(LND_RendererFillPcm(renderer, &pcm, 1, count) == LND_OK);
    if (planar)
        for (size_t f = 0; f < 25; f++) {
            data[f * 2] = left[f];
            data[f * 2 + 1] = right[f];
        }
}

static void test_graph(int32_t format, int32_t layout) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, format) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, layout) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_MIX_BLOCK_FRAMES, 32) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_MIXER_BUFFER_FRAMES, 1024) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .free = release}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    int32_t a[14], b[14], expected[14];
    for (unsigned n = 0; n < 14; n++) {
        a[n] = (n & 1 ? -1 : 1) * (int32_t)(123456789 + n * 7919);
        b[n] = (int32_t)(65432197 + n * 101);
        int32_t qa = quantize(a[n], format), qb = quantize(b[n], format);
        expected[n] = quantize((int64_t)qa + qb, format);
    }
    LND_PCM pcm = {.data = a, .frames = 7, .channels = 2, .format = LND_FORMAT_S32};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 2, .sample_rate_hz = 16000};
    LND_SOURCE *native = LND_SourceCreate(&config);
    LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_S32, 2, 16000, 7);
    CHECK(native && buffer);
    memcpy(LND_BufferGetData(buffer), b, sizeof b);
    LND_SOURCE *legacy = LND_SourceCreateBuffer(buffer);
    LND_NODE *bus = LND_NodeCreateMixer(2, 16000, LND_MIX_LOCKSTEP);
    CHECK(bus && legacy);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(native), bus) == LND_OK);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(legacy), bus) == LND_OK);
    LND_NODE *split = LND_NodeCreateChannelSplitter(2, 16000);
    LND_NODE *merge = LND_NodeCreateChannelMerger(2, 16000);
    CHECK(split && merge && LND_NodeConnect(bus, split) == LND_OK);
    LND_NODE *left = LND_NodeGetSplitterOutput(split, 0), *right = LND_NodeGetSplitterOutput(split, 1);
    CHECK(left && right);
    CHECK(LND_NodeConnect(left, merge) == LND_OK && LND_NodeConnect(right, merge) == LND_OK);
    CHECK(LND_NodeSetChannelMergerInput(merge, 1, left) == LND_OK && LND_NodeSetChannelMergerInput(merge, 0, right) == LND_OK);
    LND_RENDERER *direct = LND_RendererCreateNode(bus), *swapped = LND_RendererCreateNode(merge);
    CHECK(direct && swapped);
    LND_SOUND *sound = LND_NodeEnsureSound(bus, nullptr);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    LND_SOURCE *view = LND_NodeEnsureSource(bus);
    CHECK(view && LND_SourceGetFormat(view) == format);
    unsigned before = allocations;
    deny_alloc = true;
    size_t position = 0;
    const size_t sizes[] = {3, 13, 19, 7, 5, 17, 2, 11, 23};
    for (unsigned chunk = 0; chunk < 12 * (sizeof sizes / sizeof *sizes); chunk++) {
        size_t count = sizes[chunk % (sizeof sizes / sizeof *sizes)];
        int32_t regular[50], reverse[50];
        memset(regular, 0x6a, sizeof regular);
        memset(reverse, 0x6a, sizeof reverse);
        fill(direct, regular, count, (chunk & 1) != 0);
        fill(swapped, reverse, count, (chunk & 2) != 0);
        for (size_t f = 0; f < count; f++) {
            size_t at = (position + f) % 7;
            CHECK(regular[(f + 1) * 2] == expected[at * 2] && regular[(f + 1) * 2 + 1] == expected[at * 2 + 1]);
            CHECK(reverse[(f + 1) * 2] == expected[at * 2 + 1] && reverse[(f + 1) * 2 + 1] == expected[at * 2]);
        }
        CHECK(regular[0] == 0x6a6a6a6a && reverse[0] == 0x6a6a6a6a);
        CHECK(regular[(count + 1) * 2] == 0x6a6a6a6a && reverse[(count + 1) * 2] == 0x6a6a6a6a);
        position += count;
    }
    int32_t tail[26];
    LND_PCM view_pcm = {.data = tail, .frames = 13, .channels = 2, .format = LND_FORMAT_S32};
    CHECK(LND_SourceReadPcm(view, &view_pcm, 0, 13) == 13);
    for (unsigned f = 0; f < 13; f++) {
        size_t at = (position + f) % 7;
        CHECK(tail[f * 2] == expected[at * 2] && tail[f * 2 + 1] == expected[at * 2 + 1]);
    }
    CHECK(allocations == before);
    deny_alloc = false;
    LND_LibraryFree();
    CHECK(live == 0);
}

static void test_saturation(void) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S32) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, LND_LAYOUT_PLANAR) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .free = release}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_NODE *bus = LND_NodeCreateBus(1, 8000);
    int32_t values[] = {-1610612737, 1610612739, 536870913};
    for (unsigned n = 0; n < 3; n++) {
        LND_PCM pcm = {.data = &values[n], .frames = 1, .channels = 1, .format = LND_FORMAT_S32};
        LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 8000};
        LND_SOURCE *source = LND_SourceCreate(&config);
        CHECK(source && LND_NodeConnect(LND_SourceEnsureNode(source), bus) == LND_OK);
    }
    LND_SOUND *sound = LND_NodeEnsureSound(bus, nullptr);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(bus);
    int32_t sample;
    LND_PCM output = {.data = &sample, .frames = 1, .channels = 1, .format = LND_FORMAT_S32};
    CHECK(renderer && LND_RendererFillPcm(renderer, &output, 0, 1) == LND_OK && sample == 536870915);
    CHECK(LND_NodeSetGain(bus, 0.5f) == LND_OK && LND_NodeGetGain(bus) == 0.5f);
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 1) == LND_OK && sample == 268435458);
    CHECK(LND_NodeSetGain(bus, 4.0f) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 1) == LND_OK && sample == INT32_MAX);
    CHECK(LND_NodeSetGain(bus, 0.0f) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 1) == LND_OK && sample == 0);
    LND_LibraryFree();
    CHECK(live == 0);
}

int main(void) {
    for (int32_t format = LND_FORMAT_U8; format <= LND_FORMAT_F64; format++)
        for (int32_t layout = 0; layout <= 1; layout++)
            test_graph(format, layout);
    test_saturation();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
