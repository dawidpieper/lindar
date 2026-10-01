#include "src/platform.h"
#undef LND_OS_MACOS
#undef LND_OS_IOS
#define LND_OS_MACOS LND_TEST_MACOS
#define LND_OS_IOS (!LND_TEST_MACOS)
#undef LND_MODULE_IOS
#define LND_MODULE_IOS (!LND_TEST_MACOS)
#include "io/devices/coreaudio/audiounit.c"
#if !LND_TEST_MACOS
#include "io/devices/ios/session.c"
int32_t lnd_ios_native_configure(const LND_IOS_SESSION_CONFIG *config) { return LND_OK; }
int32_t lnd_ios_native_active(bool active) { return LND_OK; }
int32_t lnd_ios_native_input(int32_t recording, int32_t orientation) { return LND_OK; }
int32_t lnd_ios_native_orientation(int32_t orientation) { return LND_OK; }
#endif
#include <stdio.h>

#include <stdlib.h>

static unsigned checks, failures, units, listeners, callbacks, calls, fail_at, render_error, silent;
static unsigned init_count, uninit_count, input_enabled, output_disabled;
static Float64 device_rate = 48000;
static UInt32 alive = 1, device_channels = 2;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%d: %s\n", __LINE__, #x);                                                                                                                  \
        }                                                                                                                                                      \
    } while (0)
static bool fail(void) { return ++calls == fail_at; }

