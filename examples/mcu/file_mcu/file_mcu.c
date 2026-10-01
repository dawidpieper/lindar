#include "file_mcu.h"

static void *file_mcu_allocate(void *user, size_t bytes) {
    file_mcu *p = user;
    p->allocations++;
    size_t at = (p->used + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1);
    if (at > p->capacity || bytes > p->capacity - at) return nullptr;
    p->used = at + bytes;
    return p->memory + at;
}

static void file_mcu_release(void *user, void *memory) {
    (void)user;
    (void)memory;
}

int32_t file_mcu_configure(file_mcu *p, void *memory, size_t bytes) {
    if (!p || !memory || (uintptr_t)memory % alignof(max_align_t)) return LND_ERR_INVALID_ARG;
    *p = (file_mcu){.memory = memory, .capacity = bytes};
    int32_t r = LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (r == LND_OK) r = LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16);
    if (r == LND_OK) r = LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, LND_LAYOUT_INTERLEAVED);
    if (r == LND_OK) r = LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = file_mcu_allocate, .free = file_mcu_release, .user = p});
    return r;
}

int32_t file_mcu_init(file_mcu *p, const void *file, size_t bytes) {
    int32_t r = LND_LibraryInit();
    if (r != LND_OK) return r;
    LND_ENCODED_SOURCE_OPTIONS options = {.block_frames = 128};
    p->source = LND_SourceCreateEncodedMemory(file, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
    if (!p->source) return LND_ErrorGetLast();
    p->sound = LND_SourceEnsureSound(p->source, nullptr);
    if (!p->sound) return LND_ErrorGetLast();
    LND_RENDERER_CONFIG config = {
        .render = LND_SoundRenderPcm, .user = p->sound, .channels = LND_SourceGetChannels(p->source), .sample_rate_hz = LND_SourceGetSampleRateHz(p->source), .block_frames = 128};
    p->renderer = LND_RendererCreateProc(&config);
    return p->renderer ? LND_SoundPlay(p->sound) : LND_ErrorGetLast();
}

int32_t file_mcu_fill(file_mcu *p, int16_t *buffer, size_t frames) {
    LND_PCM pcm = {.data = buffer, .frames = frames, .channels = LND_SourceGetChannels(p->source), .format = LND_FORMAT_S16};
    return LND_RendererFillPcm(p->renderer, &pcm, 0, frames);
}

void file_mcu_free(file_mcu *p) {
    if (p->renderer) LND_RendererFree(p->renderer);
    if (p->source) LND_SourceFree(p->source);
    p->renderer = nullptr;
    p->source = nullptr;
    p->sound = nullptr;
}
