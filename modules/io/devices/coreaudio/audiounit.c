#include "lindar_coreaudio.h"
#include "io/devices/backend.h"
#include "io/devices/engine.h"
#include "src/alloc.h"
#include "src/atomic.h"
#include "src/thread.h"
#include "src/format.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#if LND_OS_MACOS
#include <CoreAudio/CoreAudio.h>
#endif
#if LND_MODULE_IOS
#include "io/devices/ios/session.h"
#endif
#include <string.h>

static lnd_atomic_u32 lnd_apple_generation;
static lnd_atomic_u32 lnd_apple_inactive;
static lnd_atomic_u32 lnd_apple_events;

typedef struct lnd_apple {
    uint32_t generation;
    uint32_t listeners;
} lnd_apple;

struct lnd_stream {
    AudioUnit unit;
    lnd_stream_proc proc;
    void *user;
    void *buffer;
    lnd_stream_cfg cfg;
    lnd_atomic_u32 failed;
    uint32_t generation;
    UInt32 max_frames;
    bool capture;
    bool initialized;
    bool running;
#if LND_MODULE_IOS
    bool session_acquired;
#endif
#if LND_OS_MACOS
    AudioDeviceID device;
    uint64_t status_time;
#endif
};

void LND_CoreAudioNotifyRouteChange(void) {
    lnd_add(&lnd_apple_generation, 1);
    lnd_engine_wake();
}

void LND_CoreAudioSetSessionActive(bool active) {
    if (lnd_exchange(&lnd_apple_inactive, !active) != (uint32_t)!active) LND_CoreAudioNotifyRouteChange();
}

#if LND_OS_MACOS
static const AudioObjectPropertySelector lnd_apple_selectors[] = {
    kAudioHardwarePropertyDevices,
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioHardwarePropertyDefaultInputDevice,
};

static AudioObjectPropertyAddress lnd_apple_address(AudioObjectPropertySelector selector, AudioObjectPropertyScope scope) {
    return (AudioObjectPropertyAddress){selector, scope, kAudioObjectPropertyElementMain};
}

static OSStatus lnd_apple_get(AudioObjectID object, AudioObjectPropertySelector selector, AudioObjectPropertyScope scope, void *dst, UInt32 size) {
    AudioObjectPropertyAddress address = lnd_apple_address(selector, scope);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, dst);
}

static OSStatus lnd_apple_listener(AudioObjectID object, UInt32 count, const AudioObjectPropertyAddress addresses[], void *user) {
    uint32_t events = 0;
    for (UInt32 i = 0; i < count; i++) {
        if (addresses[i].mSelector == kAudioHardwarePropertyDevices) events |= LND_BACKEND_EVENT_DEVICES;
        if (addresses[i].mSelector == kAudioHardwarePropertyDefaultOutputDevice) events |= LND_BACKEND_EVENT_DEFAULT_OUTPUT;
        if (addresses[i].mSelector == kAudioHardwarePropertyDefaultInputDevice) events |= LND_BACKEND_EVENT_DEFAULT_INPUT;
    }
    atomic_fetch_or_explicit(&lnd_apple_events, events, memory_order_release);
    lnd_engine_wake();
    return noErr;
}

static char *lnd_apple_string(AudioDeviceID device, AudioObjectPropertySelector selector) {
    CFStringRef value = nullptr;
    if (lnd_apple_get(device, selector, kAudioObjectPropertyScopeGlobal, &value, sizeof value) != noErr || !value) return nullptr;
    CFIndex size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8);
    char *text = size >= 0 && (uint64_t)size < SIZE_MAX ? lnd_alloc((size_t)size + 1) : nullptr;
    if (text && !CFStringGetCString(value, text, size + 1, kCFStringEncodingUTF8)) {
        lnd_free(text);
        text = nullptr;
    }
    CFRelease(value);
    return text;
}

static uint32_t lnd_apple_channels(AudioDeviceID device, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress address = lnd_apple_address(kAudioDevicePropertyStreamConfiguration, scope);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr || size < sizeof(UInt32)) return 0;
    AudioBufferList *list = lnd_alloc(size);
    if (!list) return 0;
    uint32_t channels = 0;
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, list) == noErr && size >= offsetof(AudioBufferList, mBuffers) &&
        list->mNumberBuffers <= (size - offsetof(AudioBufferList, mBuffers)) / sizeof(AudioBuffer)) {
        for (UInt32 i = 0; i < list->mNumberBuffers; i++) {
            if (list->mBuffers[i].mNumberChannels > UINT32_MAX - channels) {
                channels = 0;
                break;
            }
            channels += list->mBuffers[i].mNumberChannels;
        }
    }
    lnd_free(list);
    return channels;
}
#endif