struct MockUnit {
    AURenderCallbackStruct callback;
    AudioStreamBasicDescription format;
    UInt32 maximum;
    bool capture;
};
AudioComponent AudioComponentFindNext(AudioComponent previous, const AudioComponentDescription *desc) {
    CHECK(desc->componentSubType == (LND_TEST_MACOS ? kAudioUnitSubType_HALOutput : kAudioUnitSubType_RemoteIO));
    return fail() ? nullptr : (void *)1;
}
OSStatus AudioComponentInstanceNew(AudioComponent component, AudioComponentInstance *unit) {
    if (fail()) return -100;
    *unit = calloc(1, sizeof **unit);
    units++;
    return noErr;
}
OSStatus AudioComponentInstanceDispose(AudioComponentInstance unit) {
    free(unit);
    units--;
    return noErr;
}
OSStatus AudioUnitSetProperty(AudioUnit unit, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, const void *data, UInt32 size) {
    if (fail()) return -100;
    if (id == kAudioUnitProperty_StreamFormat) {
        unit->format = *(const AudioStreamBasicDescription *)data;
        CHECK(element == (unit->capture ? 1u : 0u));
        CHECK(scope == (unit->capture ? kAudioUnitScope_Output : kAudioUnitScope_Input));
    } else if (id == kAudioUnitProperty_SetRenderCallback || id == kAudioOutputUnitProperty_SetInputCallback) {
        unit->callback = *(const AURenderCallbackStruct *)data;
        CHECK(element == 0);
        CHECK(scope == (unit->capture ? kAudioUnitScope_Global : kAudioUnitScope_Input));
    } else if (id == kAudioOutputUnitProperty_EnableIO) {
        if (element == 1 && scope == kAudioUnitScope_Input && *(const UInt32 *)data) {
            unit->capture = true;
            input_enabled++;
        }
        if (element == 0 && scope == kAudioUnitScope_Output && !*(const UInt32 *)data) output_disabled++;
    } else if (id == kAudioUnitProperty_MaximumFramesPerSlice) {
        unit->maximum = *(const UInt32 *)data;
    } else if (id == kAudioUnitProperty_ShouldAllocateBuffer) {
        CHECK(element == 1 && scope == kAudioUnitScope_Output && !*(const UInt32 *)data);
    }
    return noErr;
}
OSStatus AudioUnitGetProperty(AudioUnit unit, AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, void *data, UInt32 *size) {
    if (fail()) return -100;
    if (id == kAudioUnitProperty_StreamFormat) {
        CHECK(element == (unit->capture ? 1u : 0u));
        CHECK(scope == (unit->capture ? kAudioUnitScope_Input : kAudioUnitScope_Output));
        *(AudioStreamBasicDescription *)data = (AudioStreamBasicDescription){.mSampleRate = device_rate, .mChannelsPerFrame = device_channels};
    } else if (id == kAudioUnitProperty_MaximumFramesPerSlice)
        *(UInt32 *)data = unit->maximum;
    else if (id == kAudioUnitProperty_Latency)
        *(Float64 *)data = 0.01;
    else
        return -1;
    return noErr;
}
OSStatus AudioUnitInitialize(AudioUnit unit) {
    if (fail()) return -100;
    init_count++;
    return noErr;
}
OSStatus AudioUnitUninitialize(AudioUnit unit) {
    uninit_count++;
    return noErr;
}
OSStatus AudioOutputUnitStart(AudioUnit unit) { return fail() ? -100 : noErr; }
OSStatus AudioOutputUnitStop(AudioUnit unit) { return fail() ? -100 : noErr; }
OSStatus AudioUnitRender(AudioUnit unit, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus, UInt32 frames, AudioBufferList *data) {
    CHECK(bus == 1 && data->mNumberBuffers == 1);
    CHECK(data->mBuffers[0].mDataByteSize == frames * unit->format.mBytesPerFrame);
    if (render_error) return -100;
    memset(data->mBuffers[0].mData, 255, data->mBuffers[0].mDataByteSize);
    if (silent) *flags |= kAudioUnitRenderAction_OutputIsSilence;
    return noErr;
}
OSStatus AudioObjectGetPropertyDataSize(AudioObjectID object, const AudioObjectPropertyAddress *address, UInt32 qsize, const void *q, UInt32 *size) {
    *size = address->mSelector == kAudioHardwarePropertyDevices ? sizeof(AudioDeviceID) : sizeof(AudioBufferList);
    return noErr;
}
OSStatus AudioObjectGetPropertyData(AudioObjectID object, const AudioObjectPropertyAddress *address, UInt32 qsize, const void *q, UInt32 *size, void *data) {
    switch (address->mSelector) {
    case kAudioHardwarePropertyDevices:
    case kAudioHardwarePropertyDefaultOutputDevice:
    case kAudioHardwarePropertyDefaultInputDevice:
        *(AudioDeviceID *)data = 42;
        break;
    case kAudioObjectPropertyName:
        *(CFStringRef *)data = "USB Audio";
        break;
    case kAudioDevicePropertyDeviceUID:
        *(CFStringRef *)data = "test:usb";
        break;
    case kAudioDevicePropertyStreamConfiguration:
        *(AudioBufferList *)data = (AudioBufferList){1, {{2, 0, nullptr}}};
        break;
    case kAudioDevicePropertyNominalSampleRate:
        *(Float64 *)data = device_rate;
        break;
    case kAudioDevicePropertyBufferFrameSize:
        *(UInt32 *)data = 128;
        break;
    case kAudioDevicePropertyBufferFrameSizeRange:
        *(AudioValueRange *)data = (AudioValueRange){32, 1024};
        break;
    case kAudioDevicePropertyDeviceIsAlive:
        *(UInt32 *)data = alive;
        break;
    default:
        return -1;
    }
    return noErr;
}
OSStatus AudioObjectAddPropertyListener(AudioObjectID object, const AudioObjectPropertyAddress *address, AudioObjectPropertyListenerProc proc, void *user) {
    listeners++;
    return noErr;
}
OSStatus AudioObjectRemovePropertyListener(AudioObjectID object, const AudioObjectPropertyAddress *address, AudioObjectPropertyListenerProc proc, void *user) {
    listeners--;
    return noErr;
}
CFIndex CFStringGetLength(CFStringRef value) { return (CFIndex)strlen(value); }
CFIndex CFStringGetMaximumSizeForEncoding(CFIndex size, CFStringEncoding encoding) { return size; }
bool CFStringGetCString(CFStringRef value, char *dst, CFIndex size, CFStringEncoding encoding) {
    snprintf(dst, (size_t)size, "%s", value);
    return true;
}
void CFRelease(const void *value) {}

