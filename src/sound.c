#include "native.h"
#include "notify.h"
#include "alloc.h"
#include "pcm.h"
#include "sample.h"
#include "render.h"

#include <string.h>

int32_t lnd_native_sound_ref(LND_SOUND *sound) {
    if (!sound) return LND_ERR_INVALID_ARG;
    if (sound->ops) return LND_ERR_UNSUPPORTED;
    lnd_native_sound *s = (lnd_native_sound *)sound;
    if (!s->source) return LND_ERR_STATE;
    if (s->references == UINT32_MAX) return LND_ERR_BUSY;
    s->references++;
    return LND_OK;
}

void lnd_native_sound_unref(LND_SOUND *sound) {
    lnd_native_sound *s = (lnd_native_sound *)sound;
    LND_ASSERT(s->references);
    s->references--;
}

size_t LND_SoundGetMemoryBytes(void) { return sizeof(lnd_native_sound); }

lnd_native_sound *lnd_native_sound_init(void *memory, size_t bytes, lnd_native_source *source) {
    if (!lnd_ctx.initialized) return lnd_error_null(LND_ERR_STATE);
    if (!memory || bytes < sizeof(lnd_native_sound) || (uintptr_t)memory % alignof(lnd_native_sound) || !source ||
        (!source->config.pcm && !source->config.read))
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (source->sound || lnd_playback_overlaps(memory, sizeof(lnd_native_sound)) || lnd_renderers_overlap(memory, sizeof(lnd_native_sound)))
        return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_spinlock_try(&source->lock)) return lnd_error_null(LND_ERR_BUSY);
    lnd_native_sound *s = memory;
    memset(s, 0, sizeof *s);
    s->source = source;
    lnd_store(&s->gain, 65536);
    source->sound = s;
    lnd_spinlock_unlock(&source->lock);
    return s;
}

LND_SOUND *LND_SoundInit(void *memory, size_t bytes, LND_SOURCE *source) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!source) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (source->ops) return lnd_error_null(LND_ERR_UNSUPPORTED);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_native_sound *s = lnd_native_sound_init(memory, bytes, (lnd_native_source *)source);
    lnd_context_unlock();
    return (LND_SOUND *)s;
}

int32_t LND_SoundFree(LND_SOUND *sound) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!sound) return lnd_error(LND_ERR_INVALID_ARG);
    if (sound->ops) return lnd_error(LND_ERR_UNSUPPORTED);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_native_sound *s = (lnd_native_sound *)sound;
    lnd_native_source *source = s->source;
    int32_t result = LND_ERR_STATE;
    if (s->references)
        result = LND_ERR_BUSY;
    else if (source) {
        if (!lnd_spinlock_try(&source->lock))
            result = LND_ERR_BUSY;
        else {
            lnd_notify_detach(&s->notifications);
            source->sound = nullptr;
            s->source = nullptr;
            lnd_spinlock_unlock(&source->lock);
            if (s->owned) lnd_free(s);
            result = LND_OK;
        }
    }
    lnd_context_unlock();
    return lnd_error(result);
}

static int32_t lnd_sound_enter(lnd_native_sound *s) {
    if (lnd_callback_active()) return LND_ERR_BUSY;
    if (!s) return LND_ERR_INVALID_ARG;
    if (!s->source) return LND_ERR_STATE;
    return lnd_spinlock_try(&s->source->lock) ? LND_OK : LND_ERR_BUSY;
}

int32_t lnd_native_sound_play(lnd_native_sound *s) {
    int32_t r = lnd_sound_enter(s);
    if (r != LND_OK) return lnd_error(r);
    if (s->ended) r = lnd_native_seek(s->source, 0);
#if LND_MODULE_GRAPH
    if (r == LND_OK && lnd_load(&s->state) == LND_SOUND_STOPPED) lnd_add(&s->source->revision, 1);
#endif
    if (r == LND_OK) lnd_store(&s->state, LND_SOUND_PLAYING);
    lnd_spinlock_unlock(&s->source->lock);
    return lnd_error(r);
}

int32_t lnd_native_sound_set_pause(lnd_native_sound *s, bool pause) {
    int32_t r = lnd_sound_enter(s);
    if (r != LND_OK) return lnd_error(r);
    uint32_t state = lnd_load_relaxed(&s->state);
    if (pause && (state == LND_SOUND_PLAYING || state == LND_SOUND_STALLED))
        lnd_store(&s->state, LND_SOUND_PAUSED);
    else if (!pause && state == LND_SOUND_PAUSED)
        lnd_store(&s->state, LND_SOUND_PLAYING);
    lnd_spinlock_unlock(&s->source->lock);
    return LND_OK;
}

