#include "render.h"
#include "alloc.h"
#include "config.h"
#include "context.h"
#include "error.h"
#include "pcm.h"
#include "native.h"

#include <string.h>

static LND_RENDERER *lnd_renderers;

size_t LND_RendererGetMemoryBytes(const LND_RENDERER_CONFIG *config) {
    if (!config || !config->render || !config->channels || config->channels > LND_MAX_CHANNELS || !config->sample_rate_hz || !config->block_frames ||
        config->block_frames > UINT32_MAX / config->channels)
        return 0;
    size_t width = LND_PcmGetSampleBytes((int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT)) * config->channels;
    size_t base = sizeof(LND_RENDERER) + config->channels * sizeof(void *);
    if (config->block_frames > (SIZE_MAX - base) / width) return 0;
    return base + (size_t)config->block_frames * width;
}

bool lnd_renderers_overlap(const void *memory, size_t bytes) {
    uintptr_t begin = (uintptr_t)memory;
    if (begin > UINTPTR_MAX - bytes) return true;
    for (LND_RENDERER *p = lnd_renderers; p; p = p->next) {
        size_t extent = sizeof *p + p->config.channels * sizeof(void *) + p->stage.frames * p->stage.channels * LND_PcmGetSampleBytes(p->stage.format);
        if (begin < (uintptr_t)p + extent && (uintptr_t)p < begin + bytes) return true;
    }
    return false;
}

static LND_RENDERER *lnd_renderer_init(void *memory, size_t bytes, const LND_RENDERER_CONFIG *config) {
    if (!lnd_ctx.initialized) return lnd_error_null(LND_ERR_STATE);
    size_t need = LND_RendererGetMemoryBytes(config);
    if (!need || !memory || bytes < need || (uintptr_t)memory % alignof(LND_RENDERER)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (lnd_renderers_overlap(memory, need) || lnd_playback_overlaps(memory, need)) return lnd_error_null(LND_ERR_BUSY);
    LND_RENDERER_CONFIG saved = *config;
    if (saved.render == LND_SoundRenderPcm) {
        int32_t result = lnd_sound_ref(saved.user);
        if (result != LND_OK) return lnd_error_null(result);
    }
    if (saved.render == lnd_native_sound_render_pcm) {
        int32_t result = lnd_native_sound_ref(saved.user);
        if (result != LND_OK) return lnd_error_null(result);
    }
    LND_RENDERER *r = memory;
    memset(r, 0, sizeof *r);
    r->config = saved;
    void **planes = (void **)(r + 1);
    r->stage = (LND_PCM){.data = planes + saved.channels,
                         .planes = planes,
                         .frames = saved.block_frames,
                         .channels = saved.channels,
                         .format = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT),
                         .layout = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_LAYOUT)};
    size_t plane_bytes = r->stage.frames * LND_PcmGetSampleBytes(r->stage.format);
    for (uint32_t c = 0; c < saved.channels; c++)
        planes[c] = (uint8_t *)r->stage.data + c * plane_bytes;
    r->next = lnd_renderers;
    lnd_renderers = r;
    return r;
}

LND_RENDERER *LND_RendererInit(void *memory, size_t bytes, const LND_RENDERER_CONFIG *config) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    LND_RENDERER *r = lnd_renderer_init(memory, bytes, config);
    lnd_context_unlock();
    return r;
}

LND_RENDERER *lnd_renderer_create(const LND_RENDERER_CONFIG *config) {
    size_t bytes = LND_RendererGetMemoryBytes(config);
    if (!bytes) return lnd_error_null(LND_ERR_INVALID_ARG);
    void *memory = lnd_alloc(bytes);
    if (!memory) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    LND_RENDERER *r = lnd_renderer_init(memory, bytes, config);
    if (!r)
        lnd_free(memory);
    else
        r->owned = true;
    return r;
}

LND_RENDERER *LND_RendererCreateProc(const LND_RENDERER_CONFIG *config) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    LND_RENDERER *r = lnd_renderer_create(config);
    lnd_context_unlock();
    return r;
}

static void lnd_renderer_destroy(LND_RENDERER *r) {
    lnd_spinlock_lock(&r->lock);
    if (r->config.render == LND_SoundRenderPcm) lnd_sound_unref(r->config.user);
    if (r->config.render == lnd_native_sound_render_pcm) lnd_native_sound_unref(r->config.user);
    if (r->config.close) {
        lnd_callback_enter();
        r->config.close(r->config.user);
        lnd_callback_leave();
    }
    bool owned = r->owned;
    r->config.render = nullptr;
    lnd_spinlock_unlock(&r->lock);
    if (owned) lnd_free(r);
}

