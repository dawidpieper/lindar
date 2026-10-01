#include "lindar.h"
#include "lnd_modules.h"
#if LND_MODULE_PCM_FLOAT
#include "lindar_pcm_float.h"
#endif

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

static void begin(int32_t format, int32_t layout) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, format) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, layout) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .free = release}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
}

static void test_pcm(int32_t format, int32_t layout) {
    begin(format, layout);
    int32_t samples[] = {-1073741824, 1073741824, -536870912, 536870912, 536870912, -536870912};
    LND_PCM input = {.data = samples, .frames = 3, .channels = 2, .format = LND_FORMAT_S32};
    LND_SOURCE_CONFIG config = {.pcm = &input, .channels = 2, .sample_rate_hz = 16000, .block_frames = 2};
    alignas(max_align_t) unsigned char source_memory[2048], sound_memory[256], render_memory[2048];
    size_t bytes = LND_SourceGetMemoryBytes(&config);
    CHECK(bytes && bytes <= sizeof source_memory);
    CHECK(LND_SoundGetMemoryBytes() <= sizeof sound_memory);
    deny_alloc = true;
    unsigned before = allocations;
    CHECK(!LND_SourceInit(source_memory, bytes - 1, &config));
    CHECK(!LND_SourceInit(source_memory + 1, sizeof source_memory - 1, &config));
    LND_SOURCE *source = LND_SourceInit(source_memory, sizeof source_memory, &config);
    CHECK(source != nullptr);
    CHECK(!LND_SourceInit(source_memory, sizeof source_memory, &config));
    CHECK(LND_ErrorGetLast() == LND_ERR_BUSY);
    CHECK(LND_SourceGetSampleRateHz(source) == 16000 && LND_SourceGetChannels(source) == 2);
    CHECK(LND_SourceGetLengthFrames(source) == 3 && LND_SourceGetFormat(source) == format);
    CHECK(!LND_SoundInit(source_memory, sizeof source_memory, source));
    CHECK(!LND_SoundInit(sound_memory + 1, sizeof sound_memory - 1, source));
    LND_SOUND *sound = LND_SoundInit(sound_memory, sizeof sound_memory, source);
    CHECK(sound != nullptr);
    CHECK(LND_SourceEnsureSound(source, nullptr) == sound);
    CHECK(LND_SoundGetSource(sound) == source && LND_SoundGetSampleRateHz(sound) == 16000 && LND_SoundGetLengthFrames(sound) == 3);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_STOPPED && LND_SoundGetGainQ16(sound) == 65536);
    CHECK(LND_SoundSetPause(sound, false) == LND_OK && LND_SoundGetState(sound) == LND_SOUND_STOPPED);
    LND_RENDERER_CONFIG rc = {.render = LND_SoundRenderPcm, .user = sound, .sample_rate_hz = 16000, .channels = 2, .block_frames = 5};
    LND_RENDERER *renderer = LND_RendererInit(render_memory, sizeof render_memory, &rc);
    CHECK(renderer != nullptr);
    CHECK(LND_SourceFree(source) == LND_ERR_BUSY);
    CHECK(LND_SoundFree(sound) == LND_ERR_BUSY);
    int32_t out[22];
    LND_PCM output = {.data = out, .frames = 11, .channels = 2, .format = LND_FORMAT_S32};
    memset(out, 0x55, sizeof out);
    CHECK(LND_RendererFillPcm(renderer, &output, 1, 9) == LND_OK);
    for (size_t i = 2; i < 20; i++) CHECK(out[i] == 0);
    CHECK(out[0] == 0x55555555 && out[21] == 0x55555555);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &output, 1, 1) == LND_OK);
    CHECK(out[2] == samples[0] && out[3] == samples[1] && LND_SoundGetPositionFrames(sound) == 1);
    CHECK(LND_SoundSetPause(sound, true) == LND_OK && LND_SoundGetState(sound) == LND_SOUND_PAUSED);
    CHECK(LND_RendererFillPcm(renderer, &output, 1, 4) == LND_OK);
    CHECK(out[2] == 0 && LND_SoundGetPositionFrames(sound) == 1);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 4) == LND_OK);
    CHECK(out[0] == samples[2] && out[2] == samples[4] && out[4] == 0);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_STOPPED && LND_SoundGetPositionFrames(sound) == 3);
    CHECK(LND_SoundPlay(sound) == LND_OK && LND_SoundGetPositionFrames(sound) == 0);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundGetLoop(sound));
    CHECK(LND_RendererFillPcm(renderer, &output, 1, 9) == LND_OK);
    for (size_t f = 0; f < 9; f++)
        for (size_t c = 0; c < 2; c++) CHECK(out[2 + f * 2 + c] == samples[(f % 3) * 2 + c]);
    CHECK(LND_SoundGetPositionFrames(sound) == 0 && LND_SoundGetState(sound) == LND_SOUND_PLAYING);
    CHECK(LND_SoundSeekFrames(sound, 2) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 2) == LND_OK);
    CHECK(out[0] == samples[4] && out[2] == samples[0] && LND_SourceGetPositionFrames(source) == 1);
    CHECK(LND_SoundStop(sound) == LND_OK && LND_SourceGetPositionFrames(source) == 0);
    CHECK(LND_SoundSetGainQ16(sound, 32768) == LND_OK);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 3) == LND_OK);
    for (size_t i = 0; i < 6; i++) CHECK(out[i] == samples[i] / 2);
    CHECK(LND_SoundSetGainQ16(sound, 0) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 1) == LND_OK && out[0] == 0 && out[1] == 0);
    CHECK(LND_SoundSetGainQ16(sound, UINT32_MAX) == LND_OK);
    CHECK(LND_SoundSeekFrames(sound, 0) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 1) == LND_OK);
    CHECK(out[0] == INT32_MIN && out[1] > 2100000000);
    CHECK(LND_SoundSetLoop(sound, false) == LND_OK);
    CHECK(LND_SoundSeekFrames(sound, UINT64_MAX) == LND_OK && LND_SourceGetPositionFrames(source) == 3);
    CHECK(LND_SoundRenderPcm(sound, &output, 0, 1) == 0 && LND_SoundGetState(sound) == LND_SOUND_STOPPED);
    CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
    CHECK(LND_SourceReadPcm(source, &output, 0, 4) == 3);
    for (size_t i = 0; i < 6; i++) CHECK(out[i] == samples[i]);
    CHECK(LND_SourceReadPcm(source, &output, 0, 1) == 0);
    CHECK(LND_SourceReadPcm(source, &output, 12, 0) == LND_ERR_INVALID_ARG);
    CHECK(allocations == before);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(LND_SoundGetSource(sound) == nullptr && LND_SoundPlay(sound) == LND_ERR_STATE);
    CHECK(LND_SourceInit(source_memory, sizeof source_memory, &config) == source);
    CHECK(LND_SoundInit(sound_memory, sizeof sound_memory, source) == sound);
    CHECK(LND_SoundFree(sound) == LND_OK);
    LND_LibraryFree();
    CHECK(allocations == before);
    deny_alloc = false;
}