static void process(void *user, void *data, uint64_t frames) {
    lnd_stream **s = user;
    CHECK(frames == 7);
    size_t bytes = (size_t)frames * (*s)->cfg.channels * lnd_format_bytes((*s)->cfg.format);
    if ((*s)->capture && silent) {
        unsigned char value = (*s)->cfg.format == LND_FORMAT_U8 ? 128 : 0;
        for (size_t i = 0; i < bytes; i++)
            CHECK(((unsigned char *)data)[i] == value);
    } else
        memset(data, 0x3f, bytes);
    callbacks++;
}
int main(void) {
    lnd_backend b = {.vt = &lnd_backend_coreaudio_vt};
    CHECK(b.vt->init(&b) == LND_OK);
    lnd_device_list devices = {0};
    CHECK(b.vt->enumerate(&b, LND_DEVICE_OUTPUT, &devices) == LND_OK && devices.count == 1);
    lnd_device *d = devices.items[0];
    CHECK(d->flags & LND_DEVICE_FLAG_DEFAULT);
    for (unsigned capture = 0; capture < 2; capture++) {
        for (int32_t format = LND_FORMAT_U8; format <= LND_FORMAT_F64; format++) {
            lnd_stream_cfg cfg = {.format = format, .periods = 2};
            lnd_stream *s = nullptr;
            calls = 0;
            CHECK((capture ? b.vt->open_capture : b.vt->open)(&b, d, &cfg, process, &s, &s) == LND_OK);
            CHECK(units == 1 && cfg.channels == 2 && cfg.sample_rate_hz == 48000);
            CHECK(b.vt->start(s) == LND_OK);
            unsigned char data[128];
            memset(data, 0xa5, sizeof data);
            AudioBufferList buffers = {1, {{2, 7 * 2 * lnd_format_bytes(format), data}}};
            AudioUnitRenderActionFlags flags = 0;
            AudioTimeStamp time = {0};
            silent = capture;
            unsigned before = callbacks;
            CHECK(s->unit->callback.inputProc(s, &flags, &time, capture ? 1 : 0, 7, capture ? nullptr : &buffers) == noErr);
            CHECK(callbacks == before + 1);
            if (!capture) {
                CHECK(data[buffers.mBuffers[0].mDataByteSize] == 0xa5);
                buffers.mBuffers[0].mDataByteSize--;
                CHECK(lnd_apple_render(s, &flags, &time, 0, 7, &buffers) != noErr);
            } else {
                CHECK(lnd_apple_render(s, &flags, &time, 1, s->max_frames + 1, nullptr) == kAudioUnitErr_TooManyFramesToProcess);
            }
            CHECK(b.vt->status(s) == LND_ERR_EXTERNAL && callbacks == before + 1);
            b.vt->close(s);
            CHECK(units == 0 && init_count == uninit_count);
        }
    }
    CHECK(input_enabled == 6 && output_disabled == 6);
    for (unsigned capture = 0; capture < 2; capture++) {
        for (unsigned point = 1; point < 18; point++) {
            lnd_stream_cfg cfg = {.periods = 2};
            lnd_stream *s = nullptr;
            calls = 0;
            fail_at = point;
            int r = (capture ? b.vt->open_capture : b.vt->open)(&b, d, &cfg, process, &s, &s);
            if (r == LND_OK) b.vt->close(s);
            CHECK(units == 0 && init_count == uninit_count);
        }
    }
    fail_at = 0;
    lnd_stream_cfg cfg = {.format = LND_FORMAT_U8};
    lnd_stream *s = nullptr;
    CHECK(b.vt->open(&b, d, &cfg, process, &s, &s) == LND_OK);
    CHECK(b.vt->start(s) == LND_OK);
    LND_CoreAudioSetSessionActive(false);
    CHECK(b.vt->status(s) == LND_ERR_EXTERNAL);
    unsigned char data[14] = {0};
    AudioBufferList buffers = {1, {{2, sizeof data, data}}};
    AudioUnitRenderActionFlags flags = 0;
    AudioTimeStamp time = {0};
    CHECK(lnd_apple_render(s, &flags, &time, 0, 7, &buffers) == noErr);
    CHECK((flags & kAudioUnitRenderAction_OutputIsSilence) && data[0] == 128 && data[13] == 128);
    LND_CoreAudioSetSessionActive(true);
    CHECK(b.vt->status(s) == LND_ERR_EXTERNAL);
    CHECK(b.vt->poll(&b) & LND_BACKEND_EVENT_DEFAULT_OUTPUT);
    b.vt->close(s);
    cfg = (lnd_stream_cfg){.exclusive = true};
    CHECK(b.vt->open(&b, d, &cfg, process, &s, &s) == LND_ERR_UNSUPPORTED);
    cfg = (lnd_stream_cfg){.loopback = true};
    CHECK(b.vt->open_capture(&b, d, &cfg, process, &s, &s) == LND_ERR_UNSUPPORTED);
#if !LND_TEST_MACOS
    LND_IOS_SESSION_CONFIG session_config = {.category = LND_IOS_CATEGORY_PLAY_AND_RECORD, .recording = LND_IOS_RECORDING_STEREO};
    CHECK(LND_IosSessionSetConfig(&session_config) == LND_OK);
    device_channels = 1;
    cfg = (lnd_stream_cfg){0};
    CHECK(b.vt->open_capture(&b, d, &cfg, process, &s, &s) == LND_ERR_UNSUPPORTED);
    CHECK(!units && !lnd_ios_streams[1] && !lnd_ios_activated);
    CHECK(LND_IosSessionSetConfig(nullptr) == LND_OK);
    CHECK(b.vt->open_capture(&b, d, &cfg, process, &s, &s) == LND_OK);
    CHECK(cfg.channels == 1);
    b.vt->close(s);
#endif
    lnd_device_list_free(&devices);
    b.vt->free(&b);
    CHECK(units == 0 && listeners == 0);
#if !LND_TEST_MACOS
    CHECK(!lnd_ios_streams[0] && !lnd_ios_streams[1] && !lnd_ios_activated);
    lnd_ios_free();
#endif
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