int32_t lnd_native_sound_stop(lnd_native_sound *s) {
    int32_t r = lnd_sound_enter(s);
    if (r != LND_OK) return lnd_error(r);
    if (s->source->config.pcm || s->source->config.seek) r = lnd_native_seek(s->source, 0);
    lnd_store(&s->state, LND_SOUND_STOPPED);
    lnd_spinlock_unlock(&s->source->lock);
    return lnd_error(r);
}

int32_t lnd_native_sound_get_state(const lnd_native_sound *s) { return s && s->source ? (int32_t)lnd_load(&s->state) : LND_SOUND_STOPPED; }
lnd_native_source *lnd_native_sound_get_source(const lnd_native_sound *s) { return s ? s->source : nullptr; }
uint64_t lnd_native_sound_get_position_frames(const lnd_native_sound *s) { return s && s->source ? lnd_load(&s->source->position) : 0; }
uint32_t lnd_native_sound_get_sample_rate_hz(const lnd_native_sound *s) { return s && s->source ? s->source->config.sample_rate_hz : 0; }
uint32_t lnd_native_sound_get_channels(const lnd_native_sound *s) { return s && s->source ? s->source->config.channels : 0; }
uint64_t lnd_native_sound_get_length_frames(const lnd_native_sound *s) { return s && s->source ? s->source->config.length_frames : 0; }

int32_t lnd_native_sound_seek_frames(lnd_native_sound *s, uint64_t frame) {
    if (!s || !s->source) return lnd_error(s ? LND_ERR_STATE : LND_ERR_INVALID_ARG);
    return lnd_native_source_seek_frames(s->source, frame);
}

int32_t lnd_native_sound_set_loop(lnd_native_sound *s, bool loop) {
    int32_t r = lnd_sound_enter(s);
    if (r != LND_OK) return lnd_error(r);
    if (loop && !s->source->config.pcm && !s->source->config.seek)
        r = LND_ERR_UNSUPPORTED;
    else
        lnd_store(&s->loop, loop);
    lnd_spinlock_unlock(&s->source->lock);
    return lnd_error(r);
}

bool lnd_native_sound_get_loop(const lnd_native_sound *s) { return s && lnd_load(&s->loop); }

int32_t lnd_native_sound_set_gain_q16(lnd_native_sound *s, uint32_t gain) {
    int32_t r = lnd_sound_enter(s);
    if (r != LND_OK) return lnd_error(r);
    lnd_store(&s->gain, gain);
    lnd_spinlock_unlock(&s->source->lock);
    return LND_OK;
}

uint32_t lnd_native_sound_get_gain_q16(const lnd_native_sound *s) { return s ? lnd_load(&s->gain) : 0; }

int32_t LND_SoundSetGainQ16(LND_SOUND *sound, uint32_t gain) {
    sound = lnd_sound_view(sound);
#if LND_MODULE_GRAPH
    if (sound && sound->ops) return sound->ops->SoundSetGainQ16(sound, gain);
#endif
    return lnd_native_sound_set_gain_q16((lnd_native_sound *)sound, gain);
}

uint32_t LND_SoundGetGainQ16(const LND_SOUND *sound) {
    sound = lnd_sound_view(sound);
#if LND_MODULE_GRAPH
    if (sound && sound->ops) return sound->ops->SoundGetGainQ16(sound);
#endif
    return lnd_native_sound_get_gain_q16((const lnd_native_sound *)sound);
}

static int32_t lnd_sound_gain(const LND_PCM *pcm, size_t offset, size_t frames, uint32_t gain) {
    if (gain == 65536 || !frames) return LND_OK;
    if (!gain) return LND_PcmSilence(pcm, offset, frames);
    if (pcm->format > LND_FORMAT_S32) {
#if LND_MODULE_PCM_FLOAT
        lnd_pcm_gain_float(pcm, offset, frames, gain);
        return LND_OK;
#else
        return LND_ERR_UNSUPPORTED;
#endif
    }
    size_t stride = lnd_pcm_stride(pcm);
    for (uint32_t c = 0; c < pcm->channels; c++) {
        uint8_t *data = lnd_pcm_at(pcm, c, offset);
        for (size_t f = 0; f < frames; f++, data += stride) {
            int64_t value = lnd_pcm_integer_load(data, pcm->format) * gain;
            value = value >= 0 ? (value + 32768) / 65536 : -((-value + 32768) / 65536);
            lnd_pcm_integer_store(data, pcm->format, value);
        }
    }
    return LND_OK;
}

