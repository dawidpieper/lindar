#include "lindar_null.h"
#include "lindar_devices.h"
#include "lindar_graph.h"
#include "lindar_output.h"
#include "lindar.h"
#include "src/atomic.h"
#include "src/context.h"
#include "src/thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lnd_atomic_u32 checks, failures;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        lnd_add(&checks, 1);                                                                                                                                   \
        if (!(x)) {                                                                                                                                            \
            lnd_add(&failures, 1);                                                                                                                             \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

typedef struct callback_state {
    lnd_atomic_u32 entered;
    lnd_atomic_u32 release;
    lnd_atomic_u32 done;
    LND_OUTPUT *output;
    LND_DEVICE_INSTANCE *instance;
    LND_NODE *node;
    bool gate;
} callback_state;

static void probe(callback_state *state) {
    if (lnd_exchange(&state->entered, 1)) return;
    while (state->gate && !lnd_load(&state->release))
        lnd_sleep_ms(1);
    CHECK(LND_LibraryInit() == LND_ERR_BUSY);
    CHECK(LND_LibraryUpdate() == LND_ERR_BUSY);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_CHANNELS, 1) == LND_ERR_BUSY);
    CHECK(LND_DeviceSetPreferredBackend(nullptr) == LND_ERR_BUSY);
    CHECK(LND_ConfigGet(LND_CFG_GRAPH_CHANNELS) == 2);
    CHECK(LND_AllocatorSetConfig(nullptr) == LND_ERR_BUSY);
    CHECK(LND_NullSetOutputCallback(nullptr, nullptr) == LND_ERR_BUSY);
    CHECK(LND_DeviceSetEventCallback(nullptr, nullptr) == LND_ERR_BUSY);
    CHECK(LND_NodeCreateBus(2, 48000) == nullptr && LND_ErrorGetLast() == LND_ERR_BUSY);
    LND_LibraryFree();
    CHECK(LND_ErrorGetLast() == LND_ERR_BUSY);
    if (state->node) {
        CHECK(LND_NodeGetSampleRateHz(state->node) == 48000);
        CHECK(LND_NodeGetChannels(state->node) == 2);
        CHECK(LND_NodeGetInputCount(state->node) == 0);
        CHECK(LND_NodeGetOutputCount(state->node) == 1);
        CHECK(LND_NodeGetGain(state->node) == 1.0f);
        CHECK(LND_NodeGetClipMode(state->node) == LND_CLIP_NONE);
        CHECK(LND_NodeGetPositionFrames(state->node) == 0);
    }
    if (state->instance) {
        CHECK(LND_DeviceGetOutputInstance() == state->instance);
        CHECK(LND_DeviceInstanceGetDevice(state->instance) != nullptr);
        CHECK(LND_DeviceInstanceGetSampleRateHz(state->instance) == 48000);
        CHECK(LND_DeviceInstanceGetChannels(state->instance) == 2);
        CHECK(LND_DeviceInstanceGetFormat(state->instance) == LND_FORMAT_F32);
        CHECK(LND_DeviceInstanceGetPeriodFrames(state->instance) > 0);
        CHECK(LND_DeviceInstanceGetBufferFrames(state->instance) >= LND_DeviceInstanceGetPeriodFrames(state->instance));
        CHECK(LND_DeviceInstanceIsRunning(state->instance));
    }
    if (state->output) {
        CHECK(LND_OutputFlush(state->output) == LND_ERR_BUSY);
        CHECK(LND_OutputFree(state->output) == LND_ERR_BUSY);
        float frame[2] = {0};
        CHECK(LND_OutputWrite(state->output, frame, LND_FORMAT_F32, 1) == LND_ERR_BUSY);
    }
    lnd_store(&state->done, 1);
}

static int64_t read_source(void *user, void *dst, uint64_t frames) {
    probe(user);
    memset(dst, 0, (size_t)frames * 2 * sizeof(float));
    return frames;
}

static void sink(void *user, const void *src, uint64_t frames) { probe(user); }

static size_t write_output(void *user, const void *src, size_t size) {
    probe(user);
    return size;
}
static int32_t seek_output(void *user, uint64_t position) {
    probe(user);
    return LND_OK;
}

static void begin(void) {
    CHECK(LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("null")) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_CHANNELS, 2) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_SAMPLE_RATE_HZ, 48000) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
}

static void test_thread(bool use_sink) {
    callback_state state = {.gate = true};
    if (use_sink) {
        CHECK(LND_NullSetOutputCallback(sink, &state) == LND_OK);
    }
    begin();
    LND_SOURCE_PROCS procs = {.read = read_source};
    callback_state quiet = {.entered = 1};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, use_sink ? &quiet : &state, LND_FORMAT_F32, 2, 48000, LND_GRAPH_SOURCE_DIRECT);
    CHECK(source != nullptr);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(sound != nullptr);
    if (!use_sink) state.node = LND_SourceEnsureNode(source);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 1);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    for (unsigned n = 0; n < 1000 && !lnd_load(&state.entered); n++)
        lnd_sleep_ms(1);
    CHECK(lnd_load(&state.entered) != 0);
    state.instance = LND_DeviceEnsureOutputInstance();
    CHECK(state.instance != nullptr);
    lnd_context_lock();
    lnd_store(&state.release, 1);
    for (unsigned n = 0; n < 1000 && !lnd_load(&state.done); n++)
        lnd_sleep_ms(1);
    CHECK(lnd_load(&state.done) != 0);
    lnd_context_unlock();
    LND_LibraryFree();
}

static void test_stream(void) {
    begin();
    callback_state state = {0};
    LND_SOURCE_PROCS procs = {.read = read_source};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &state, LND_FORMAT_F32, 2, 48000, 0);
    CHECK(source != nullptr && lnd_load(&state.done));
    CHECK(LND_SourceFree(source) == LND_OK);
    LND_LibraryFree();
}

static void test_output(void) {
    begin();
    callback_state state = {0};
    LND_IO_OUTPUT_PROCS procs = {.write = write_output, .seek = seek_output};
    LND_ENCODER_PARAMS params = {.encoder_name = "wav", .sample_rate_hz = 48000, .channels = 2, .format = LND_FORMAT_S16};
    LND_OUTPUT *output = LND_OutputCreateProc(&procs, &state, &params);
    CHECK(output && lnd_load(&state.done));
    state.output = output;
    lnd_store(&state.entered, 0);
    lnd_store(&state.done, 0);
    float frames[64] = {0};
    CHECK(LND_OutputWrite(output, frames, LND_FORMAT_F32, 32) == LND_OK);
    CHECK(lnd_load(&state.done));
    CHECK(LND_OutputFree(output) == LND_OK);
    LND_LibraryFree();
}

int main(void) {
    test_thread(false);
    test_thread(true);
    test_stream();
    test_output();
    printf("%u checks, %u failures\n", lnd_load(&checks), lnd_load(&failures));
    return lnd_load(&failures) ? 1 : 0;
}
