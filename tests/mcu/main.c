#include "lindar.h"
#include "lindar_queue.h"
#include "platform.h"
#include <string.h>

static volatile uint32_t initialized = UINT32_C(0x12345678), zeroed;
static unsigned checks;
alignas(max_align_t) static uint8_t source_memory[768], sound_memory[128], render_memory[768];
#define CHECK(value) do { checks++; if (!(value)) { qemu_value("LND_FAIL", __LINE__); qemu_exit(1); } } while (0)

static uint32_t checksum(const int16_t *pcm, size_t count) {
    uint32_t result = UINT32_C(2166136261);
    for (size_t i = 0; i < count; i++) result = (result ^ (uint16_t)pcm[i]) * UINT32_C(16777619);
    return result;
}

static void configure(int32_t layout) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16LE) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, (uint64_t)layout) == LND_OK);
    CHECK(LND_ConfigFindKey("graph.gain_ramp_frames") == nullptr);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, 2) == LND_ERR_INVALID_ARG);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, (uint64_t)layout) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
}

static void pcm_test(void) {
    uint8_t input[] = {0, 128, 64, 192, 255, 0};
    int16_t left[5], right[5];
    for (unsigned i = 0; i < 5; i++) left[i] = right[i] = 12345;
    void *planes[] = {left, right};
    LND_PCM in = {.data = input, .frames = 3, .channels = 2, .format = LND_FORMAT_U8};
    LND_PCM out = {.planes = planes, .frames = 5, .channels = 2, .format = LND_FORMAT_S16LE, .layout = LND_LAYOUT_PLANAR};
    CHECK(LND_PcmConvert(&out, 1, &in, 0, 3) == LND_OK);
    CHECK(left[0] == 12345 && left[4] == 12345 && right[0] == 12345 && right[4] == 12345);
    CHECK(left[1] == -32768 && left[2] == -16384 && left[3] == 32512);
    CHECK(right[1] == 0 && right[2] == 16384 && right[3] == -32768);
    CHECK(LND_PcmConvert(&out, 3, &in, 0, 3) == LND_ERR_INVALID_ARG);
    CHECK(LND_PcmSilence(&out, 2, 1) == LND_OK && left[2] == 0 && right[2] == 0);
    qemu_value("LND_CASE pcm", checksum(left, 5) ^ checksum(right, 5));
    const int16_t expected[] = {-32768, -16384, 0, 16384, 32767};
    int32_t wide[] = {INT32_MIN, -INT32_C(1073741824), 0, INT32_C(1073741824), INT32_MAX};
    uint8_t packed[] = {0,0,128, 0,0,192, 0,0,0, 0,0,64, 255,255,127};
    in = (LND_PCM){.data = wide, .frames = 5, .channels = 1, .format = LND_FORMAT_S32LE};
    out = (LND_PCM){.data = left, .frames = 5, .channels = 1, .format = LND_FORMAT_S16LE};
    CHECK(LND_PcmConvert(&out, 0, &in, 0, 5) == LND_OK);
    CHECK(!memcmp(left, expected, sizeof left));
    in.data = packed;
    in.format = LND_FORMAT_S24LE;
    CHECK(LND_PcmConvert(&out, 0, &in, 0, 5) == LND_OK);
    CHECK(!memcmp(left, expected, sizeof left));
    qemu_value("LND_CASE pcm_wide", checksum(left, 5));
}

static LND_RENDERER *renderer(LND_SOURCE *source, LND_SOUND **sound) {
    *sound = LND_SoundInit(sound_memory, sizeof sound_memory, source);
    CHECK(*sound != nullptr);
    LND_RENDERER_CONFIG config = {.render = LND_SoundRenderPcm, .user = *sound, .channels = 1, .sample_rate_hz = 8000, .block_frames = 8};
    size_t bytes = LND_RendererGetMemoryBytes(&config);
    CHECK(bytes && bytes <= sizeof render_memory - 8);
    memset(render_memory, 0xa5, sizeof render_memory);
    CHECK(LND_RendererInit(render_memory, bytes - 1, &config) == nullptr);
    LND_RENDERER *r = LND_RendererInit(render_memory, bytes, &config);
    CHECK(r != nullptr);
    CHECK(LND_SourceFree(source) == LND_ERR_BUSY);
    return r;
}