int64_t lnd_native_sound_render(void *sound, const LND_PCM *pcm, size_t offset, size_t frames, bool transport) {
    LND_SOUND *handle = sound;
    if (!handle || !lnd_pcm_writable(pcm, offset, frames) || frames > (size_t)INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    if (handle->ops) return lnd_error(LND_ERR_UNSUPPORTED);
    lnd_native_sound *s = sound;
    lnd_native_source *source = s->source;
    if (!source) return lnd_error(LND_ERR_STATE);
    if (pcm->channels != source->config.channels) return lnd_error(LND_ERR_INVALID_ARG);
    bool direct = pcm->format == source->stage.format && pcm->layout == source->stage.layout;
    if (frames && !direct && !source->stage.frames) return lnd_error(LND_ERR_UNSUPPORTED);
#if !LND_MODULE_PCM_FLOAT
    if (frames && pcm->format > LND_FORMAT_S32 && pcm->format != source->stage.format) return lnd_error(LND_ERR_UNSUPPORTED);
#endif
    if (!lnd_spinlock_try(&source->lock)) return lnd_error(LND_ERR_BUSY);
    size_t done = 0;
    int32_t result = LND_OK;
    bool restarted = false;
    uint32_t gain = lnd_load_relaxed(&s->gain);
    while (done < frames && (!transport || lnd_load_relaxed(&s->state) == LND_SOUND_PLAYING || lnd_load_relaxed(&s->state) == LND_SOUND_STALLED)) {
        const LND_PCM *target = direct ? pcm : &source->stage;
        size_t at = direct ? offset + done : 0;
        size_t want = direct ? frames - done : LND_MIN(frames - done, source->stage.frames);
#if LND_MODULE_NOTIFY
        uint64_t before = lnd_load_relaxed(&source->position);
#endif
        int64_t got = lnd_native_read(source, target, at, want);
        if (transport && got > 0) lnd_notify_range(s->notifications, before, lnd_load_relaxed(&source->position), source->config.sample_rate_hz, true);
        if (got < 0) {
            result = (int32_t)got;
            break;
        }
        if (transport) result = lnd_sound_gain(target, at, (size_t)got, gain);
        if (result == LND_OK && !direct) result = LND_PcmConvert(pcm, offset + done, target, at, (size_t)got);
        if (result != LND_OK) break;
        done += (size_t)got;
        if (got) restarted = false;
        int32_t status = lnd_load_relaxed(&source->status);
        bool end = status == LND_SOURCE_EOF;
        if (!end) {
            if (transport) {
                uint32_t state = status == LND_SOURCE_WAITING ? LND_SOUND_STALLED : LND_SOUND_PLAYING;
#if LND_MODULE_NOTIFY
                uint32_t previous = lnd_load_relaxed(&s->state);
#endif
                lnd_store(&s->state, state);
#if LND_MODULE_NOTIFY
                if (state != previous && (state == LND_SOUND_STALLED || previous == LND_SOUND_STALLED))
                    lnd_notify_emit(s->notifications, state == LND_SOUND_STALLED ? LND_NOTIFY_STALLED : LND_NOTIFY_RESUMED, lnd_load_relaxed(&source->position),
                                    source->config.sample_rate_hz, 0, 0);
#endif
            }
            if ((size_t)got < want) break;
            continue;
        }
        if (!lnd_load_relaxed(&s->loop) || (!got && restarted)) {
            if (transport) {
                if (!s->ended) lnd_notify_emit(s->notifications, LND_NOTIFY_END, lnd_load_relaxed(&source->position), source->config.sample_rate_hz, 0, 0);
                s->ended = true;
                lnd_store(&s->state, LND_SOUND_STOPPED);
            }
            break;
        }
        result = lnd_native_seek(source, 0);
        if (result != LND_OK) {
            if (transport) lnd_store(&s->state, LND_SOUND_STOPPED);
            break;
        }
        restarted = true;
    }
    lnd_spinlock_unlock(&source->lock);
    return done || result == LND_OK ? (int64_t)done : lnd_error(result);
}

int64_t lnd_native_sound_render_pcm(void *sound, const LND_PCM *pcm, size_t offset, size_t frames) { return lnd_native_sound_render(sound, pcm, offset, frames, true); }

int64_t LND_SoundRenderPcm(void *sound, const LND_PCM *pcm, size_t offset, size_t frames) {
    LND_SOUND *s = lnd_sound_view(sound);
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SoundRenderPcm(s, pcm, offset, frames);
#endif
    return lnd_native_sound_render_pcm(s, pcm, offset, frames);
}

int32_t lnd_sound_ref(LND_SOUND *sound) {
    sound = lnd_sound_view(sound);
#if LND_MODULE_GRAPH
    if (sound && sound->ops) return sound->ops->SoundRef(sound);
#endif
    return lnd_native_sound_ref(sound);
}

void lnd_sound_unref(LND_SOUND *sound) {
    sound = lnd_sound_view(sound);
#if LND_MODULE_GRAPH
    if (sound && sound->ops) {
        sound->ops->SoundUnref(sound);
        return;
    }
#endif
    lnd_native_sound_unref(sound);
}
