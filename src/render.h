#pragma once

#include "lindar.h"
#include "spinlock.h"

struct LND_RENDERER {
    LND_RENDERER_CONFIG config;
    LND_PCM stage;
    lnd_spinlock lock;
    bool owned;
    int32_t pending_error;
    struct LND_RENDERER *next;
};

LND_RENDERER *lnd_renderer_create(const LND_RENDERER_CONFIG *config);
void lnd_renderers_free_all(void);

bool lnd_renderers_overlap(const void *memory, size_t bytes);
int32_t lnd_renderer_free(LND_RENDERER *renderer);
int64_t lnd_renderer_read(LND_RENDERER *renderer, const LND_PCM *pcm, size_t offset, size_t frames, LND_RENDER_PROC render);
