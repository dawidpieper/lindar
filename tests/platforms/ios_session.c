#include "io/devices/ios/session.c"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures, config_calls, active_calls, input_calls, orientation_calls, notifications;
static int32_t config_error, active_error, input_error, orientation_error, applied_orientation, applied_recording;
static bool active_value;
static LND_IOS_SESSION_CONFIG applied_config;

#define CHECK(expr)                                                                                                                                            \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(expr)) {                                                                                                                                         \
            failures++;                                                                                                                                        \
            printf("%d: %s\n", __LINE__, #expr);                                                                                                               \
        }                                                                                                                                                      \
    } while (0)

int32_t lnd_ios_native_configure(const LND_IOS_SESSION_CONFIG *config) {
    config_calls++;
    applied_config = *config;
    return config_error;
}
int32_t lnd_ios_native_active(bool active) {
    active_calls++;
    active_value = active;
    return active_error;
}
int32_t lnd_ios_native_input(int32_t recording, int32_t orientation) {
    input_calls++;
    applied_recording = recording;
    applied_orientation = orientation;
    return input_error;
}
int32_t lnd_ios_native_orientation(int32_t orientation) {
    orientation_calls++;
    applied_orientation = orientation;
    return orientation_error;
}
void LND_CoreAudioSetSessionActive(bool active) { notifications++; }