static int32_t lnd_apple_init(lnd_backend *b) {
    lnd_apple *a = lnd_alloc_zero(sizeof *a);
    if (!a) return LND_ERR_OUT_OF_MEMORY;
    a->generation = lnd_load(&lnd_apple_generation);
#if LND_OS_MACOS
    for (uint32_t i = 0; i < LND_COUNTOF(lnd_apple_selectors); i++) {
        AudioObjectPropertyAddress address = lnd_apple_address(lnd_apple_selectors[i], kAudioObjectPropertyScopeGlobal);
        if (AudioObjectAddPropertyListener(kAudioObjectSystemObject, &address, lnd_apple_listener, nullptr) == noErr) a->listeners |= 1u << i;
    }
#endif
    b->data = a;
    return LND_OK;
}

static void lnd_apple_free(lnd_backend *b) {
    lnd_apple *a = b->data;
#if LND_OS_MACOS
    for (uint32_t i = 0; i < LND_COUNTOF(lnd_apple_selectors); i++) {
        if (!(a->listeners & (1u << i))) continue;
        AudioObjectPropertyAddress address = lnd_apple_address(lnd_apple_selectors[i], kAudioObjectPropertyScopeGlobal);
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &address, lnd_apple_listener, nullptr);
    }
#endif
    lnd_free(a);
    b->data = nullptr;
}

static int32_t lnd_apple_enumerate(lnd_backend *b, int32_t type, lnd_device_list *out) {
#if LND_OS_MACOS
    AudioObjectPropertyScope scope = type == LND_DEVICE_OUTPUT ? kAudioDevicePropertyScopeOutput : kAudioDevicePropertyScopeInput;
    AudioDeviceID default_device = kAudioObjectUnknown;
    lnd_apple_get(kAudioObjectSystemObject, type == LND_DEVICE_OUTPUT ? kAudioHardwarePropertyDefaultOutputDevice : kAudioHardwarePropertyDefaultInputDevice,
                  kAudioObjectPropertyScopeGlobal, &default_device, sizeof default_device);
    AudioObjectPropertyAddress address = lnd_apple_address(kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr) return LND_ERR_EXTERNAL;
    if (!size) return LND_OK;
    AudioDeviceID *ids = lnd_alloc(size);
    if (!ids) return LND_ERR_OUT_OF_MEMORY;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, ids) != noErr) {
        lnd_free(ids);
        return LND_ERR_EXTERNAL;
    }
    int32_t r = LND_OK;
    for (UInt32 i = 0; i < size / sizeof *ids && r == LND_OK; i++) {
        uint32_t channels = lnd_apple_channels(ids[i], scope);
        if (!channels) continue;
        char *name = lnd_apple_string(ids[i], kAudioObjectPropertyName);
        char *uid = lnd_apple_string(ids[i], kAudioDevicePropertyDeviceUID);
        lnd_device *d = uid ? lnd_device_new(type, name, uid) : nullptr;
        lnd_free(name);
        lnd_free(uid);
        if (!d) {
            r = LND_ERR_OUT_OF_MEMORY;
            break;
        }
        d->flags = LND_DEVICE_FLAG_SHARED | LND_DEVICE_FLAG_MULTI_INSTANCE;
        if (ids[i] == default_device) d->flags |= LND_DEVICE_FLAG_DEFAULT;
        Float64 sample_rate_hz = 0;
        UInt32 period = 0;
        AudioValueRange range = {0};
        lnd_apple_get(ids[i], kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, &sample_rate_hz, sizeof sample_rate_hz);
        lnd_apple_get(ids[i], kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, &period, sizeof period);
        lnd_apple_get(ids[i], kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal, &range, sizeof range);
        d->sample_rate_hz = sample_rate_hz > 0 && sample_rate_hz <= UINT32_MAX ? (uint32_t)sample_rate_hz : 0;
        d->channels = LND_MIN(channels, LND_MAX_CHANNELS);
        d->format = LND_FORMAT_F32;
        d->default_period = period;
        d->min_period = range.mMinimum > 0 && range.mMinimum <= UINT32_MAX ? (uint32_t)range.mMinimum : 0;
        d->backend_data = (void *)(uintptr_t)ids[i];
        r = lnd_device_list_push(out, d);
        if (r != LND_OK) lnd_device_free(d);
    }
    lnd_free(ids);
    return r;