static void transport_test(int32_t layout) {
    configure(layout);
    int16_t input[] = {-12000, 4000, 16000}, output[7];
    LND_PCM in = {.data = input, .frames = 3, .channels = 1, .format = LND_FORMAT_S16LE};
    LND_SOURCE_CONFIG config = {.pcm = &in, .channels = 1, .sample_rate_hz = 8000};
    size_t bytes = LND_SourceGetMemoryBytes(&config);
    CHECK(bytes && bytes < sizeof source_memory - 8);
    CHECK(LND_SourceCreate(&config) == nullptr);
    CHECK(LND_SourceInit(source_memory, bytes - 1, &config) == nullptr);
    memset(source_memory, 0xa5, sizeof source_memory);
    LND_SOURCE *source = LND_SourceInit(source_memory, bytes, &config);
    CHECK(source != nullptr);
    LND_SOUND *sound;
    LND_RENDERER *r = renderer(source, &sound);
    LND_PCM out = {.data = output, .frames = 7, .channels = 1, .format = LND_FORMAT_S16LE};
    for (unsigned i = 0; i < 7; i++) output[i] = 12345;
    CHECK(LND_RendererFillPcm(r, &out, 1, 5) == LND_OK);
    CHECK(output[0] == 12345 && output[6] == 12345 && output[1] == 0 && output[5] == 0);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_RendererFillPcm(r, &out, 1, 1) == LND_OK && output[1] == -12000);
    CHECK(LND_SoundSetPause(sound, true) == LND_OK);
    CHECK(LND_RendererFillPcm(r, &out, 1, 2) == LND_OK && output[1] == 0 && LND_SoundGetPositionFrames(sound) == 1);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundSeekFrames(sound, 2) == LND_OK);
    CHECK(LND_SoundSetGainQ16(sound, 32768) == LND_OK && LND_SoundSetPause(sound, false) == LND_OK);
    CHECK(LND_RendererFillPcm(r, &out, 1, 5) == LND_OK);
    const int16_t expected[] = {12345, 8000, -6000, 2000, 8000, -6000, 12345};
    CHECK(!memcmp(output, expected, sizeof output));
    qemu_value(layout == LND_LAYOUT_PLANAR ? "LND_CASE transport_planar" : "LND_CASE transport", checksum(output, 7));
    CHECK(LND_SoundStop(sound) == LND_OK && LND_SoundGetPositionFrames(sound) == 0);
    CHECK(LND_SoundSetLoop(sound, false) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_RendererFillPcm(r, &out, 1, 5) == LND_OK && output[4] == 0 && output[5] == 0);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_STOPPED && LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    for (size_t i = bytes; i < sizeof source_memory; i++) CHECK(source_memory[i] == 0xa5);
    for (size_t i = sizeof render_memory - 8; i < sizeof render_memory; i++) CHECK(render_memory[i] == 0xa5);
    CHECK(LND_RendererFree(r) == LND_OK && LND_SoundFree(sound) == LND_OK && LND_SourceFree(source) == LND_OK);
    LND_LibraryFree();
}

static void queue_test(void) {
    configure(LND_LAYOUT_INTERLEAVED);
    size_t bytes = LND_QueueGetMemoryBytes(1, 8000, 8);
    CHECK(bytes && bytes <= sizeof source_memory);
    LND_SOURCE *source = LND_QueueInit(source_memory, bytes, 1, 8000, 8);
    CHECK(source != nullptr);
    LND_SOUND *sound;
    LND_RENDERER *r = renderer(source, &sound);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    int16_t input[] = {100, 200, 300, 400, 500, 600, 700, 800}, output[8];
    LND_PCM in = {.data = input, .frames = 8, .channels = 1, .format = LND_FORMAT_S16LE};
    LND_PCM out = {.data = output, .frames = 8, .channels = 1, .format = LND_FORMAT_S16LE};
    CHECK(LND_RendererFillPcm(r, &out, 0, 8) == LND_OK && LND_SoundGetState(sound) == LND_SOUND_STALLED);
    CHECK(LND_QueueWritePcm(source, &in, 0, 8) == 8 && LND_QueueWritePcm(source, &in, 0, 1) == 0);
    CHECK(LND_RendererFillPcm(r, &out, 0, 5) == LND_OK && output[0] == 100 && output[4] == 500);
    CHECK(LND_QueueWritePcm(source, &in, 0, 5) == 5);
    CHECK(LND_RendererFillPcm(r, &out, 0, 8) == LND_OK);
    const int16_t expected[] = {600, 700, 800, 100, 200, 300, 400, 500};
    CHECK(!memcmp(output, expected, sizeof output));
    qemu_value("LND_CASE queue", checksum(output, 8));
    CHECK(LND_RendererFillPcm(r, &out, 0, 8) == LND_OK && LND_SourceGetStatus(source) == LND_SOURCE_WAITING);
    CHECK(LND_SourceEnd(source) == LND_OK);
    CHECK(LND_RendererFillPcm(r, &out, 0, 8) == LND_OK && LND_SoundGetState(sound) == LND_SOUND_STOPPED);
    CHECK(LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    CHECK(LND_RendererFree(r) == LND_OK && LND_SoundFree(sound) == LND_OK && LND_SourceFree(source) == LND_OK);
    LND_LibraryFree();
}

int main(void) {
    CHECK(initialized == UINT32_C(0x12345678) && zeroed == 0);
    pcm_test();
    transport_test(LND_LAYOUT_INTERLEAVED);
    transport_test(LND_LAYOUT_PLANAR);
    queue_test();
    qemu_value("LND_CHECKS", checks);
    qemu_exit(0);
}