typedef struct reader {
    size_t position, length;
    unsigned reads, seeks, closed;
    int64_t error;
    bool reenter;
    LND_SOURCE *source;
    LND_SOUND *sound;
} reader;

static int64_t read_proc(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    reader *r = user;
    r->reads++;
    if (r->reenter) {
        CHECK(LND_SourceReadPcm(r->source, pcm, offset, frames) == LND_ERR_BUSY);
        CHECK(LND_SourceSeekFrames(r->source, 0) == LND_ERR_BUSY);
        CHECK(LND_SourceFree(r->source) == LND_ERR_BUSY);
        CHECK(LND_SoundRenderPcm(r->sound, pcm, offset, frames) == LND_ERR_BUSY);
        CHECK(LND_SoundStop(r->sound) == LND_ERR_BUSY);
        CHECK(LND_SoundFree(r->sound) == LND_ERR_BUSY);
        CHECK(LND_SourceGetPositionFrames(r->source) == r->position);
    }
    if (r->error) return r->error;
    size_t count = frames < r->length - r->position ? frames : r->length - r->position;
    CHECK(LND_PcmSilence(pcm, offset, count) == LND_OK);
    r->position += count;
    return (int64_t)count;
}
static int32_t seek_proc(void *user, uint64_t frame) {
    reader *r = user;
    r->seeks++;
    r->position = (size_t)frame;
    return LND_OK;
}
static void close_proc(void *user) { ((reader *)user)->closed++; }