#else
    lnd_device *d =
        lnd_device_new(type, type == LND_DEVICE_OUTPUT ? "System Output" : "System Input", type == LND_DEVICE_OUTPUT ? "remoteio:output" : "remoteio:input");
    if (!d) return LND_ERR_OUT_OF_MEMORY;
    d->flags = LND_DEVICE_FLAG_SHARED | LND_DEVICE_FLAG_DEFAULT;
    d->max_instances = 1;
    d->format = LND_FORMAT_F32;
    int32_t r = lnd_device_list_push(out, d);
    if (r != LND_OK) lnd_device_free(d);
    return r;
#endif
}

static uint32_t lnd_apple_poll(lnd_backend *b) {
    lnd_apple *a = b->data;
    uint32_t events = lnd_exchange(&lnd_apple_events, 0);
    uint32_t generation = lnd_load(&lnd_apple_generation);
    if (generation != a->generation) {
        a->generation = generation;
        events |= LND_BACKEND_EVENT_DEVICES | LND_BACKEND_EVENT_DEFAULT_INPUT | LND_BACKEND_EVENT_DEFAULT_OUTPUT;
    }
    return events;
}

static void lnd_apple_silence(lnd_stream *s, AudioBufferList *buffers) {
    if (!buffers) return;
    for (UInt32 i = 0; i < buffers->mNumberBuffers; i++)
        if (buffers->mBuffers[i].mData) memset(buffers->mBuffers[i].mData, s->cfg.format == LND_FORMAT_U8 ? 128 : 0, buffers->mBuffers[i].mDataByteSize);
}

static OSStatus lnd_apple_render(void *user, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus, UInt32 frames,
                                 AudioBufferList *buffers) {
    lnd_stream *s = user;
    if (lnd_load(&lnd_apple_inactive) || lnd_load(&s->failed) || s->generation != lnd_load(&lnd_apple_generation)) {
        if (!s->capture) lnd_apple_silence(s, buffers);
        *flags |= kAudioUnitRenderAction_OutputIsSilence;
        return noErr;
    }
    uint64_t bytes = (uint64_t)frames * s->cfg.channels * lnd_format_bytes(s->cfg.format);
    if (s->capture) {
        if (frames > s->max_frames || bytes > UINT32_MAX) {
            lnd_store(&s->failed, 1);
            return kAudioUnitErr_TooManyFramesToProcess;
        }
        AudioBufferList data = {1, {{s->cfg.channels, (UInt32)bytes, s->buffer}}};
        OSStatus r = AudioUnitRender(s->unit, flags, time, 1, frames, &data);
        if (r != noErr) {
            lnd_store(&s->failed, 1);
            return r;
        }
        if (data.mBuffers[0].mDataByteSize < bytes || data.mBuffers[0].mData != s->buffer) {
            lnd_store(&s->failed, 1);
            return kAudioUnitErr_InvalidPropertyValue;
        }
        if (*flags & kAudioUnitRenderAction_OutputIsSilence) memset(s->buffer, s->cfg.format == LND_FORMAT_U8 ? 128 : 0, (size_t)bytes);
        if (frames) s->proc(s->user, s->buffer, frames);
    } else {
        if (!buffers || buffers->mNumberBuffers != 1 || !buffers->mBuffers[0].mData || buffers->mBuffers[0].mDataByteSize < bytes ||
            buffers->mBuffers[0].mNumberChannels != s->cfg.channels) {
            lnd_apple_silence(s, buffers);
            lnd_store(&s->failed, 1);
            *flags |= kAudioUnitRenderAction_OutputIsSilence;
            return kAudioUnitErr_InvalidPropertyValue;
        }
        if (frames) s->proc(s->user, buffers->mBuffers[0].mData, frames);
    }
    return noErr;
}

static void lnd_apple_dispose(lnd_stream *s) {
    if (s->unit) {
        if (s->running) AudioOutputUnitStop(s->unit);
        if (s->initialized) AudioUnitUninitialize(s->unit);
        AudioComponentInstanceDispose(s->unit);
    }
#if LND_MODULE_IOS
    if (s->session_acquired) lnd_ios_session_release(s->capture);
#endif
    lnd_free_aligned(s->buffer);
    lnd_free(s);
}

