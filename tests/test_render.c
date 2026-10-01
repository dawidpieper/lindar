#include "lindar_graph.h"
#include "lindar_buffers.h"
#include "lindar.h"

#include <math.h>
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
#define NEAR(a, b, e) CHECK(fabs((double)(a) - (double)(b)) <= (e))

static void *allocate(void *user, size_t size) {
    (void)user;
    allocations++;
    return deny_alloc ? nullptr : malloc(size);
}

static void *resize(void *user, void *p, size_t size) {
    (void)user;
    allocations++;
    return deny_alloc ? nullptr : realloc(p, size);
}

static void release(void *user, void *p) {
    (void)user;
    free(p);
}

static void begin(void) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .realloc = resize, .free = release}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_REALTIME) == LND_ERR_STATE);
}

static void test_render(void) {
    begin();
    LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_F32, 2, 48000, 3);
    float values[] = {0.25f, -0.5f, -0.25f, 0.5f, 0.75f, -0.75f};
    memcpy(LND_BufferGetData(buffer), values, sizeof values);
    LND_SOURCE *source = LND_SourceCreateBuffer(buffer);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_RENDERER *renderer = LND_RendererCreateNode(LND_SourceEnsureNode(source));
    CHECK(renderer && LND_RendererGetChannels(renderer) == 2 && LND_RendererGetSampleRateHz(renderer) == 48000);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK);
    for (int format = LND_FORMAT_U8; format <= LND_FORMAT_F64; format++) {
        for (int layout = 0; layout <= 1; layout++) {
            CHECK(LND_SoundStop(sound) == LND_OK);
            CHECK(LND_LibraryUpdate() == LND_OK);
            CHECK(LND_SoundSeekFrames(sound, 0) == LND_OK);
            CHECK(LND_SoundPlay(sound) == LND_OK);
            unsigned char data[2][9000];
            memset(data, 0x5a, sizeof data);
            void *planes[] = {data[0] + 1, data[1] + 1};
            LND_PCM pcm = {.data = data[0] + 1, .planes = planes, .frames = 515, .channels = 2, .format = format, .layout = layout};
            unsigned before = allocations;
            deny_alloc = true;
            CHECK(LND_RendererFillPcm(renderer, &pcm, 1, 513) == LND_OK);
            deny_alloc = false;
            CHECK(allocations == before);
            double result[1026];
            LND_PCM converted = {.data = result, .frames = 513, .channels = 2, .format = LND_FORMAT_F64};
            CHECK(LND_PcmConvert(&converted, 0, &pcm, 1, 513) == LND_OK);
            for (unsigned i = 0; i < 1026; i++)
                NEAR(result[i], values[i % 6], 1e-12);
            size_t stride = LND_PcmGetSampleBytes(format) * (layout ? 1 : 2);
            CHECK(data[0][0] == 0x5a && data[0][1] == 0x5a && data[0][1 + 514 * stride] == 0x5a);
            CHECK(LND_RendererFillPcm(renderer, &pcm, 514, 2) == LND_ERR_INVALID_ARG);
        }
    }
    CHECK(LND_SoundSetPause(sound, true) == LND_OK);
    float paused[12];
    LND_PCM pcm = {.data = paused, .frames = 6, .channels = 2, .format = LND_FORMAT_F32};
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 6) == LND_OK);
    for (unsigned i = 0; i < 12; i++)
        CHECK(paused[i] == 0.0f);
    CHECK(LND_SoundSetPause(sound, false) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 6) == LND_OK);
    CHECK(paused[0] != 0.0f);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 6) == LND_ERR_STATE);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    LND_BufferFree(buffer);
    LND_LibraryFree();
}

typedef struct generator {
    uint64_t position;
} generator;

static int64_t generate(void *user, void *dst, uint64_t frames) {
    generator *g = user;
    float *out = dst;
    for (uint64_t i = 0; i < frames; i++)
        out[i] = (float)((g->position++ % 4) - 2.0) * 0.25f;
    return frames;
}

static void test_proc(void) {
    begin();
    generator g = {0};
    LND_SOURCE_PROCS procs = {.read = generate};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &g, LND_FORMAT_F32, 1, 8000, 0);
    CHECK(source != nullptr);
    CHECK(g.position == 0);
    LND_NODE *bus = LND_NodeCreateBus(1, 8000);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(source), bus) == LND_OK);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_RENDERER *a = LND_RendererCreateNode(bus), *b = LND_RendererCreateNode(bus);
    CHECK(a && b);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    float first[37], second[37];
    LND_PCM pa = {.data = first, .frames = 37, .channels = 1, .format = LND_FORMAT_F32};
    LND_PCM pb = pa;
    pb.data = second;
    unsigned before = allocations;
    deny_alloc = true;
    CHECK(LND_RendererFillPcm(a, &pa, 0, 37) == LND_OK);
    CHECK(LND_RendererFillPcm(b, &pb, 0, 37) == LND_OK);
    deny_alloc = false;
    CHECK(allocations == before);
    CHECK(g.position == 37);
    CHECK(memcmp(first, second, sizeof first) == 0);
    CHECK(LND_NodeFree(bus) == LND_OK);
    CHECK(LND_RendererFillPcm(a, &pa, 0, 37) == LND_ERR_STATE);
    CHECK(LND_RendererFree(a) == LND_OK && LND_RendererFree(b) == LND_OK);
    LND_LibraryFree();
}

int main(void) {
    CHECK(LND_LibraryUpdate() == LND_ERR_STATE);
    test_render();
    test_proc();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
