#include "lindar_android.h"
#include "io/devices/backend.h"
#include "io/devices/engine.h"
#include "io/devices/context.h"
#include "src/alloc.h"
#include "src/atomic.h"
#include "src/error.h"
#include "src/config.h"

#include <aaudio/AAudio.h>
#include <android/api-level.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>

typedef struct lnd_android {
    lnd_device_list devices[2];
    uint32_t generation;
} lnd_android;

extern const lnd_backend_vt lnd_backend_aaudio_vt;

static lnd_atomic_u32 lnd_android_generation;
static lnd_atomic_u32 lnd_android_inactive;
static lnd_atomic_u32 lnd_android_changed;

struct lnd_stream {
    AAudioStream *stream;
    lnd_stream_proc proc;
    void *user;
    lnd_atomic_u32 failed;
    uint32_t generation;
    bool running;
};

void LND_AaudioNotifyRouteChange(void) {
    lnd_add(&lnd_android_generation, 1);
    lnd_engine_wake();
}

void LND_AaudioSetActive(bool active) {
    if (lnd_exchange(&lnd_android_inactive, !active) != (uint32_t)!active) LND_AaudioNotifyRouteChange();
}

static int32_t lnd_android_device(lnd_device_list *out, int32_t type, int32_t id, const char *name) {
    char key[48];
    snprintf(key, sizeof key, "aaudio:%s:%d", type == LND_DEVICE_OUTPUT ? "output" : "input", id);
    lnd_device *d = lnd_device_new(type, name, key);
    if (!d) return LND_ERR_OUT_OF_MEMORY;
    d->flags = LND_DEVICE_FLAG_SHARED | LND_DEVICE_FLAG_MULTI_INSTANCE | LND_DEVICE_FLAG_EXCLUSIVE;
    if (!id) d->flags |= LND_DEVICE_FLAG_DEFAULT;
    d->backend_data = (void *)(uintptr_t)id;
    d->format = LND_FORMAT_F32;
    int32_t r = lnd_device_list_push(out, d);
    if (r != LND_OK) lnd_device_free(d);
    return r;
}

int32_t LND_AaudioSetDevices(const LND_AAUDIO_DEVICE *devices, uint32_t count) {
    if ((count && !devices) || count > 1024) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_device_ctx.backend_ready || lnd_device_ctx.backend.vt != &lnd_backend_aaudio_vt) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_STATE);
    }
    lnd_device_list fresh[2] = {0};
    int32_t r = LND_OK;
    for (uint32_t i = 0; i < count && r == LND_OK; i++) {
        const LND_AAUDIO_DEVICE *d = &devices[i];
        if (d->id <= 0 || (d->type != LND_DEVICE_OUTPUT && d->type != LND_DEVICE_INPUT) || !d->name) {
            r = LND_ERR_INVALID_ARG;
            break;
        }
        for (uint32_t j = 0; j < i; j++)
            if (devices[j].id == d->id && devices[j].type == d->type) r = LND_ERR_INVALID_ARG;
        if (r == LND_OK) r = lnd_android_device(&fresh[d->type], d->type, d->id, d->name);
    }
    if (r == LND_OK) {
        lnd_android *a = lnd_device_ctx.backend.data;
        for (unsigned t = 0; t < 2; t++) {
            lnd_device_list_free(&a->devices[t]);
            a->devices[t] = fresh[t];
            fresh[t] = (lnd_device_list){0};
        }
        lnd_store(&lnd_android_changed, 1);
        lnd_engine_wake();
    }
    for (unsigned t = 0; t < 2; t++)
        lnd_device_list_free(&fresh[t]);
    lnd_context_unlock();
    return lnd_error(r);
}

static int32_t lnd_aaudio_init(lnd_backend *b) {
    lnd_android *a = lnd_alloc_zero(sizeof *a);
    if (!a) return LND_ERR_OUT_OF_MEMORY;
    a->generation = lnd_load(&lnd_android_generation);
    b->data = a;
    return LND_OK;
}

static void lnd_aaudio_free(lnd_backend *b) {
    lnd_android *a = b->data;
    for (unsigned t = 0; t < 2; t++)
        lnd_device_list_free(&a->devices[t]);
    lnd_free(a);
    b->data = nullptr;
}

static int32_t lnd_aaudio_enumerate(lnd_backend *b, int32_t type, lnd_device_list *out) {
    lnd_android *a = b->data;
    int32_t r = lnd_android_device(out, type, 0, type == LND_DEVICE_OUTPUT ? "System Output" : "System Input");
    for (uint32_t i = 0; i < a->devices[type].count && r == LND_OK; i++) {
        lnd_device *d = a->devices[type].items[i];
        r = lnd_android_device(out, type, (int32_t)(uintptr_t)d->backend_data, d->name);
    }
    return r;
}