static int32_t lnd_apple_open_common(lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out, bool capture) {
    if (cfg->exclusive || cfg->loopback) return LND_ERR_UNSUPPORTED;
    if (lnd_load(&lnd_apple_inactive)) return LND_ERR_STATE;
    if (cfg->channels > LND_MAX_CHANNELS || cfg->format < LND_FORMAT_NONE || cfg->format > LND_FORMAT_F64) return LND_ERR_FORMAT;
    lnd_stream *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->proc = proc;
    s->user = user;
    s->capture = capture;
    s->generation = lnd_load(&lnd_apple_generation);
    int32_t result = LND_ERR_EXTERNAL;
#if LND_MODULE_IOS
    result = lnd_ios_session_acquire(capture);
    if (result != LND_OK) goto fail;
    s->session_acquired = true;
#endif
    AudioComponentDescription desc = {
        .componentType = kAudioUnitType_Output,
#if LND_OS_MACOS
        .componentSubType = kAudioUnitSubType_HALOutput,
#else
        .componentSubType = kAudioUnitSubType_RemoteIO,
#endif
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    result = LND_ERR_EXTERNAL;
    if (!component || AudioComponentInstanceNew(component, &s->unit) != noErr) goto fail;
    UInt32 on = 1, off = 0;
    if (capture) {
        if (AudioUnitSetProperty(s->unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &on, sizeof on) != noErr ||
            AudioUnitSetProperty(s->unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &off, sizeof off) != noErr)
            goto fail;
    }
#if LND_OS_MACOS
    s->device = (AudioDeviceID)(uintptr_t)d->backend_data;
    if (AudioUnitSetProperty(s->unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &s->device, sizeof s->device) != noErr) goto fail;
#endif
    AudioStreamBasicDescription native = {0};
    UInt32 size = sizeof native;
    if (AudioUnitGetProperty(s->unit, kAudioUnitProperty_StreamFormat, capture ? kAudioUnitScope_Input : kAudioUnitScope_Output, capture ? 1 : 0, &native,
                             &size) != noErr)
        goto fail;
    if (!(native.mSampleRate > 0 && native.mSampleRate <= UINT32_MAX)) goto fail;
#if LND_MODULE_IOS
    if (capture && native.mChannelsPerFrame < lnd_ios_session_input_channels()) {
        result = LND_ERR_UNSUPPORTED;
        goto fail;
    }
#endif
    cfg->sample_rate_hz = (uint32_t)native.mSampleRate;
    cfg->channels = cfg->channels ? cfg->channels : LND_MIN(native.mChannelsPerFrame, capture ? LND_MAX_CHANNELS : 2u);
    if (!cfg->channels || cfg->channels > LND_MAX_CHANNELS) {
        result = LND_ERR_FORMAT;
        goto fail;
    }
    cfg->format = cfg->format ? cfg->format : LND_FORMAT_F32;
    UInt32 sample_bytes = (UInt32)lnd_format_bytes(cfg->format);
    AudioStreamBasicDescription format = {
        .mSampleRate = cfg->sample_rate_hz,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsPacked | (cfg->format == LND_FORMAT_F32 || cfg->format == LND_FORMAT_F64 ? kAudioFormatFlagIsFloat
                                                    : cfg->format == LND_FORMAT_U8                                 ? 0
                                                                                                                   : kAudioFormatFlagIsSignedInteger),
        .mBytesPerPacket = sample_bytes * cfg->channels,
        .mFramesPerPacket = 1,
        .mBytesPerFrame = sample_bytes * cfg->channels,
        .mChannelsPerFrame = cfg->channels,
        .mBitsPerChannel = sample_bytes * 8,
    };
    if (AudioUnitSetProperty(s->unit, kAudioUnitProperty_StreamFormat, capture ? kAudioUnitScope_Output : kAudioUnitScope_Input, capture ? 1 : 0, &format,
                             sizeof format) != noErr) {
        result = LND_ERR_FORMAT;
        goto fail;
    }
    AURenderCallbackStruct callback = {lnd_apple_render, s};
    if (AudioUnitSetProperty(s->unit, capture ? kAudioOutputUnitProperty_SetInputCallback : kAudioUnitProperty_SetRenderCallback,
                             capture ? kAudioUnitScope_Global : kAudioUnitScope_Input, 0, &callback, sizeof callback) != noErr)
        goto fail;
    s->max_frames = LND_MAX(4096u, cfg->period_frames);
    if (s->max_frames > 1048576) {
        result = LND_ERR_INVALID_ARG;
        goto fail;
    }
    if (AudioUnitSetProperty(s->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &s->max_frames, sizeof s->max_frames) != noErr)
        goto fail;
    if (capture && AudioUnitSetProperty(s->unit, kAudioUnitProperty_ShouldAllocateBuffer, kAudioUnitScope_Output, 1, &off, sizeof off) != noErr) goto fail;
    if (AudioUnitInitialize(s->unit) != noErr) goto fail;
    s->initialized = true;
    size = sizeof s->max_frames;
    if (AudioUnitGetProperty(s->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &s->max_frames, &size) != noErr || !s->max_frames ||
        s->max_frames > 1048576)
        goto fail;
    cfg->period_frames = d->default_period ? d->default_period : LND_MAX(1u, cfg->sample_rate_hz / 100);
    cfg->buffer_frames = cfg->period_frames;
    Float64 latency = 0;
    size = sizeof latency;
    AudioUnitGetProperty(s->unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency, &size);
    cfg->latency_frames = cfg->period_frames;
    if (latency > 0 && latency * cfg->sample_rate_hz < UINT32_MAX - cfg->period_frames) cfg->latency_frames += (uint32_t)(latency * cfg->sample_rate_hz);
    if (capture) {
        s->buffer = lnd_alloc_aligned((size_t)s->max_frames * format.mBytesPerFrame, LND_CACHE_LINE);
        if (!s->buffer) {
            result = LND_ERR_OUT_OF_MEMORY;
            goto fail;
        }
    }
    s->cfg = *cfg;
#if LND_MODULE_IOS
    s->generation = lnd_load(&lnd_apple_generation);
#endif
    *out = s;
    return LND_OK;
fail:
    lnd_apple_dispose(s);
    return result;
}

static int32_t lnd_apple_open(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    return lnd_apple_open_common(d, cfg, proc, user, out, false);
}

static int32_t lnd_apple_open_capture(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    return lnd_apple_open_common(d, cfg, proc, user, out, true);
}

static int32_t lnd_apple_start(lnd_stream *s) {
    if (s->running) return LND_OK;
    if (lnd_load(&lnd_apple_inactive)) return LND_ERR_STATE;
    if (AudioOutputUnitStart(s->unit) != noErr) return LND_ERR_EXTERNAL;
    s->running = true;
    return LND_OK;
}

static int32_t lnd_apple_stop(lnd_stream *s) {
    if (!s->running) return LND_OK;
    OSStatus r = AudioOutputUnitStop(s->unit);
    if (r != noErr) {
        AudioUnitUninitialize(s->unit);
        s->initialized = false;
        lnd_store(&s->failed, 1);
    }
    s->running = false;
    return r == noErr ? LND_OK : LND_ERR_EXTERNAL;
}

static int32_t lnd_apple_status(lnd_stream *s) {
    if (lnd_load(&s->failed) || lnd_load(&lnd_apple_inactive) || s->generation != lnd_load(&lnd_apple_generation)) return LND_ERR_EXTERNAL;
#if LND_OS_MACOS
    uint64_t now = lnd_time_ns();
    if (now - s->status_time >= 250000000ull) {
        s->status_time = now;
        UInt32 alive = 0;
        Float64 sample_rate_hz = 0;
        if (lnd_apple_get(s->device, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, &alive, sizeof alive) != noErr || !alive ||
            lnd_apple_get(s->device, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, &sample_rate_hz, sizeof sample_rate_hz) != noErr ||
            sample_rate_hz != s->cfg.sample_rate_hz)
            return LND_ERR_EXTERNAL;
    }
#endif
    return LND_OK;
}

static void lnd_apple_close(lnd_stream *s) {
    lnd_apple_stop(s);
    lnd_apple_dispose(s);
}

const lnd_backend_vt lnd_backend_coreaudio_vt = {
    .name = "coreaudio",
    .init = lnd_apple_init,
    .free = lnd_apple_free,
    .enumerate = lnd_apple_enumerate,
    .poll = lnd_apple_poll,
    .open = lnd_apple_open,
    .open_capture = lnd_apple_open_capture,
    .start = lnd_apple_start,
    .stop = lnd_apple_stop,
    .status = lnd_apple_status,
    .close = lnd_apple_close,
};
