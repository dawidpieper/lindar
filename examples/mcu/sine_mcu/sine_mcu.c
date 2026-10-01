#include "sine_mcu.h"
#include "sine_table.h"

static int16_t sine_sample(uint32_t phase) {
    uint32_t p = phase & 0x7fffffff;
    if (p > 0x40000000) p = 0x80000000 - p;
    uint32_t index = p >> 22;
    int32_t value = sine_quarter[index];
    if (index < 256) value += ((sine_quarter[index + 1] - value) * (int32_t)(p & 0x3fffff) + 0x200000) >> 22;
    return (int16_t)(phase & 0x80000000 ? -value : value);
}

static int64_t sine_render(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    sine_mcu *sine = user;
    if (pcm->format != LND_FORMAT_S16LE || pcm->channels != 1) return LND_ERR_FORMAT;
    size_t stride = pcm->stride_bytes ? pcm->stride_bytes : sizeof(int16_t);
    uint8_t *data = pcm->layout == LND_LAYOUT_PLANAR ? pcm->planes[0] : pcm->data;
    uint32_t phase = sine->phase;
    for (size_t f = 0; f < frames; f++) {
        uint16_t sample = (uint16_t)sine_sample(phase);
        size_t at = (offset + f) * stride;
        data[at] = (uint8_t)sample;
        data[at + 1] = (uint8_t)(sample >> 8);
        phase += sine->step;
    }
    sine->phase = phase;
    return (int64_t)frames;
}

int32_t sine_mcu_configure(void) {
    int32_t r = LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (r == LND_OK) r = LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16LE);
    if (r == LND_OK) r = LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, LND_LAYOUT_INTERLEAVED);
    return r;
}

static int32_t sine_seek(void *user, uint64_t frame) {
    sine_mcu *sine = user;
    sine->phase = (uint32_t)frame * sine->step;
    return LND_OK;
}

static size_t aligned_size(size_t bytes) {
    size_t alignment = alignof(max_align_t);
    return (bytes + alignment - 1) / alignment * alignment;
}

size_t sine_mcu_memory_size(void) {
    LND_RENDERER_CONFIG config = {.render = sine_render, .channels = 1, .sample_rate_hz = 48000, .block_frames = 128};
    LND_SOURCE_CONFIG source = {.read = sine_render, .channels = 1, .sample_rate_hz = 48000};
    return aligned_size(LND_SourceGetMemoryBytes(&source)) + aligned_size(LND_SoundGetMemoryBytes()) + LND_RendererGetMemoryBytes(&config);
}

int32_t sine_mcu_init(sine_mcu *sine, void *memory, size_t bytes, uint32_t sample_rate_hz, uint32_t frequency) {
    if (!sine || !sample_rate_hz || !frequency || frequency > sample_rate_hz / 2) return LND_ERR_INVALID_ARG;
    if (LND_ConfigGet(LND_CFG_RUN_MODE) != LND_MODE_SINGLE_THREADED || LND_ConfigGet(LND_CFG_INTERNAL_FORMAT) != LND_FORMAT_S16LE) return LND_ERR_STATE;
    int32_t r = LND_LibraryInit();
    if (r != LND_OK) return r;
    if (!memory || bytes < sine_mcu_memory_size() || (uintptr_t)memory % alignof(max_align_t)) return LND_ERR_INVALID_ARG;
    *sine = (sine_mcu){.step = (uint32_t)((((uint64_t)frequency << 32) + sample_rate_hz / 2) / sample_rate_hz)};
    LND_SOURCE_CONFIG source = {.read = sine_render, .seek = sine_seek, .user = sine, .channels = 1, .sample_rate_hz = sample_rate_hz};
    size_t source_bytes = aligned_size(LND_SourceGetMemoryBytes(&source));
    size_t sound_bytes = aligned_size(LND_SoundGetMemoryBytes());
    sine->source = LND_SourceInit(memory, source_bytes, &source);
    if (!sine->source) return LND_ErrorGetLast();
    sine->sound = LND_SoundInit((uint8_t *)memory + source_bytes, sound_bytes, sine->source);
    if (sine->sound) {
        LND_RENDERER_CONFIG config = {.render = LND_SoundRenderPcm, .user = sine->sound, .channels = 1, .sample_rate_hz = sample_rate_hz, .block_frames = 128};
        sine->renderer = LND_RendererInit((uint8_t *)memory + source_bytes + sound_bytes, bytes - source_bytes - sound_bytes, &config);
    }
    if (sine->renderer) return LND_SoundPlay(sine->sound);
    r = LND_ErrorGetLast();
    LND_SourceFree(sine->source);
    sine->source = nullptr;
    sine->sound = nullptr;
    return r;
}

int32_t sine_mcu_fill(sine_mcu *sine, int16_t *buffer, size_t frames) {
    if (!sine) return LND_ERR_INVALID_ARG;
    LND_PCM output = {.data = buffer, .frames = frames, .channels = 1, .format = LND_FORMAT_S16LE};
    return LND_RendererFillPcm(sine->renderer, &output, 0, frames);
}

int32_t sine_mcu_free(sine_mcu *sine) {
    if (!sine) return LND_ERR_INVALID_ARG;
    int32_t r = LND_RendererFree(sine->renderer);
    if (r == LND_OK) {
        sine->renderer = nullptr;
        r = LND_SourceFree(sine->source);
        if (r == LND_OK) {
            sine->source = nullptr;
            sine->sound = nullptr;
        }
    }
    return r;
}
