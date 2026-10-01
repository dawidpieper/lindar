#include "native.h"
#include "notify.h"
#include "alloc.h"
#include "config.h"
#include "pcm.h"
#include "render.h"

#include <string.h>

static lnd_native_source *lnd_native_sources;

#if LND_MODULE_METADATA
bool lnd_native_source_registered(const LND_SOURCE *source) {
    for (lnd_native_source *s = lnd_native_sources; s; s = s->next)
        if (&s->base == source) return true;
    return false;
}
#endif

size_t LND_SourceGetMemoryBytes(const LND_SOURCE_CONFIG *config) {
    if (!config || !config->sample_rate_hz || !config->channels || config->channels > LND_MAX_CHANNELS || (config->read != nullptr) == (config->pcm != nullptr))
        return 0;
    if (config->flags & ~(uint32_t)LND_SOURCE_LIVE) return 0;
    if (config->pcm && (LND_PcmValidate(config->pcm) != LND_OK || config->pcm->channels != config->channels || config->length_frames)) return 0;
    size_t base = sizeof(lnd_native_source) + 2 * config->channels * sizeof(void *);
    size_t width = LND_PcmGetSampleBytes((int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT)) * config->channels;
    if (!width || config->block_frames > (SIZE_MAX - base) / width || config->block_frames > INT32_MAX) return 0;
    return base + (size_t)config->block_frames * width;
}

bool lnd_playback_overlaps(const void *memory, size_t bytes) {
    uintptr_t at = (uintptr_t)memory;
    if (at > UINTPTR_MAX - bytes) return true;
    for (lnd_native_source *s = lnd_native_sources; s; s = s->next) {
        if (at < (uintptr_t)s + s->bytes && (uintptr_t)s < at + bytes) return true;
        if (s->sound && at < (uintptr_t)s->sound + sizeof *s->sound && (uintptr_t)s->sound < at + bytes) return true;
    }
    return false;
}

static lnd_native_source *lnd_native_source_init(void *memory, size_t bytes, const LND_SOURCE_CONFIG *config) {
    if (!lnd_ctx.initialized) return lnd_error_null(LND_ERR_STATE);
    size_t need = LND_SourceGetMemoryBytes(config);
    if (!need || !memory || bytes < need || (uintptr_t)memory % alignof(lnd_native_source)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (lnd_playback_overlaps(memory, need) || lnd_renderers_overlap(memory, need)) return lnd_error_null(LND_ERR_BUSY);
    if (config->pcm && lnd_pcm_memory_overlaps(config->pcm, memory, need)) return lnd_error_null(LND_ERR_INVALID_ARG);
    LND_SOURCE_CONFIG saved = *config;
    LND_PCM input = config->pcm ? *config->pcm : (LND_PCM){0};
    void *input_planes[LND_MAX_CHANNELS];
    if (input.layout == LND_LAYOUT_PLANAR && input.frames) {
        for (uint32_t c = 0; c < saved.channels; c++)
            input_planes[c] = input.planes[c];
    }
    lnd_native_source *s = memory;
    memset(s, 0, sizeof *s);
    s->config = saved;
    s->config.length_known = saved.length_known || saved.length_frames != 0 || saved.pcm != nullptr;
    s->input = input;
    s->bytes = need;
    void **planes = (void **)(s + 1);
    s->stage = (LND_PCM){.data = planes + saved.channels * 2,
                         .planes = planes,
                         .frames = saved.block_frames,
                         .channels = saved.channels,
                         .format = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT),
                         .layout = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_LAYOUT)};
    for (uint32_t c = 0; c < saved.channels; c++) {
        planes[c] = (uint8_t *)s->stage.data + c * s->stage.frames * LND_PcmGetSampleBytes(s->stage.format);
        if (input.layout == LND_LAYOUT_PLANAR && input.frames) planes[c + saved.channels] = input_planes[c];
    }
    if (saved.pcm) {
        s->input.planes = planes + saved.channels;
        s->config.pcm = &s->input;
        s->config.length_frames = input.frames;
    }
    s->next = lnd_native_sources;
    lnd_native_sources = s;
    return s;
}

LND_SOURCE *LND_SourceInit(void *memory, size_t bytes, const LND_SOURCE_CONFIG *config) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_native_source *s = lnd_native_source_init(memory, bytes, config);
    lnd_context_unlock();
    return (LND_SOURCE *)s;
}

lnd_native_source *lnd_native_source_create(const LND_SOURCE_CONFIG *config) {
    size_t bytes = LND_SourceGetMemoryBytes(config);
    if (!bytes) return lnd_error_null(LND_ERR_INVALID_ARG);
    void *memory = lnd_alloc(bytes);
    lnd_native_source *s = memory ? lnd_native_source_init(memory, bytes, config) : lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    if (s)
        s->owned = true;
    else
        lnd_free(memory);
    return s;
}

