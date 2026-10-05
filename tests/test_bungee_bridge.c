#include "processing/stretch/bungee/bridge.h"
#include "lindar_bungee.h"
#include "lindar.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static unsigned checks, failures, live;
static int32_t fail_after = -1;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

#ifdef LND_BG_ALLOC_WRAP
extern void *__real_malloc(size_t size);
extern void __real_free(void *memory);
#define allocate __real_malloc
#define release __real_free
#else
#define allocate malloc
#define release free
#endif

void *lnd_bg_test_allocate(size_t size) {
    if (!fail_after)
        return nullptr;
    if (fail_after > 0)
        fail_after--;
    void *p = allocate(size);
    if (p)
        live++;
    return p;
}
void lnd_bg_test_release(void *memory) {
    if (memory)
        live--;
    release(memory);
}
#ifdef LND_BG_ALLOC_WRAP
void *__wrap_malloc(size_t size) { return lnd_bg_test_allocate(size); }
void __wrap_free(void *memory) { lnd_bg_test_release(memory); }
#endif

static void allocations(void) {
    unsigned baseline = live;
    int32_t error = 0;
    bool success = false;
    for (int32_t i = 0; i < 512 && !success; i++) {
        fail_after = i;
        void *engine = lnd_bg_create(2, 48000, 0, &error);
        fail_after = -1;
        success = engine != nullptr;
        CHECK(success || error == LND_ERR_OUT_OF_MEMORY);
        if (engine)
            lnd_bg_destroy(engine);
        CHECK(live == baseline);
    }
    CHECK(success);
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK && LND_LibraryInit() == LND_OK);
    baseline = live;
    success = false;
    for (int32_t i = 0; i < 512 && !success; i++) {
        fail_after = i;
        LND_NODE *node = LND_NodeCreateBungee(1, 16000, nullptr);
        fail_after = -1;
        success = node != nullptr;
        CHECK(success || LND_ErrorGetLast() == LND_ERR_OUT_OF_MEMORY);
        if (node)
            CHECK(LND_NodeFree(node) == LND_OK);
        CHECK(live == baseline);
    }
    CHECK(success);
    LND_NODE *node = LND_NodeCreateBungee(1, 16000, nullptr);
    CHECK(node != nullptr);
    baseline = live;
    success = false;
    for (int32_t i = 0; i < 512 && !success; i++) {
        fail_after = i;
        int32_t result = LND_NodeResetBungee(node);
        fail_after = -1;
        success = result == LND_OK;
        CHECK(success || result == LND_ERR_OUT_OF_MEMORY);
        LND_BUNGEE_INFO info;
        CHECK(LND_NodeGetBungeeInfo(node, &info) == LND_OK && info.error == LND_OK);
        CHECK(live == baseline);
    }
    CHECK(success);
    CHECK(LND_NodeFree(node) == LND_OK);
    LND_LibraryFree();
}

static void steady_state(void) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK && LND_LibraryInit() == LND_OK);
    static float data[16000], out[256];
    for (unsigned i = 0; i < 16000; i++)
        data[i] = (float)(0.5 * sin(i * 0.17));
    LND_PCM pcm = {.data = data, .frames = 16000, .channels = 1, .format = LND_FORMAT_F32};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .sample_rate_hz = 16000, .channels = 1});
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_NODE *node = LND_NodeCreateBungee(1, 16000, &(LND_BUNGEE_CONFIG){.pitch_ratio = 2, .tempo_ratio = 0.5f});
    CHECK(node && source && sound);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(source), node) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(node);
    CHECK(renderer && LND_SoundPlay(sound) == LND_OK);
    pcm = (LND_PCM){.data = out, .frames = 256, .channels = 1, .format = LND_FORMAT_F32};
    uint64_t total = 0;
    fail_after = 0;
    for (unsigned i = 0; i < 200; i++) {
        int64_t n = LND_RendererReadPcm(renderer, &pcm, 0, 256);
        CHECK(n >= 0);
        if (n <= 0)
            break;
        total += (uint64_t)n;
        if (LND_NodeGetStatus(node) == LND_SOURCE_EOF)
            break;
    }
    fail_after = -1;
    CHECK(total == 32000 && LND_NodeGetStatus(node) == LND_SOURCE_EOF);
    LND_LibraryFree();
}

static void large_positions(void) {
    int32_t error;
    void *engine = lnd_bg_create(1, 16000, 0, &error);
    CHECK(engine && error == LND_OK);
    static float data[65536];
    lnd_bg_request r = {.position = 0x1p40, .speed = 1, .pitch_ratio = 1, .reset = 1};
    lnd_bg_preroll(engine, &r);
    fail_after = 0;
    for (unsigned i = 0; i < 16; i++) {
        int32_t begin = 0, end = 0;
        CHECK(lnd_bg_specify(engine, &r, floor(r.position), &begin, &end) == LND_OK);
        CHECK(begin < 0 && end > 0 && end - begin < 65536);
        lnd_bg_chunk out;
        CHECK(lnd_bg_process(engine, data, 65536, &out) == LND_OK);
        if (i > 2)
            CHECK(out.begin > 0x1p39 && out.end > out.begin);
        lnd_bg_next(engine, &r);
    }
    fail_after = -1;
    lnd_bg_destroy(engine);
}

int main(void) {
    /* OpenSSL keeps process-wide allocations until exit. */
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK && LND_LibraryInit() == LND_OK);
    LND_LibraryFree();
    unsigned baseline = live;
    allocations();
    steady_state();
    large_positions();
    CHECK(live == baseline);
    printf("Bungee boundary: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
