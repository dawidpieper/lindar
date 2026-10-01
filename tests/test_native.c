#include "lindar.h"
#include "lnd_modules.h"

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

static void *allocate(void *user, size_t size) {
    allocations++;
    return deny_alloc ? nullptr : malloc(size);
}

static void release(void *user, void *memory) { free(memory); }

typedef struct generator {
    int32_t format;
    int32_t layout;
    size_t remaining;
    size_t position;
    unsigned calls;
    unsigned closed;
    int64_t result;
    LND_RENDERER *renderer;
    bool reenter;
} generator;

static int64_t generate(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    generator *g = user;
    g->calls++;
    CHECK(pcm->format == g->format && pcm->layout == g->layout);
    CHECK(frames <= 7 && offset <= pcm->frames && frames <= pcm->frames - offset);
    if (g->reenter) {
        CHECK(LND_RendererFillPcm(g->renderer, pcm, offset, frames) == LND_ERR_BUSY);
        CHECK(LND_RendererFree(g->renderer) == LND_ERR_BUSY);
        CHECK(LND_LibraryUpdate() == LND_ERR_BUSY);
        CHECK(LND_LibraryInit() == LND_ERR_BUSY);
        CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, 1) == LND_ERR_BUSY);
        CHECK(LND_ConfigGet(LND_CFG_INTERNAL_FORMAT) == (uint64_t)g->format);
        LND_LibraryFree();
        CHECK(LND_ErrorGetLast() == LND_ERR_BUSY);
    }

    if (g->result) return g->result;
    size_t count = frames < g->remaining ? frames : g->remaining;
    int32_t samples[14];
    for (size_t f = 0; f < count; f++) {
        int32_t value = (int32_t)((g->position + f) % 4) * 536870912 - 1073741824;
        samples[f * 2] = value;
        samples[f * 2 + 1] = -value;
    }
    LND_PCM source = {.data = samples, .frames = count, .channels = 2, .format = LND_FORMAT_S32};
    CHECK(LND_PcmConvert(pcm, offset, &source, 0, count) == LND_OK);
    g->position += count;
    g->remaining -= count;
    return (int64_t)count;
}

static void close_generator(void *user) { ((generator *)user)->closed++; }

static void begin(int32_t format, int32_t layout) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, format) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, layout) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .free = release}) == LND_OK);
    LND_ALLOCATOR_CONFIG allocator;
    unsigned before = allocations;
    CHECK(LND_AllocatorGetConfig(&allocator) == LND_OK);
    CHECK(allocator.alloc == allocate && allocator.free == release && !allocator.realloc && !allocator.user);
    CHECK(allocations == before);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate}) == LND_ERR_INVALID_ARG);
    CHECK(LND_AllocatorGetConfig(&allocator) == LND_OK && allocator.alloc == allocate && allocator.free == release);
    CHECK(LND_LibraryInit() == LND_OK);
    CHECK(LND_AllocatorSetConfig(nullptr) == LND_ERR_STATE);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_U8) == LND_ERR_STATE);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, 1) == LND_ERR_STATE);
}

static void test_native(int32_t format, int32_t layout) {
    begin(format, layout);
    generator g = {.format = format, .layout = layout, .remaining = 100};
    LND_RENDERER_CONFIG config = {.render = generate, .close = close_generator, .user = &g, .channels = 2, .sample_rate_hz = 48000, .block_frames = 7};
    alignas(max_align_t) unsigned char memory[2048];
    size_t bytes = LND_RendererGetMemoryBytes(&config);
    CHECK(bytes <= sizeof memory && bytes > 0);
    unsigned before = allocations;
    deny_alloc = true;
    CHECK(!LND_RendererInit(memory, bytes - 1, &config));
    CHECK(!LND_RendererInit(memory + 1, bytes, &config));
    LND_RENDERER *r = LND_RendererInit(memory, bytes, &config);
    CHECK(r != nullptr);
    CHECK(LND_RendererGetSampleRateHz(r) == 48000 && LND_RendererGetChannels(r) == 2);
    CHECK(LND_RendererGetFormat(r) == format && LND_RendererGetLayout(r) == layout);
    CHECK(!LND_RendererInit(memory, bytes, &config) && LND_ErrorGetLast() == LND_ERR_BUSY);
    g.renderer = r;
    g.reenter = true;
    int32_t last_format = LND_MODULE_PCM_FLOAT ? LND_FORMAT_F64 : LND_FORMAT_S32;
    for (int32_t output_format = LND_FORMAT_U8; output_format <= last_format; output_format++) {
        for (int output_layout = 0; output_layout <= 1; output_layout++) {
            unsigned char data[2][700];
            memset(data, 0x57, sizeof data);
            void *planes[] = {data[0] + 1, data[1] + 1};
            LND_PCM output = {.data = data[0] + 1,
                              .planes = planes,
                              .frames = 29,
                              .stride_bytes = 3 * LND_PcmGetSampleBytes(output_format),
                              .channels = 2,
                              .format = output_format,
                              .layout = output_layout};
            g.position = 0;
            g.remaining = 100;
            CHECK(LND_RendererFillPcm(r, &output, 2, 25) == LND_OK);
            CHECK(g.position == 25 && g.remaining == 75);
            int32_t values[50];
            LND_PCM decoded = {.data = values, .frames = 25, .channels = 2, .format = LND_FORMAT_S32};
            CHECK(LND_PcmConvert(&decoded, 0, &output, 2, 25) == LND_OK);
            for (size_t f = 0; f < 25; f++) {
                int32_t expected = (int32_t)(f % 4) * 536870912 - 1073741824;
                CHECK(values[f * 2] == expected && values[f * 2 + 1] == -expected);
            }
            CHECK(data[0][0] == 0x57 && data[1][0] == 0x57);
            CHECK(data[0][1] == 0x57 && data[0][1 + 27 * output.stride_bytes] == 0x57);
        }
    }
    int32_t values[50];
    memset(values, 0x73, sizeof values);
    LND_PCM output = {.data = values, .frames = 25, .channels = 2, .format = LND_FORMAT_S32};
    g.position = 0;
    g.remaining = 3;
    unsigned calls = g.calls;
    CHECK(LND_RendererFillPcm(r, &output, 1, 23) == LND_OK);
    CHECK(g.calls == calls + 1 && g.position == 3);
    CHECK(values[0] == 0x73737373 && values[48] == 0x73737373);
    for (size_t n = 8; n < 48; n++) CHECK(values[n] == 0);
    calls = g.calls;
    CHECK(LND_RendererFillPcm(r, &output, 25, 0) == LND_OK && g.calls == calls);
    CHECK(LND_RendererFillPcm(r, &output, 24, 2) == LND_ERR_INVALID_ARG);
    g.result = 8;
    CHECK(LND_RendererFillPcm(r, &output, 0, 7) == LND_ERR_IO);
    g.result = LND_ERR_FORMAT;
    CHECK(LND_RendererFillPcm(r, &output, 0, 7) == LND_ERR_FORMAT);
    CHECK(allocations == before);
    CHECK(LND_RendererFree(r) == LND_OK && g.closed == 1);
    CHECK(LND_RendererFillPcm(r, &output, 0, 1) == LND_ERR_STATE);
    CHECK(LND_RendererFree(r) == LND_ERR_INVALID_ARG && g.closed == 1);
    r = LND_RendererInit(memory, bytes, &config);
    CHECK(r != nullptr);
    LND_LibraryFree();
    CHECK(g.closed == 2 && allocations == before);
    deny_alloc = false;
}