static uint32_t lnd_aaudio_poll(lnd_backend *b) {
    lnd_android *a = b->data;
    uint32_t generation = lnd_load(&lnd_android_generation);
    uint32_t events = lnd_exchange(&lnd_android_changed, 0) ? LND_BACKEND_EVENT_DEVICES : 0;
    if (a->generation != generation) {
        a->generation = generation;
        events |= LND_BACKEND_EVENT_DEVICES | LND_BACKEND_EVENT_DEFAULT_INPUT | LND_BACKEND_EVENT_DEFAULT_OUTPUT;
    }
    return events;
}

static int32_t lnd_aaudio_error(aaudio_result_t r) {
    if (r == AAUDIO_ERROR_NO_MEMORY) return LND_ERR_OUT_OF_MEMORY;
    if (r == AAUDIO_ERROR_DISCONNECTED || r == AAUDIO_ERROR_UNAVAILABLE) return LND_ERR_NO_DEVICE;
    if (r == AAUDIO_ERROR_INVALID_FORMAT || r == AAUDIO_ERROR_INVALID_RATE) return LND_ERR_FORMAT;
    return LND_ERR_EXTERNAL;
}

static aaudio_data_callback_result_t lnd_aaudio_data(AAudioStream *stream, void *user, void *data, int32_t frames) {
    lnd_stream *s = user;
    if (lnd_load(&s->failed) || lnd_load(&lnd_android_inactive) || s->generation != lnd_load(&lnd_android_generation)) return AAUDIO_CALLBACK_RESULT_STOP;
    if (frames > 0) s->proc(s->user, data, (uint32_t)frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void lnd_aaudio_failure(AAudioStream *stream, void *user, aaudio_result_t error) {
    lnd_stream *s = user;
    lnd_store(&s->failed, 1);
    if (error == AAUDIO_ERROR_DISCONNECTED) lnd_store(&lnd_android_changed, 1);
}

static int32_t lnd_aaudio_recording_profile(AAudioStreamBuilder *builder) {
    uint32_t profile = lnd_cfg_u32(LND_CFG_ANDROID_RECORDING_PROFILE);
    if (!profile) return LND_OK;
    static const int32_t presets[] = {0, AAUDIO_INPUT_PRESET_GENERIC, AAUDIO_INPUT_PRESET_CAMCORDER, AAUDIO_INPUT_PRESET_VOICE_RECOGNITION,
                                      AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION, AAUDIO_INPUT_PRESET_UNPROCESSED, AAUDIO_INPUT_PRESET_VOICE_PERFORMANCE};
    if (profile >= LND_COUNTOF(presets)) return LND_ERR_INVALID_ARG;
    if (android_get_device_api_level() < (profile == LND_ANDROID_RECORDING_VOICE_PERFORMANCE ? 29 : 28)) return LND_ERR_UNSUPPORTED;
    void (*set_preset)(AAudioStreamBuilder *, int32_t) = (void (*)(AAudioStreamBuilder *, int32_t))dlsym(RTLD_DEFAULT, "AAudioStreamBuilder_setInputPreset");
    if (!set_preset) return LND_ERR_UNSUPPORTED;
    set_preset(builder, presets[profile]);
    return LND_OK;
}

static int32_t lnd_aaudio_open_common(lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out, bool capture) {
    if (cfg->loopback) return LND_ERR_UNSUPPORTED;
    if (lnd_load(&lnd_android_inactive)) return LND_ERR_STATE;
    if ((cfg->format && cfg->format != LND_FORMAT_F32 && cfg->format != LND_FORMAT_S16) || cfg->channels > LND_MAX_CHANNELS || cfg->sample_rate_hz > INT32_MAX ||
        cfg->period_frames > INT32_MAX)
        return LND_ERR_FORMAT;
    lnd_stream *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->proc = proc;
    s->user = user;
    s->generation = lnd_load(&lnd_android_generation);
    AAudioStreamBuilder *builder = nullptr;
    aaudio_result_t r = AAudio_createStreamBuilder(&builder);
    if (r != AAUDIO_OK) {
        lnd_free(s);
        return lnd_aaudio_error(r);
    }
    if (capture) {
        int32_t result = lnd_aaudio_recording_profile(builder);
        if (result != LND_OK) {
            AAudioStreamBuilder_delete(builder);
            lnd_free(s);
            return result;
        }
    }
    AAudioStreamBuilder_setDirection(builder, capture ? AAUDIO_DIRECTION_INPUT : AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setDeviceId(builder, (int32_t)(uintptr_t)d->backend_data);
    AAudioStreamBuilder_setSharingMode(builder, cfg->exclusive ? AAUDIO_SHARING_MODE_EXCLUSIVE : AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, cfg->format == LND_FORMAT_S16 ? AAUDIO_FORMAT_PCM_I16 : AAUDIO_FORMAT_PCM_FLOAT);
    if (cfg->sample_rate_hz) AAudioStreamBuilder_setSampleRate(builder, (int32_t)cfg->sample_rate_hz);
    if (cfg->channels) AAudioStreamBuilder_setChannelCount(builder, (int32_t)cfg->channels);
    if (cfg->period_frames) AAudioStreamBuilder_setFramesPerDataCallback(builder, (int32_t)cfg->period_frames);
    AAudioStreamBuilder_setDataCallback(builder, lnd_aaudio_data, s);
    AAudioStreamBuilder_setErrorCallback(builder, lnd_aaudio_failure, s);
    r = AAudioStreamBuilder_openStream(builder, &s->stream);
    AAudioStreamBuilder_delete(builder);
    if (r != AAUDIO_OK) goto fail;
    if (cfg->exclusive && AAudioStream_getSharingMode(s->stream) != AAUDIO_SHARING_MODE_EXCLUSIVE) {
        r = AAUDIO_ERROR_UNAVAILABLE;
        goto fail;
    }
    int32_t sample_rate_hz = AAudioStream_getSampleRate(s->stream);
    int32_t channels = AAudioStream_getChannelCount(s->stream);
    int32_t burst = AAudioStream_getFramesPerBurst(s->stream);
    int32_t capacity = AAudioStream_getBufferCapacityInFrames(s->stream);
    aaudio_format_t format = AAudioStream_getFormat(s->stream);
    if (sample_rate_hz <= 0 || channels <= 0 || channels > LND_MAX_CHANNELS || burst <= 0 || capacity <= 0 ||
        (format != AAUDIO_FORMAT_PCM_FLOAT && format != AAUDIO_FORMAT_PCM_I16)) {
        r = AAUDIO_ERROR_INVALID_FORMAT;
        goto fail;
    }
    uint32_t period = cfg->period_frames ? cfg->period_frames : (uint32_t)burst;
    uint64_t wanted = (uint64_t)period * LND_MAX(cfg->periods, 2u);
    int32_t size = AAudioStream_setBufferSizeInFrames(s->stream, (int32_t)LND_MIN(wanted, (uint64_t)capacity));
    if (size <= 0) size = AAudioStream_getBufferSizeInFrames(s->stream);
    if (size <= 0) {
        r = AAUDIO_ERROR_INTERNAL;
        goto fail;
    }
    cfg->sample_rate_hz = (uint32_t)sample_rate_hz;
    cfg->channels = (uint32_t)channels;
    cfg->format = format == AAUDIO_FORMAT_PCM_FLOAT ? LND_FORMAT_F32 : LND_FORMAT_S16;
    cfg->period_frames = period;
    cfg->buffer_frames = (uint32_t)size;
    cfg->latency_frames = (uint32_t)size + (uint32_t)burst;
    *out = s;
    return LND_OK;
fail:
    if (s->stream) AAudioStream_close(s->stream);
    lnd_free(s);
    return lnd_aaudio_error(r);
}

static int32_t lnd_aaudio_open(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    return lnd_aaudio_open_common(d, cfg, proc, user, out, false);
}

static int32_t lnd_aaudio_open_capture(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    return lnd_aaudio_open_common(d, cfg, proc, user, out, true);
}

static int32_t lnd_aaudio_start(lnd_stream *s) {
    if (s->running) return LND_OK;
    if (!s->stream || lnd_load(&lnd_android_inactive)) return LND_ERR_STATE;
    aaudio_result_t r = AAudioStream_requestStart(s->stream);
    if (r != AAUDIO_OK) return lnd_aaudio_error(r);
    s->running = true;
    return LND_OK;
}

static int32_t lnd_aaudio_stop(lnd_stream *s) {
    if (!s->stream || !s->running) return LND_OK;
    aaudio_result_t r = AAudioStream_requestStop(s->stream);
    aaudio_stream_state_t state = AAudioStream_getState(s->stream);
    while (r == AAUDIO_OK && (state == AAUDIO_STREAM_STATE_STOPPING || state == AAUDIO_STREAM_STATE_STARTING || state == AAUDIO_STREAM_STATE_STARTED)) {
        r = AAudioStream_waitForStateChange(s->stream, state, &state, 1000000000ll);
    }
    if (r != AAUDIO_OK) {
        AAudioStream_close(s->stream);
        s->stream = nullptr;
        lnd_store(&s->failed, 1);
    }
    s->running = false;
    return r == AAUDIO_OK ? LND_OK : lnd_aaudio_error(r);
}

static int32_t lnd_aaudio_status(lnd_stream *s) {
    return !s->stream || lnd_load(&s->failed) || lnd_load(&lnd_android_inactive) || s->generation != lnd_load(&lnd_android_generation) ? LND_ERR_EXTERNAL
                                                                                                                                       : LND_OK;
}

static void lnd_aaudio_close(lnd_stream *s) {
    lnd_aaudio_stop(s);
    if (s->stream) AAudioStream_close(s->stream);
    lnd_free(s);
}

const lnd_backend_vt lnd_backend_aaudio_vt = {
    .name = "aaudio",
    .init = lnd_aaudio_init,
    .free = lnd_aaudio_free,
    .enumerate = lnd_aaudio_enumerate,
    .poll = lnd_aaudio_poll,
    .open = lnd_aaudio_open,
    .open_capture = lnd_aaudio_open_capture,
    .start = lnd_aaudio_start,
    .stop = lnd_aaudio_stop,
    .status = lnd_aaudio_status,
    .close = lnd_aaudio_close,
};
