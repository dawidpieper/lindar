#include "io/devices/aaudio/aaudio.c"
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, builders, streams, callbacks, preset_calls, lookups;
static int api_level = 28, preset_missing;
static LND_CONFIG_KEY recording_profile;
LND_CONFIG_KEY *const LND_CFG_ANDROID_RECORDING_PROFILE = &recording_profile;
static int fail_create, fail_open, fail_stop, bad_channels, deny_exclusive;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%d: %s\n", __LINE__, #x);                                                                                                                  \
        }                                                                                                                                                      \
    } while (0)

struct AAudioStreamBuilder {
    int32_t direction, id, sharing, performance, format, sample_rate_hz, channels, period, preset;
    AAudioStream_dataCallback data;
    AAudioStream_errorCallback error;
    void *data_user, *error_user;
};
struct AAudioStream {
    AAudioStreamBuilder cfg;
    int32_t size, state;
};
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **b) {
    if (fail_create) return AAUDIO_ERROR_NO_MEMORY;
    *b = calloc(1, sizeof **b);
    builders++;
    return AAUDIO_OK;
}
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *b) {
    free(b);
    builders--;
    return AAUDIO_OK;
}
#define SETTER(name, field)                                                                                                                                    \
    void AAudioStreamBuilder_set##name(AAudioStreamBuilder *b, int32_t v) { b->field = v; }
SETTER(Direction, direction)
SETTER(DeviceId, id)
SETTER(SharingMode, sharing)
SETTER(PerformanceMode, performance)
SETTER(Format, format)
SETTER(SampleRate, sample_rate_hz)
SETTER(ChannelCount, channels)
SETTER(FramesPerDataCallback, period)
int android_get_device_api_level(void) { return api_level; }
static void set_preset(AAudioStreamBuilder *builder, int32_t preset) {
    preset_calls++;
    builder->preset = preset;
}
void *dlsym(void *handle, const char *symbol) {
    CHECK(handle == RTLD_DEFAULT && !strcmp(symbol, "AAudioStreamBuilder_setInputPreset"));
    lookups++;
    return preset_missing ? nullptr : (void *)set_preset;
}

void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b, AAudioStream_dataCallback cb, void *user) {
    b->data = cb;
    b->data_user = user;
}
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b, AAudioStream_errorCallback cb, void *user) {
    b->error = cb;
    b->error_user = user;
}
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b, AAudioStream **s) {
    if (fail_open) return AAUDIO_ERROR_UNAVAILABLE;
    *s = calloc(1, sizeof **s);
    (*s)->cfg = *b;
    if (!b->sample_rate_hz) (*s)->cfg.sample_rate_hz = 48000;
    if (!b->channels) (*s)->cfg.channels = 2;
    (*s)->size = 192;
    streams++;
    return AAUDIO_OK;
}
aaudio_result_t AAudioStream_close(AAudioStream *s) {
    free(s);
    streams--;
    return AAUDIO_OK;
}
int32_t AAudioStream_getSharingMode(AAudioStream *s) { return deny_exclusive ? AAUDIO_SHARING_MODE_SHARED : s->cfg.sharing; }
int32_t AAudioStream_getSampleRate(AAudioStream *s) { return s->cfg.sample_rate_hz; }
int32_t AAudioStream_getChannelCount(AAudioStream *s) { return bad_channels ? 99 : s->cfg.channels; }
int32_t AAudioStream_getFramesPerBurst(AAudioStream *s) { return 192; }
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream *s) { return 768; }
aaudio_format_t AAudioStream_getFormat(AAudioStream *s) { return s->cfg.format; }
int32_t AAudioStream_setBufferSizeInFrames(AAudioStream *s, int32_t n) { return s->size = n; }
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *s) { return s->size; }
aaudio_result_t AAudioStream_requestStart(AAudioStream *s) {
    s->state = AAUDIO_STREAM_STATE_STARTED;
    return AAUDIO_OK;
}
aaudio_result_t AAudioStream_requestStop(AAudioStream *s) {
    s->state = AAUDIO_STREAM_STATE_STOPPING;
    return AAUDIO_OK;
}
aaudio_stream_state_t AAudioStream_getState(AAudioStream *s) { return s->state; }
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *s, aaudio_stream_state_t before, aaudio_stream_state_t *after, int64_t timeout) {
    if (fail_stop) return AAUDIO_ERROR_INTERNAL;
    *after = s->state = AAUDIO_STREAM_STATE_STOPPED;
    return AAUDIO_OK;
}
static void process(void *user, void *data, uint64_t frames) {
    CHECK(user == &callbacks);
    CHECK(data != nullptr && frames == 7);
    callbacks++;
}