LND_SOURCE *LND_SourceCreate(const LND_SOURCE_CONFIG *config) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_native_source *s = lnd_native_source_create(config);
    lnd_context_unlock();
    return (LND_SOURCE *)s;
}

static void lnd_native_source_destroy_locked(lnd_native_source *s) {
    if (s->sound) {
        lnd_notify_detach(&s->sound->notifications);
        s->sound->source = nullptr;
        if (s->sound->owned) lnd_free(s->sound);
        s->sound = nullptr;
    }
    if (s->config.close) {
        lnd_callback_enter();
        s->config.close(s->config.user);
        lnd_callback_leave();
    }
    s->config.read = nullptr;
#if LND_MODULE_METADATA
    LND_MetadataFree(s->base.metadata);
    s->base.metadata = nullptr;
#endif
    s->config.pcm = nullptr;
    lnd_spinlock_unlock(&s->lock);
    if (s->owned) lnd_free(s);
}

void lnd_playback_free_all(void) {
    while (lnd_native_sources) {
        lnd_native_source *s = lnd_native_sources;
        lnd_native_sources = s->next;
        lnd_spinlock_lock(&s->lock);
        lnd_native_source_destroy_locked(s);
    }
}

int32_t lnd_native_source_free(lnd_native_source *s) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    for (lnd_native_source **p = &lnd_native_sources; *p; p = &(*p)->next) {
        if (*p != s) continue;
        if ((s->sound && s->sound->references) || !lnd_spinlock_try(&s->lock))
            result = LND_ERR_BUSY;
        else {
            *p = s->next;
            lnd_native_source_destroy_locked(s);
            result = LND_OK;
        }
        break;
    }
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t lnd_native_source_get_format(const lnd_native_source *s) {
#if LND_MODULE_CODECS
    if (s && s->codec) return s->decoded_format;
#endif
    return s ? s->stage.format : LND_FORMAT_NONE;
}
uint32_t lnd_native_source_get_sample_rate_hz(const lnd_native_source *s) { return s ? s->config.sample_rate_hz : 0; }
uint32_t lnd_native_source_get_channels(const lnd_native_source *s) { return s ? s->config.channels : 0; }
uint64_t lnd_native_source_get_length_frames(const lnd_native_source *s) { return s ? s->config.length_frames : 0; }
uint64_t lnd_native_source_get_position_frames(const lnd_native_source *s) { return s ? lnd_load(&s->position) : 0; }

int32_t lnd_native_seek(lnd_native_source *s, uint64_t frame) {
    if (!s->config.pcm && !s->config.read) return LND_ERR_STATE;
    uint64_t length = lnd_native_source_limit(s);
    if (lnd_native_source_bounded(s) && frame > length) frame = length;
    int32_t result = LND_OK;
    if (!s->config.pcm) {
        if (!s->config.seek) return LND_ERR_UNSUPPORTED;
        lnd_callback_enter();
        result = s->config.seek(s->config.user, frame);
        lnd_callback_leave();
    }
    if (result == LND_OK) {
        lnd_store(&s->position, frame);
        lnd_store(&s->status, LND_SOURCE_READY);
        s->end_requested = false;
        s->deferred_error = LND_OK;
        if (s->sound) s->sound->ended = false;
    }
    return result;
}

int32_t lnd_native_source_seek_frames(lnd_native_source *s, uint64_t frame) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!s) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_spinlock_try(&s->lock)) return lnd_error(LND_ERR_BUSY);
    int32_t result = lnd_native_seek(s, frame);
#if LND_MODULE_GRAPH
    if (result == LND_OK) lnd_add(&s->revision, 1);
#endif
    lnd_spinlock_unlock(&s->lock);
    return lnd_error(result);
}