int main(void) {
    LND_IOS_SESSION_CONFIG config;
    CHECK(LND_IosSessionGetConfig(&config) == LND_OK);
    CHECK(!config.category && !config.mode && !config.flags && !config.options && !config.recording && !config.orientation);
    CHECK(LND_IosSessionGetConfig(nullptr) == LND_ERR_INVALID_ARG);
    CHECK(LND_IosGetDeviceOrientation() == LND_IOS_ORIENTATION_DEFAULT);
    CHECK(lnd_ios_session_acquire(false) == LND_OK);
    CHECK(config_calls == 1 && active_calls == 1 && active_value && input_calls == 0);
    CHECK(lnd_ios_session_acquire(true) == LND_OK);
    CHECK(active_calls == 1 && input_calls == 1 && !applied_recording && !applied_orientation);
    CHECK(LND_IosSessionSetConfig(nullptr) == LND_ERR_BUSY);
    lnd_ios_session_release(false);
    CHECK(active_calls == 1);
    lnd_ios_session_release(true);
    CHECK(active_calls == 2 && !active_value);
    config = (LND_IOS_SESSION_CONFIG){.category = LND_IOS_CATEGORY_PLAY_AND_RECORD,
                                      .mode = LND_IOS_MODE_MEASUREMENT,
                                      .options = LND_IOS_SESSION_MIX_WITH_OTHERS,
                                      .sample_rate_hz = 48000,
                                      .buffer_duration_us = 10000,
                                      .recording = LND_IOS_RECORDING_STEREO,
                                      .orientation = LND_IOS_ORIENTATION_AUTO};
    CHECK(LND_IosSessionSetConfig(&config) == LND_OK && config_calls == 1);
    CHECK(LND_IosSetDeviceOrientation(LND_IOS_ORIENTATION_LANDSCAPE_LEFT) == LND_OK && !orientation_calls);
    CHECK(lnd_ios_session_acquire(true) == LND_OK);
    CHECK(applied_config.sample_rate_hz == 48000 && applied_config.buffer_duration_us == 10000);
    CHECK(applied_recording == LND_IOS_RECORDING_STEREO && applied_orientation == LND_IOS_ORIENTATION_LANDSCAPE_LEFT);
    CHECK(LND_IosSetDeviceOrientation(LND_IOS_ORIENTATION_PORTRAIT) == LND_OK && orientation_calls == 1);
    CHECK(LND_IosSetDeviceOrientation(LND_IOS_ORIENTATION_PORTRAIT) == LND_OK && orientation_calls == 1);
    CHECK(LND_IosSetDeviceOrientation(LND_IOS_ORIENTATION_DEFAULT) == LND_OK && orientation_calls == 1);
    CHECK(LND_IosGetDeviceOrientation() == LND_IOS_ORIENTATION_PORTRAIT);
    orientation_error = LND_ERR_UNSUPPORTED;
    CHECK(LND_IosSetDeviceOrientation(LND_IOS_ORIENTATION_LANDSCAPE_RIGHT) == LND_ERR_UNSUPPORTED);
    CHECK(LND_IosGetDeviceOrientation() == LND_IOS_ORIENTATION_PORTRAIT);
    orientation_error = 0;
    CHECK(LND_IosSetDeviceOrientation(LND_IOS_ORIENTATION_LANDSCAPE_RIGHT) == LND_OK);
    CHECK(LND_IosSetDeviceOrientation(LND_IOS_ORIENTATION_AUTO) == LND_ERR_INVALID_ARG);
    CHECK(LND_IosSetDeviceOrientation(-1) == LND_ERR_INVALID_ARG);
    lnd_ios_session_release(true);
    config.orientation = LND_IOS_ORIENTATION_PORTRAIT_UPSIDE_DOWN;
    CHECK(LND_IosSessionSetConfig(&config) == LND_OK);
    CHECK(lnd_ios_session_acquire(true) == LND_OK);
    CHECK(applied_orientation == LND_IOS_ORIENTATION_PORTRAIT_UPSIDE_DOWN);
    unsigned before = orientation_calls;
    CHECK(LND_IosSetDeviceOrientation(LND_IOS_ORIENTATION_PORTRAIT) == LND_OK && orientation_calls == before);
    lnd_ios_session_release(true);
    config.flags = LND_IOS_SESSION_MANUAL;
    CHECK(LND_IosSessionSetConfig(&config) == LND_OK);
    unsigned before_config = config_calls, before_active = active_calls, before_input = input_calls;
    CHECK(lnd_ios_session_acquire(false) == LND_OK && lnd_ios_session_acquire(true) == LND_OK);
    CHECK(LND_IosSetDeviceOrientation(LND_IOS_ORIENTATION_LANDSCAPE_LEFT) == LND_OK);
    lnd_ios_session_release(true);
    lnd_ios_session_release(false);
    CHECK(config_calls == before_config && active_calls == before_active && input_calls == before_input && orientation_calls == before);
    CHECK(LND_IosSessionSetActive(true) == LND_OK && notifications == 1);
    CHECK(LND_IosSessionApply() == LND_OK && config_calls == before_config + 1 && input_calls == before_input);
    CHECK(LND_IosSessionApplyInput() == LND_OK && input_calls == before_input + 1);
    CHECK(LND_IosSessionSetActive(false) == LND_OK && notifications == 2);
    config.flags = 0;
    CHECK(LND_IosSessionSetConfig(&config) == LND_OK);
    config_error = LND_ERR_EXTERNAL;
    before = active_calls;
    CHECK(lnd_ios_session_acquire(true) == LND_ERR_EXTERNAL && active_calls == before);
    config_error = 0;
    active_error = LND_ERR_EXTERNAL;
    CHECK(lnd_ios_session_acquire(true) == LND_ERR_EXTERNAL);
    active_error = 0;
    input_error = LND_ERR_UNSUPPORTED;
    before = active_calls;
    CHECK(lnd_ios_session_acquire(true) == LND_ERR_UNSUPPORTED && active_calls == before + 2 && !active_value);
    CHECK(!lnd_ios_streams[0] && !lnd_ios_streams[1] && !lnd_ios_activated);
    CHECK(lnd_ios_session_acquire(false) == LND_OK);
    before = active_calls;
    CHECK(lnd_ios_session_acquire(true) == LND_ERR_UNSUPPORTED && active_calls == before);
    CHECK(lnd_ios_streams[0] == 1 && !lnd_ios_streams[1] && lnd_ios_activated);
    lnd_ios_session_release(false);
    input_error = 0;
    CHECK(lnd_ios_session_acquire(false) == LND_OK);
    active_error = LND_ERR_EXTERNAL;
    lnd_ios_session_release(false);
    CHECK(lnd_ios_activated && !lnd_ios_streams[0]);
    CHECK(LND_IosSessionSetConfig(nullptr) == LND_ERR_BUSY);
    active_error = 0;
    CHECK(LND_IosSessionSetActive(false) == LND_OK && !lnd_ios_activated);
    config.category = -1;
    CHECK(LND_IosSessionSetConfig(&config) == LND_ERR_INVALID_ARG);
    config.category = LND_IOS_CATEGORY_PLAY_AND_RECORD;
    config.recording = LND_IOS_RECORDING_MONO;
    CHECK(LND_IosSessionSetConfig(&config) == LND_ERR_INVALID_ARG);
    config.orientation = LND_IOS_ORIENTATION_DEFAULT;
    CHECK(LND_IosSessionSetConfig(&config) == LND_OK);
    CHECK(lnd_ios_session_acquire(true) == LND_OK && applied_recording == LND_IOS_RECORDING_MONO);
    lnd_ios_session_release(true);
    config.options = 0x80000000u;
    CHECK(LND_IosSessionSetConfig(&config) == LND_ERR_INVALID_ARG);
    CHECK(LND_IosSessionSetConfig(nullptr) == LND_OK);
    lnd_ios_free();
    CHECK(LND_IosGetDeviceOrientation() == LND_IOS_ORIENTATION_DEFAULT);
    CHECK(LND_IosSessionGetConfig(&config) == LND_OK && !config.category && !config.recording && !config.flags);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