static void test_callback(void) {
    begin(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    reader r = {.length = 2, .reenter = true};
    LND_SOURCE_CONFIG config = {.read = read_proc, .seek = seek_proc, .close = close_proc, .user = &r, .channels = 1, .sample_rate_hz = 8000, .block_frames = 4};
    r.source = LND_SourceCreate(&config);
    CHECK(r.source != nullptr);
    r.sound = LND_SourceEnsureSound(r.source, nullptr);
    CHECK(r.sound != nullptr);
    int16_t data[17];
    LND_PCM pcm = {.data = data, .frames = 17, .channels = 1, .format = LND_FORMAT_S16};
    deny_alloc = true;
    unsigned before = allocations;
    CHECK(LND_SoundSetLoop(r.sound, true) == LND_OK);
    CHECK(LND_SoundPlay(r.sound) == LND_OK);
    CHECK(LND_SoundRenderPcm(r.sound, &pcm, 0, 17) == 17);
    CHECK(r.position == 1 && r.seeks == 8);
    CHECK(LND_SoundSeekFrames(r.sound, 0) == LND_OK);
    r.length = 0;
    unsigned reads = r.reads;
    CHECK(LND_SoundRenderPcm(r.sound, &pcm, 0, 17) == 0);
    CHECK(r.reads == reads + 2 && LND_SoundGetState(r.sound) == LND_SOUND_STOPPED);
    r.error = 99;
    CHECK(LND_SoundPlay(r.sound) == LND_OK);
    CHECK(LND_SoundRenderPcm(r.sound, &pcm, 0, 17) == LND_ERR_IO);
    CHECK(LND_SourceGetPositionFrames(r.source) == 0);
    r.error = LND_ERR_FORMAT;
    CHECK(LND_SoundRenderPcm(r.sound, &pcm, 0, 1) == LND_ERR_FORMAT);
    CHECK(allocations == before);
    CHECK(LND_SourceFree(r.source) == LND_OK && r.closed == 1);
    deny_alloc = false;
    config.seek = nullptr;
    r.error = 0;
    r.length = 3;
    r.source = LND_SourceCreate(&config);
    r.sound = LND_SourceEnsureSound(r.source, nullptr);
    CHECK(r.source && r.sound);
    CHECK(LND_SoundSetLoop(r.sound, true) == LND_ERR_UNSUPPORTED);
    CHECK(LND_SourceSeekFrames(r.source, 0) == LND_ERR_UNSUPPORTED);
    CHECK(LND_SoundPlay(r.sound) == LND_OK);
    CHECK(LND_SoundRenderPcm(r.sound, &pcm, 0, 17) == 3);
    CHECK(LND_SoundPlay(r.sound) == LND_ERR_UNSUPPORTED);
    LND_LibraryFree();
    CHECK(r.closed == 2);
}

static void test_internal_gain(void) {
    begin(LND_FORMAT_U8, LND_LAYOUT_PLANAR);
    unsigned char samples[] = {131, 125};
    LND_PCM input = {.data = samples, .frames = 2, .channels = 1, .format = LND_FORMAT_U8};
    LND_SOURCE_CONFIG config = {.pcm = &input, .channels = 1, .sample_rate_hz = 8000, .block_frames = 1};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(source && sound);
    CHECK(LND_SoundSetGainQ16(sound, 32768) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    int32_t samples_out[2];
    LND_PCM output = {.data = samples_out, .frames = 2, .channels = 1, .format = LND_FORMAT_S32};
    CHECK(LND_SoundRenderPcm(sound, &output, 0, 2) == 2);
    CHECK(samples_out[0] == 33554432 && samples_out[1] == -33554432);
    LND_LibraryFree();
}

#if LND_MODULE_PCM_FLOAT
static void test_offline(void) {
    begin(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    int16_t data[] = {16384, -8192, 4096};
    LND_PCM pcm = {.data = data, .frames = 3, .channels = 1, .format = LND_FORMAT_S16};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 8000, .block_frames = 1};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(source && sound);
    CHECK(LND_SoundSetGainQ16(sound, 0) == LND_OK);
    float output[4] = {1, 1, 1, 1};
    unsigned before = allocations;
    deny_alloc = true;
    CHECK(LND_SoundReadF32(sound, output, 4) == 3);
    CHECK(output[0] == 0.5f && output[1] == -0.25f && output[2] == 0.125f && output[3] == 1.0f);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_STOPPED && LND_SoundGetPositionFrames(sound) == 3);
    CHECK(LND_SoundSeekFrames(sound, 1) == LND_OK);
    CHECK(LND_SoundPlay(sound) == LND_OK && LND_SoundSetPause(sound, true) == LND_OK);
    CHECK(LND_SoundReadF32(sound, output, 1) == 1 && output[0] == -0.25f);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_PAUSED && LND_SoundGetPositionFrames(sound) == 2);
    CHECK(allocations == before);
    deny_alloc = false;
    LND_LibraryFree();
}
#endif

int main(void) {
    CHECK(LND_SourceGetMemoryBytes(nullptr) == 0);
    int32_t last = LND_MODULE_PCM_FLOAT ? LND_FORMAT_F64 : LND_FORMAT_S32;
    for (int32_t format = LND_FORMAT_U8; format <= last; format++)
        for (int32_t layout = 0; layout <= 1; layout++) test_pcm(format, layout);
    test_callback();
    test_internal_gain();
#if LND_MODULE_PCM_FLOAT
    test_offline();
#endif
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
