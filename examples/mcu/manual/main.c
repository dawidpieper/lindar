#include "lindar.h"

#include <stdio.h>

static int64_t render(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    uint32_t *phase = user;
    size_t stride = pcm->stride_bytes ? pcm->stride_bytes : 2 * (pcm->layout == LND_LAYOUT_PLANAR ? 1 : pcm->channels);
    for (size_t f = 0; f < frames; f++) {
        uint16_t value = (uint16_t)*phase;
        *phase += 601;
        for (uint32_t c = 0; c < pcm->channels; c++) {
            uint8_t *p = pcm->layout == LND_LAYOUT_PLANAR ? (uint8_t *)pcm->planes[c] : (uint8_t *)pcm->data + c * 2;
            p += (offset + f) * stride;
            p[0] = (uint8_t)value;
            p[1] = (uint8_t)(value >> 8);
        }
    }
    return (int64_t)frames;
}

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16LE);
    LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, LND_LAYOUT_PLANAR);
    if (LND_LibraryInit() != LND_OK) return 1;
    uint32_t phase = 0;
    LND_RENDERER_CONFIG config = {.render = render, .user = &phase, .channels = 2, .sample_rate_hz = 48000, .block_frames = 64};
    size_t bytes = LND_RendererGetMemoryBytes(&config);
    max_align_t memory[(bytes + sizeof(max_align_t) - 1) / sizeof(max_align_t)];
    LND_RENDERER *renderer = LND_RendererInit(memory, sizeof memory, &config);
    uint8_t samples[512 * 2 * 2];
    LND_PCM output = {.data = samples, .frames = 512, .channels = 2, .format = LND_FORMAT_S16LE};
    int32_t result = renderer ? LND_RendererFillPcm(renderer, &output, 0, 512) : LND_ErrorGetLast();
    if (result == LND_OK) printf("512 frames, 48000 Hz, 2 channels, %zu bytes of renderer memory\n", bytes);
    LND_LibraryFree();
    return result == LND_OK ? 0 : 1;
}