void lnd_renderers_free_all(void) {
    while (lnd_renderers) {
        LND_RENDERER *r = lnd_renderers;
        lnd_renderers = r->next;
        lnd_renderer_destroy(r);
    }
}

int32_t lnd_renderer_free(LND_RENDERER *renderer) {
    for (LND_RENDERER **p = &lnd_renderers; *p; p = &(*p)->next) {
        if (*p != renderer) continue;
        *p = renderer->next;
        lnd_renderer_destroy(renderer);
        return LND_OK;
    }
    return LND_ERR_INVALID_ARG;
}

int32_t LND_RendererFree(LND_RENDERER *renderer) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = lnd_renderer_free(renderer);
    lnd_context_unlock();
    return lnd_error(result);
}

uint32_t LND_RendererGetSampleRateHz(const LND_RENDERER *r) { return r ? r->config.sample_rate_hz : 0; }
uint32_t LND_RendererGetChannels(const LND_RENDERER *r) { return r ? r->config.channels : 0; }
int32_t LND_RendererGetFormat(const LND_RENDERER *r) { return r ? r->stage.format : LND_FORMAT_NONE; }
int32_t LND_RendererGetLayout(const LND_RENDERER *r) { return r ? r->stage.layout : LND_LAYOUT_INTERLEAVED; }

static int64_t lnd_renderer_read_result(LND_RENDERER *r, const LND_PCM *pcm, size_t offset, size_t frames, LND_RENDER_PROC render, bool fill) {
    if (!r || !lnd_pcm_writable(pcm, offset, frames) || frames > (size_t)INT64_MAX || pcm->channels != r->config.channels)
        return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_spinlock_try(&r->lock)) return lnd_error(LND_ERR_BUSY);
    if (!r->config.render) {
        lnd_spinlock_unlock(&r->lock);
        return lnd_error(LND_ERR_STATE);
    }
    if (frames && r->pending_error) {
        int32_t error = r->pending_error;
        r->pending_error = LND_OK;
        LND_PcmSilence(pcm, offset, frames);
        lnd_spinlock_unlock(&r->lock);
        return lnd_error(error);
    }
    int32_t result = LND_OK;
    size_t total = 0;
    if (!render) render = r->config.render;
    bool direct = pcm->format == r->stage.format && pcm->layout == r->stage.layout;
    for (size_t done = 0; done < frames;) {
        size_t n = LND_MIN(frames - done, r->config.block_frames);
        const LND_PCM *target = direct ? pcm : &r->stage;
        size_t at = direct ? offset + done : 0;
        lnd_callback_enter();
        int64_t got = render(r->config.user, target, at, n);
        lnd_callback_leave();
        if (got < 0 || (uint64_t)got > n) {
            result = got >= LND_ERR_CYCLE && got < 0 ? (int32_t)got : LND_ERR_IO;
            LND_PcmSilence(pcm, offset + done, frames - done);
            break;
        }
        if ((size_t)got < n) LND_PcmSilence(target, at + (size_t)got, n - (size_t)got);
        if (!direct) result = LND_PcmConvert(pcm, offset + done, target, at, n);
        if (result != LND_OK) {
            LND_PcmSilence(pcm, offset + done, frames - done);
            break;
        }
        total += (size_t)got;
        done += n;
        if ((size_t)got < n) {
            LND_PcmSilence(pcm, offset + done, frames - done);
            break;
        }
    }
    if (total && result < 0 && !fill) r->pending_error = result;
    lnd_spinlock_unlock(&r->lock);
    return result == LND_OK || (total && !fill) ? (int64_t)total : lnd_error(result);
}

int64_t lnd_renderer_read(LND_RENDERER *r, const LND_PCM *pcm, size_t offset, size_t frames, LND_RENDER_PROC render) {
    return lnd_renderer_read_result(r, pcm, offset, frames, render, false);
}

int64_t LND_RendererReadPcm(LND_RENDERER *r, const LND_PCM *pcm, size_t offset, size_t frames) { return lnd_renderer_read(r, pcm, offset, frames, nullptr); }

int32_t LND_RendererFillPcm(LND_RENDERER *r, const LND_PCM *pcm, size_t offset, size_t frames) {
    int64_t result = lnd_renderer_read_result(r, pcm, offset, frames, nullptr, true);
    return lnd_error(result < 0 ? (int32_t)result : LND_OK);
}