int main(void) {
    lnd_backend b = {.vt = &lnd_backend_aaudio_vt};
    CHECK(b.vt->init(&b) == LND_OK);
    lnd_device_list devices = {0};
    CHECK(b.vt->enumerate(&b, LND_DEVICE_OUTPUT, &devices) == LND_OK);
    CHECK(devices.count == 1 && (devices.items[0]->flags & LND_DEVICE_FLAG_DEFAULT));
    lnd_device *d = devices.items[0];
    lnd_stream_cfg cfg = {.periods = 2};
    lnd_stream *s = nullptr;
    CHECK(b.vt->open(&b, d, &cfg, process, &callbacks, &s) == LND_OK);
    CHECK(cfg.sample_rate_hz == 48000 && cfg.channels == 2 && cfg.buffer_frames == 384);
    CHECK(s && builders == 0 && streams == 1);
    CHECK(b.vt->start(s) == LND_OK && b.vt->start(s) == LND_OK);
    float data[14] = {0};
    CHECK(s->stream->cfg.data(s->stream, s, data, 7) == AAUDIO_CALLBACK_RESULT_CONTINUE && callbacks == 1);
    CHECK(s->stream->cfg.data(s->stream, s, data, 0) == AAUDIO_CALLBACK_RESULT_CONTINUE && callbacks == 1);
    s->stream->cfg.error(s->stream, s, AAUDIO_ERROR_DISCONNECTED);
    CHECK(b.vt->status(s) == LND_ERR_EXTERNAL);
    CHECK(s->stream->cfg.data(s->stream, s, data, 7) == AAUDIO_CALLBACK_RESULT_STOP && callbacks == 1);
    CHECK(b.vt->poll(&b) & LND_BACKEND_EVENT_DEVICES);
    b.vt->close(s);
    CHECK(streams == 0);
    cfg = (lnd_stream_cfg){.format = LND_FORMAT_S16, .channels = 1, .sample_rate_hz = 44100, .period_frames = 64, .periods = 3};
    CHECK(b.vt->open_capture(&b, d, &cfg, process, &callbacks, &s) == LND_OK);
    CHECK(s->stream->cfg.direction == AAUDIO_DIRECTION_INPUT && cfg.format == LND_FORMAT_S16 && cfg.buffer_frames == 192);
    CHECK(b.vt->start(s) == LND_OK);
    LND_AaudioSetActive(false);
    CHECK(b.vt->status(s) == LND_ERR_EXTERNAL);
    CHECK(s->stream->cfg.data(s->stream, s, data, 7) == AAUDIO_CALLBACK_RESULT_STOP);
    LND_AaudioSetActive(true);
    CHECK(b.vt->status(s) == LND_ERR_EXTERNAL);
    fail_stop = 1;
    CHECK(b.vt->stop(s) != LND_OK && s->stream == nullptr && streams == 0);
    b.vt->close(s);
    fail_stop = 0;
    cfg = (lnd_stream_cfg){.periods = 2};
    fail_create = 1;
    CHECK(b.vt->open(&b, d, &cfg, process, &callbacks, &s) == LND_ERR_OUT_OF_MEMORY);
    fail_create = 0;
    fail_open = 1;
    CHECK(b.vt->open(&b, d, &cfg, process, &callbacks, &s) == LND_ERR_NO_DEVICE);
    fail_open = 0;
    bad_channels = 1;
    CHECK(b.vt->open(&b, d, &cfg, process, &callbacks, &s) == LND_ERR_FORMAT);
    bad_channels = 0;
    cfg.exclusive = true;
    deny_exclusive = 1;
    CHECK(b.vt->open(&b, d, &cfg, process, &callbacks, &s) == LND_ERR_NO_DEVICE);
    cfg.exclusive = false;
    cfg.loopback = true;
    CHECK(b.vt->open(&b, d, &cfg, process, &callbacks, &s) == LND_ERR_UNSUPPORTED);
    cfg.loopback = false;
    cfg.format = LND_FORMAT_S24;
    CHECK(b.vt->open(&b, d, &cfg, process, &callbacks, &s) == LND_ERR_FORMAT);
    CHECK(builders == 0 && streams == 0);
    CHECK(preset_calls == 0 && lookups == 0);
    cfg = (lnd_stream_cfg){.periods = 2};
    const int32_t presets[] = {0, 1, 5, 6, 7, 9, 10};
    for (uint32_t profile = 0; profile <= LND_ANDROID_RECORDING_VOICE_PERFORMANCE; profile++) {
        api_level = 29;
        lnd_store(&recording_profile.value, profile);
        CHECK(b.vt->open_capture(&b, d, &cfg, process, &callbacks, &s) == LND_OK);
        CHECK(s->stream->cfg.preset == presets[profile]);
        b.vt->close(s);
    }
    CHECK(preset_calls == 6 && lookups == 6);
    api_level = 28;
    CHECK(b.vt->open_capture(&b, d, &cfg, process, &callbacks, &s) == LND_ERR_UNSUPPORTED);
    lnd_store(&recording_profile.value, LND_ANDROID_RECORDING_MICROPHONE);
    api_level = 26;
    CHECK(b.vt->open_capture(&b, d, &cfg, process, &callbacks, &s) == LND_ERR_UNSUPPORTED);
    CHECK(b.vt->open(&b, d, &cfg, process, &callbacks, &s) == LND_OK);
    CHECK(!s->stream->cfg.preset);
    b.vt->close(s);
    CHECK(lookups == 6);
    api_level = 28;
    preset_missing = 1;
    CHECK(b.vt->open_capture(&b, d, &cfg, process, &callbacks, &s) == LND_ERR_UNSUPPORTED);
    CHECK(streams == 0 && builders == 0);
    lnd_store(&recording_profile.value, LND_ANDROID_RECORDING_DEFAULT);
    CHECK(b.vt->open_capture(&b, d, &cfg, process, &callbacks, &s) == LND_OK);
    b.vt->close(s);
    CHECK(lookups == 7);
    lnd_device_ctx.backend = b;
    lnd_device_ctx.backend_ready = true;
    LND_AAUDIO_DEVICE list[] = {{7, LND_DEVICE_OUTPUT, "USB"}, {7, LND_DEVICE_INPUT, "USB microphone"}};
    CHECK(LND_AaudioSetDevices(list, 2) == LND_OK);
    lnd_device_list extra = {0};
    CHECK(b.vt->enumerate(&b, LND_DEVICE_INPUT, &extra) == LND_OK && extra.count == 2);
    CHECK((uintptr_t)extra.items[1]->backend_data == 7);
    CHECK(LND_AaudioSetDevices(nullptr, 1) == LND_ERR_INVALID_ARG);
    list[1] = list[0];
    CHECK(LND_AaudioSetDevices(list, 2) == LND_ERR_INVALID_ARG);
    CHECK(((lnd_android *)b.data)->devices[LND_DEVICE_INPUT].count == 1);
    CHECK(LND_AaudioSetDevices(nullptr, 0) == LND_OK);
    lnd_device_ctx.backend_ready = false;
    lnd_device_list_free(&extra);
    lnd_device_list_free(&devices);
    b.vt->free(&b);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