static int64_t exact(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    uint8_t input[] = {1, 0, 0, 64, 255, 255, 255, 127, 1, 0, 0, 128};
    LND_PCM source = {.data = input, .frames = 3, .channels = 1, .format = LND_FORMAT_S32};
    int32_t result = LND_PcmConvert(pcm, offset, &source, 0, frames);
    return result == LND_OK ? (int64_t)frames : result;
}

static void test_exact(void) {
    begin(LND_FORMAT_S32, LND_LAYOUT_PLANAR);
    LND_RENDERER_CONFIG config = {.render = exact, .channels = 1, .sample_rate_hz = 8000, .block_frames = 3};
    LND_RENDERER *r = LND_RendererCreateProc(&config);
    CHECK(r != nullptr);
    uint8_t output[12];
    uint8_t expected[] = {1, 0, 0, 64, 255, 255, 255, 127, 1, 0, 0, 128};
    LND_PCM pcm = {.data = output, .frames = 3, .channels = 1, .format = LND_FORMAT_S32};
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 3) == LND_OK);
    CHECK(memcmp(output, expected, sizeof output) == 0);
    CHECK(LND_RendererFree(r) == LND_OK);
    LND_LibraryFree();
}

static void test_config_keys(void) {
    uint32_t count = LND_ConfigGetKeyCount();
    CHECK(count >= 3);
    CHECK(LND_ConfigFindKey("core.run_mode") == LND_CFG_RUN_MODE);
    CHECK(LND_ConfigFindKey("core.internal_format") == LND_CFG_INTERNAL_FORMAT);
    CHECK(LND_ConfigFindKey("core.internal_layout") == LND_CFG_INTERNAL_LAYOUT);
    CHECK(!LND_ConfigFindKey(nullptr));
    CHECK(!LND_ConfigFindKey("missing.key"));
    CHECK(!LND_ConfigGetKey(count));
    CHECK(!LND_ConfigGetKey(UINT32_MAX));
    CHECK(!LND_ConfigKeyGetName(nullptr));
    CHECK(LND_ConfigSet(nullptr, 0) == LND_ERR_INVALID_ARG);
    CHECK(LND_ConfigSet((LND_CONFIG_KEY *)((uintptr_t)LND_CFG_RUN_MODE + 1), 0) == LND_ERR_INVALID_ARG);
    for (uint32_t i = 0; i < count; i++) {
        LND_CONFIG_KEY *key = LND_ConfigGetKey(i);
        const char *name = LND_ConfigKeyGetName(key);
        CHECK(name && LND_ConfigFindKey(name) == key);
    }
#if !LND_MODULE_GRAPH
    CHECK(!LND_ConfigFindKey("graph.gain_ramp_frames"));
#endif
#if !LND_MODULE_DEVICES
    CHECK(!LND_ConfigFindKey("devices.auto_open"));
#endif
}

int main(void) {
    test_config_keys();
    CHECK(LND_RendererGetMemoryBytes(nullptr) == 0);
    int32_t last_format = LND_MODULE_PCM_FLOAT ? LND_FORMAT_F64 : LND_FORMAT_S32;
    for (int32_t format = LND_FORMAT_U8; format <= last_format; format++) {
        for (int32_t layout = 0; layout <= 1; layout++) test_native(format, layout);
    }
    test_exact();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