int64_t lnd_native_read(lnd_native_source *s, const LND_PCM *pcm, size_t offset, size_t frames) {
    if (!s->config.pcm && !s->config.read) return LND_ERR_STATE;
    if (!lnd_pcm_writable(pcm, offset, frames) || pcm->channels != s->config.channels || frames > (size_t)INT64_MAX) return LND_ERR_INVALID_ARG;
    if (!frames || lnd_load_relaxed(&s->status) == LND_SOURCE_EOF) return 0;
    if (s->deferred_error) {
        int32_t error = s->deferred_error;
        s->deferred_error = LND_OK;
        return error;
    }
#if !LND_MODULE_PCM_FLOAT
    if ((pcm->format > LND_FORMAT_S32 || s->stage.format > LND_FORMAT_S32) && pcm->format != s->stage.format) return LND_ERR_UNSUPPORTED;
#endif
    bool direct = pcm->format == s->stage.format && pcm->layout == s->stage.layout;
    if (!direct && !s->stage.frames) return LND_ERR_UNSUPPORTED;
    uint64_t length = lnd_native_source_limit(s);
    size_t done = 0;
    while (done < frames) {
        uint64_t position = lnd_load_relaxed(&s->position);
        size_t count = LND_MIN(frames - done, s->stage.frames ? s->stage.frames : (size_t)INT32_MAX);
        count = (size_t)LND_MIN(count, UINT64_MAX - position);
        if (lnd_native_source_bounded(s)) count = (size_t)LND_MIN(count, length - LND_MIN(length, position));
        if (!count) {
            lnd_store(&s->status, LND_SOURCE_EOF);
            break;
        }
        const LND_PCM *target = direct ? pcm : &s->stage;
        size_t at = direct ? offset + done : 0;
        int64_t got;
        if (s->config.pcm) {
            int32_t r = LND_PcmConvert(target, at, &s->input, (size_t)position, count);
            got = r == LND_OK ? (int64_t)count : r;
        } else {
            lnd_callback_enter();
            got = s->config.read(s->config.user, target, at, count);
            lnd_callback_leave();
        }
        if (got == LND_READ_EOF) {
            lnd_store(&s->status, LND_SOURCE_EOF);
            break;
        }
        if (got < 0 || (uint64_t)got > count) {
            int32_t error = got < 0 && got >= INT32_MIN ? (int32_t)got : LND_ERR_IO;
            lnd_store(&s->status, error);
            if (done) s->deferred_error = error;
            return done ? (int64_t)done : error;
        }
        bool eof = (!(s->config.flags & LND_SOURCE_LIVE) && (size_t)got < count) || (!got && s->end_requested) ||
                   (lnd_native_source_bounded(s) && position + (uint64_t)got == length);
        lnd_store(&s->status, eof ? LND_SOURCE_EOF : ((size_t)got < count ? LND_SOURCE_WAITING : LND_SOURCE_READY));
        lnd_store(&s->position, position + (uint64_t)got);
        if (!direct && got) {
            int32_t r = LND_PcmConvert(pcm, offset + done, target, at, (size_t)got);
            if (r != LND_OK) return r;
        }
        done += (size_t)got;
        if ((size_t)got < count) break;
    }
    return (int64_t)done;
}

int32_t LND_SourceGetStatus(const LND_SOURCE *source) {
    if (!source) return LND_ERR_INVALID_ARG;
#if LND_MODULE_GRAPH
    if (source->ops) return source->ops->SourceGetStatus(source);
#endif
    return lnd_load(&((const lnd_native_source *)source)->status);
}

int32_t LND_SourceEnd(LND_SOURCE *source) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!source) return lnd_error(LND_ERR_INVALID_ARG);
#if LND_MODULE_GRAPH
    if (source->ops) return source->ops->SourceEnd(source);
#endif
    lnd_native_source *s = (lnd_native_source *)source;
    if (!s->config.read) return lnd_error(LND_ERR_UNSUPPORTED);
    if (!lnd_spinlock_try(&s->lock)) return lnd_error(LND_ERR_BUSY);
    s->end_requested = true;
    lnd_spinlock_unlock(&s->lock);
    return LND_OK;
}

int64_t LND_SourceReadPcm(LND_SOURCE *source, const LND_PCM *pcm, size_t offset, size_t frames) {
    if (!source || !lnd_pcm_writable(pcm, offset, frames) || frames > (size_t)INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
#if LND_MODULE_GRAPH
    if (source->ops) return source->ops->SourceReadPcm(source, pcm, offset, frames);
#endif
    lnd_native_source *s = (lnd_native_source *)source;
    if (!lnd_spinlock_try(&s->lock)) return lnd_error(LND_ERR_BUSY);
    int64_t result = lnd_native_read(s, pcm, offset, frames);
    lnd_spinlock_unlock(&s->lock);
    if (result < 0) lnd_error((int32_t)result);
    return result;
}

int64_t lnd_native_source_read(lnd_native_source *s, void *dst, int32_t format, uint64_t frames) {
    if (!s || frames > SIZE_MAX || frames > INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    LND_PCM pcm = {.data = dst, .format = format, .channels = s->config.channels, .frames = (size_t)frames};
    int64_t got = LND_SourceReadPcm(&s->base, &pcm, 0, (size_t)frames);
    return got;
}

lnd_native_sound *lnd_native_source_ensure_sound(lnd_native_source *s, const LND_SOUND_CONFIG *config) {
    const LND_SOUND_CONFIG c = config ? *config : (LND_SOUND_CONFIG){0};
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!s) return lnd_error_null(LND_ERR_INVALID_ARG);
    if ((c.sample_rate_hz && c.sample_rate_hz != s->config.sample_rate_hz) || (c.channels && c.channels != s->config.channels) || c.channel_matrix || c.flags)
        return lnd_error_null(LND_ERR_UNSUPPORTED);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_native_sound *sound = s->sound;
    if (!sound) {
        void *memory = lnd_alloc(sizeof(lnd_native_sound));
        sound = memory ? lnd_native_sound_init(memory, sizeof(lnd_native_sound), s) : lnd_error_null(LND_ERR_OUT_OF_MEMORY);
        if (sound)
            sound->owned = true;
        else
            lnd_free(memory);
    }
    lnd_context_unlock();
    return sound;
}
