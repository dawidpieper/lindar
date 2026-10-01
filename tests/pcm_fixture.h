#pragma once

#include "stream_test.h"

typedef struct pcm_fixture {
    LND_PCM pcm;
    void *planes[32];
    unsigned char *memory[32];
    size_t bytes;
    uint32_t allocations;
} pcm_fixture;

static void fixture_init(pcm_fixture *f, uint32_t channels, size_t frames, int32_t format, int32_t layout, size_t padding) {
    memset(f, 0, sizeof *f);
    size_t sample = LND_PcmGetSampleBytes(format);
    f->pcm = (LND_PCM){
        .planes = f->planes, .frames = frames, .stride_bytes = sample * (layout ? 1 : channels) + padding, .channels = channels, .format = format, .layout = layout};
    f->bytes = frames * f->pcm.stride_bytes + sample * channels + 8;
    f->allocations = layout ? channels : 1;
    for (uint32_t c = 0; c < f->allocations; c++) {
        f->memory[c] = malloc(f->bytes);
        CHECK(f->memory[c]);
        memset(f->memory[c], 0xa7, f->bytes);
        f->planes[c] = f->memory[c] + 1;
    }
    if (!layout) f->pcm.data = f->memory[0] + 1;
}

static void fixture_clear(pcm_fixture *f) {
    for (uint32_t c = 0; c < f->allocations; c++)
        memset(f->memory[c], 0xa7, f->bytes);
}

static void fixture_guard(const pcm_fixture *f, size_t offset, size_t frames) {
    size_t width = LND_PcmGetSampleBytes(f->pcm.format) * (f->pcm.layout ? 1 : f->pcm.channels);
    for (uint32_t c = 0; c < f->allocations; c++)
        for (size_t byte = 0; byte < f->bytes; byte++) {
            size_t at = byte ? (byte - 1) / f->pcm.stride_bytes : SIZE_MAX;
            bool sample = byte && at >= offset && at - offset < frames && (byte - 1) % f->pcm.stride_bytes < width;
            if (!sample) CHECK(f->memory[c][byte] == 0xa7);
        }
}

static void fixture_free(pcm_fixture *f) {
    for (uint32_t c = 0; c < f->allocations; c++)
        free(f->memory[c]);
}
