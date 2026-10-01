#include "io/devices/asio/asio.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/context.h"
#include "io/devices/context.h"
#include "lindar.h"
#include "lindar_graph.h"
#include "playback/graph/node.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lnd_atomic_u32 checks, failures, loads, unloads, creates, disposes, starts, stops, ready_calls;
static lnd_atomic_i32 fail_load, fail_init, fail_create, fail_start, bad_buffers, bad_format, fail_rate, omit_future, fail_alloc;
static lnd_atomic_u32 callback_allocations;
static thread_local bool mock_audio_callback;
static lnd_atomic_u32 live_allocations;
static lnd_atomic_u32 registered = 2;
static lnd_atomic_u32 hardware_rate = 48000;
static lnd_atomic_u32 hardware_period = 64;
static lnd_atomic_ptr active_drivers[2];

#define CHECK(X)                                                                                                                                               \
    do {                                                                                                                                                       \
        lnd_add(&checks, 1);                                                                                                                                   \
        if (!(X)) {                                                                                                                                            \
            lnd_add(&failures, 1);                                                                                                                             \
            printf("%d: %s\n", __LINE__, #X);                                                                                                                  \
        }                                                                                                                                                      \
    } while (0)

typedef struct mock_driver {
    lnd_asio_abi abi;
    DWORD owner;
    unsigned index;
    uint32_t rate;
    uint32_t frames;
    uint32_t count;
    struct cwASIOBufferInfo *buffers;
    struct cwASIOCallbacks callbacks;
    long clock;
    bool running;
} mock_driver;

static void *test_alloc(void *user, size_t size) {
    LND_UNUSED(user);
    if (mock_audio_callback || (lnd_callback_active() && GetThreadPriority(GetCurrentThread()) == THREAD_PRIORITY_TIME_CRITICAL))
        lnd_add(&callback_allocations, 1);
    int32_t remaining = lnd_load(&fail_alloc);
    if (remaining > 0 && lnd_sub(&fail_alloc, 1) == 1)
        return nullptr;
    void *result = malloc(size);
    if (result)
        lnd_add(&live_allocations, 1);
    return result;
}

static void *test_realloc(void *user, void *memory, size_t size) {
    LND_UNUSED(user);
    int32_t remaining = lnd_load(&fail_alloc);
    if (remaining > 0 && lnd_sub(&fail_alloc, 1) == 1)
        return nullptr;
    void *result = realloc(memory, size);
    if (result && !memory)
        lnd_add(&live_allocations, 1);
    return result;
}

static void test_free(void *user, void *memory) {
    LND_UNUSED(user);
    if (memory)
        lnd_sub(&live_allocations, 1);
    free(memory);
}

static mock_driver *mock(lnd_asio_abi *abi) {
    mock_driver *driver = (mock_driver *)abi;
    CHECK(driver->owner == GetCurrentThreadId());
    return driver;
}

static HRESULT WINAPI mock_query(lnd_asio_abi *abi, const GUID *id, void **out) {
    LND_UNUSED(abi);
    LND_UNUSED(id);
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI mock_ref(lnd_asio_abi *abi) {
    LND_UNUSED(abi);
    return 1;
}
static ULONG WINAPI mock_release(lnd_asio_abi *abi) {
    mock_driver *driver = mock(abi);
    CHECK(!driver->buffers && !driver->running);
    lnd_store(&active_drivers[driver->index], nullptr);
    lnd_add(&unloads, 1);
    free(driver);
    return 0;
}
static cwASIOBool LND_ASIO_METHOD mock_init(lnd_asio_abi *abi, void *window) {
    mock(abi);
    CHECK(window != nullptr);
    return !lnd_load(&fail_init);
}
static void LND_ASIO_METHOD mock_name(lnd_asio_abi *abi, char *name) {
    mock(abi);
    strcpy(name, "Lindar mock ASIO");
}
static long LND_ASIO_METHOD mock_version(lnd_asio_abi *abi) {
    mock(abi);
    return 42;
}
static void LND_ASIO_METHOD mock_error(lnd_asio_abi *abi, char *text) {
    mock(abi);
    strcpy(text, "mock error");
}
static cwASIOError LND_ASIO_METHOD mock_start(lnd_asio_abi *abi) {
    mock_driver *driver = mock(abi);
    CHECK(driver->buffers != nullptr);
    lnd_add(&starts, 1);
    if (lnd_load(&fail_start))
        return ASE_HWMalfunction;
    driver->running = true;
    mock_audio_callback = true;
    driver->callbacks.bufferSwitch(0, ASIOTrue);
    mock_audio_callback = false;
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_stop(lnd_asio_abi *abi) {
    mock_driver *driver = mock(abi);
    driver->running = false;
    lnd_add(&stops, 1);
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_channels(lnd_asio_abi *abi, long *input, long *output) {
    mock(abi);
    *input = 4;
    *output = 6;
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_latencies(lnd_asio_abi *abi, long *input, long *output) {
    mock(abi);
    *input = 91;
    *output = 137;
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_sizes(lnd_asio_abi *abi, long *minimum, long *maximum, long *preferred, long *granularity) {
    mock(abi);
    *minimum = 32;
    *maximum = 512;
    *preferred = lnd_load(&hardware_period);
    *granularity = -1;
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_can_rate(lnd_asio_abi *abi, double rate) {
    mock(abi);
    return rate == 48000 || rate == 96000 ? ASE_OK : ASE_NotPresent;
}
static cwASIOError LND_ASIO_METHOD mock_get_rate(lnd_asio_abi *abi, double *rate) {
    *rate = mock(abi)->rate;
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_set_rate(lnd_asio_abi *abi, double rate) {
    mock_driver *driver = mock(abi);
    if (lnd_load(&fail_rate))
        return ASE_NoClock;
    driver->rate = (uint32_t)rate;
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_clocks(lnd_asio_abi *abi, struct cwASIOClockSource *clocks, long *count) {
    mock_driver *driver = mock(abi);
    CHECK(*count >= 2);
    *count = 2;
    clocks[0] = (struct cwASIOClockSource){.index = 7, .associatedChannel = -1, .associatedGroup = -1, .isCurrentSource = driver->clock == 7};
    clocks[1] = (struct cwASIOClockSource){.index = 19, .associatedChannel = 2, .associatedGroup = 1, .isCurrentSource = driver->clock == 19};
    strcpy(clocks[0].name, "Internal");
    strcpy(clocks[1].name, "External");
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_set_clock(lnd_asio_abi *abi, long clock) {
    mock_driver *driver = mock(abi);
    if (clock != 7 && clock != 19)
        return ASE_InvalidParameter;
    driver->clock = clock;
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_position(lnd_asio_abi *abi, cwASIOSamples *position, cwASIOTimeStamp *time) {
    LND_UNUSED(abi);
    *position = (cwASIOSamples){1, 123};
    *time = (cwASIOTimeStamp){2, 456};
    return ASE_OK;
}
static int mock_type(bool input, long channel) {
    if (lnd_load(&bad_format))
        return ASIOSTDSDInt8LSB1;
    static const int inputs[] = {ASIOSTFloat32LSB, ASIOSTInt24MSB, ASIOSTInt32LSB18, ASIOSTFloat64MSB};
    static const int outputs[] = {ASIOSTInt16LSB, ASIOSTFloat32LSB, ASIOSTFloat64MSB, ASIOSTInt32LSB24, ASIOSTInt24MSB, ASIOSTInt32MSB20};
    return input ? inputs[channel] : outputs[channel];
}
static cwASIOError LND_ASIO_METHOD mock_channel(lnd_asio_abi *abi, struct cwASIOChannelInfo *info) {
    mock_driver *driver = mock(abi);
    if (info->channel < 0 || info->channel >= (info->isInput ? 4 : 6))
        return ASE_InvalidParameter;
    info->type = mock_type(info->isInput, info->channel);
    info->channelGroup = info->channel / 2;
    info->isActive = driver->buffers != nullptr;
    snprintf(info->name, sizeof info->name, "%s %ld", info->isInput ? "Input" : "Output", info->channel);
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_create(lnd_asio_abi *abi, struct cwASIOBufferInfo *buffers, long count, long frames,
                                               const struct cwASIOCallbacks *callbacks) {
    mock_driver *driver = mock(abi);
    CHECK(!driver->buffers);
    lnd_add(&creates, 1);
    driver->buffers = buffers;
    driver->count = (uint32_t)count;
    driver->frames = (uint32_t)frames;
    driver->callbacks = *callbacks;
    CHECK(callbacks->asioMessage(kAsioSelectorSupported, kAsioResetRequest, nullptr, nullptr) == 1);
    CHECK(callbacks->asioMessage(kAsioEngineVersion, 0, nullptr, nullptr) == 2);
    CHECK(callbacks->asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1);
    CHECK(callbacks->asioMessage(kAsioSelectorSupported, 9999, nullptr, nullptr) == 0);
    CHECK(callbacks->asioMessage(kAsioBufferSizeChange, -1, nullptr, nullptr) == 0);
    for (long i = 0; i < count; i++) {
        lnd_asio_pcm pcm;
        CHECK(lnd_asio_pcm_get(mock_type(buffers[i].isInput, buffers[i].channelNum), &pcm));
        for (unsigned half = 0; half < 2; half++) {
            buffers[i].buffers[half] = malloc((size_t)frames * pcm.bytes);
            CHECK(buffers[i].buffers[half] != nullptr);
            memset(buffers[i].buffers[half], 0xa5, (size_t)frames * pcm.bytes);
            if (buffers[i].isInput) {
                float source[512];
                for (long j = 0; j < frames; j++)
                    source[j] = (buffers[i].channelNum + 1) * 0.125f;
                pcm.write(source, buffers[i].buffers[half], (uint32_t)frames, 1);
            }
        }
    }
    if (lnd_load(&bad_buffers)) {
        free(buffers[0].buffers[0]);
        buffers[0].buffers[0] = nullptr;
    }
    return lnd_load(&fail_create) ? ASE_NoMemory : ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_dispose(lnd_asio_abi *abi) {
    mock_driver *driver = mock(abi);
    if (driver->buffers) {
        for (uint32_t i = 0; i < driver->count; i++)
            for (unsigned half = 0; half < 2; half++)
                free(driver->buffers[i].buffers[half]);
        driver->buffers = nullptr;
        lnd_add(&disposes, 1);
    }
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_panel(lnd_asio_abi *abi) {
    mock(abi);
    return ASE_OK;
}
static cwASIOError LND_ASIO_METHOD mock_future(lnd_asio_abi *abi, long selector, void *data) {
    mock(abi);
    if (lnd_load(&omit_future))
        return ASE_NotPresent;
    if (selector == kAsioSetIoFormat) {
        CHECK(((struct cwASIOIoFormat *)data)->FormatType == kASIOPCMFormat);
        return ASE_SUCCESS;
    }
    if (selector == kAsioGetInputMeter || selector == kAsioGetOutputMeter)
        ((struct cwASIOChannelControls *)data)->meter = INT32_MAX / 2;
    if (selector == kAsioSetInputMonitor)
        CHECK(((struct cwASIOInputMonitor *)data)->input >= -1);
    if (selector == kAsioTransport)
        CHECK(((struct cwASIOTransportParameters *)data)->samplePosition.hi == 1);
    return ASE_SUCCESS;
}
static cwASIOError LND_ASIO_METHOD mock_ready(lnd_asio_abi *abi) {
    LND_UNUSED(abi);
    lnd_add(&ready_calls, 1);
    return ASE_OK;
}

static const lnd_asio_vt mock_vt = {
    mock_query,     mock_ref,      mock_release,   mock_init,   mock_name,     mock_version,  mock_error,    mock_start,
    mock_stop,      mock_channels, mock_latencies, mock_sizes,  mock_can_rate, mock_get_rate, mock_set_rate, mock_clocks,
    mock_set_clock, mock_position, mock_channel,   mock_create, mock_dispose,  mock_panel,    mock_future,   mock_ready,
};

int32_t lnd_asio_registry_enumerate(lnd_asio_registration_proc proc, void *user, uint64_t *fingerprint) {
    for (unsigned i = 0; i < lnd_load(&registered); i++) {
        GUID id = {.Data1 = i + 1};
        char key[40], name[32];
        snprintf(key, sizeof key, "{%08x-0000-0000-0000-000000000000}", i + 1);
        snprintf(name, sizeof name, "Mock ASIO %u", i + 1);
        int32_t result = proc ? proc(user, &id, key, name) : LND_OK;
        if (result != LND_OK)
            return result;
    }
    if (fingerprint)
        *fingerprint = lnd_load(&registered) + 1;
    return LND_OK;
}

int32_t lnd_asio_driver_load(const GUID *id, lnd_asio_abi **out) {
    *out = nullptr;
    if (lnd_load(&fail_load))
        return lnd_load(&fail_load) == 2 ? LND_OK : LND_ERR_NO_DEVICE;
    mock_driver *driver = calloc(1, sizeof *driver);
    if (!driver)
        return LND_ERR_OUT_OF_MEMORY;
    driver->abi.vt = &mock_vt;
    driver->owner = GetCurrentThreadId();
    driver->index = id->Data1 - 1;
    driver->rate = lnd_load(&hardware_rate);
    driver->clock = 7;
    CHECK(driver->index < 2 && !lnd_load(&active_drivers[driver->index]));
    lnd_store(&active_drivers[driver->index], driver);
    lnd_add(&loads, 1);
    *out = &driver->abi;
    return LND_OK;
}
void lnd_asio_driver_unload(lnd_asio_abi *driver) { mock_release(driver); }

typedef struct audio_state {
    lnd_atomic_u32 calls;
    lnd_atomic_u32 gate;
    lnd_atomic_u32 entered;
    unsigned channels;
    bool input;
    bool reenter;
} audio_state;

static void audio_proc(void *user, void *memory, uint64_t frames) {
    audio_state *state = user;
    CHECK(lnd_callback_active());
    if (state->reenter) {
        CHECK(LND_LibraryInit() == LND_ERR_BUSY);
        CHECK(LND_DeviceRefresh() == LND_ERR_BUSY);
        CHECK(LND_DeviceResetAsio(nullptr) == LND_ERR_BUSY);
    }
    float *pcm = memory;
    if (state->input) {
        CHECK(fabsf(pcm[0] - 0.125f) < 0.0001f);
        if (state->channels > 1)
            CHECK(fabsf(pcm[1] - 0.25f) < 0.0001f);
    } else {
        for (uint64_t frame = 0; frame < frames; frame++)
            for (unsigned channel = 0; channel < state->channels; channel++)
                pcm[frame * state->channels + channel] = (channel + 1) * 0.0625f;
    }
    lnd_store(&state->entered, 1);
    while (lnd_load(&state->gate))
        Sleep(1);
    lnd_add(&state->calls, 1);
}

static void pump(mock_driver *driver, unsigned index, bool direct) {
    struct cwASIOTime time = {0};
    time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid | kSpeedValid;
    time.timeInfo.systemTime = (cwASIOTimeStamp){3, 10};
    time.timeInfo.samplePosition = (cwASIOSamples){4, index * driver->frames};
    time.timeInfo.sampleRate = driver->rate;
    time.timeInfo.speed = 1;
    time.timeCode.flags = kTcValid | kTcRunning | kTcSpeedValid;
    time.timeCode.timeCodeSamples = (cwASIOSamples){5, 11};
    time.timeCode.speed = 0.5;
    mock_audio_callback = true;
    CHECK(driver->callbacks.bufferSwitchTimeInfo(&time, index, direct) == &time);
    mock_audio_callback = false;
}

static bool wait_calls(audio_state *state, unsigned count) {
    for (unsigned i = 0; i < 1000; i++) {
        if (lnd_load(&state->calls) >= count)
            return true;
        Sleep(1);
    }
    return false;
}

static void test_pcm(void) {
    static const float values[] = {-2, -1, -0.75f, -0.25f, 0, 0.125f, 0.5f, 0.999f, 1, 2, NAN, INFINITY, -INFINITY};
    unsigned supported = 0;
    for (int type = -1; type <= 41; type++) {
        lnd_asio_pcm pcm;
        if (!lnd_asio_pcm_get(type, &pcm))
            continue;
        supported++;
        for (unsigned stride = 1; stride <= 7; stride++) {
            float source[13 * 7] = {0}, decoded[13 * 7];
            uint8_t memory[13 * 8 + 4];
            memset(memory, 0xab, sizeof memory);
            for (unsigned i = 0; i < 13; i++)
                source[i * stride] = values[i];
            for (unsigned i = 0; i < 13 * 7; i++)
                decoded[i] = 17;
            pcm.write(source, memory + 1, 13, stride);
            pcm.read(memory + 1, decoded, 13, stride);
            for (unsigned i = 0; i < 13; i++) {
                double expected = values[i];
                if (!pcm.floating_point) {
                    double scale = ldexp(1.0, (int)pcm.bits - 1);
                    if (isnan(expected))
                        expected = 0;
                    if (expected < -1)
                        expected = -1;
                    if (expected >= 1)
                        expected = 1 - 1 / scale;
                    CHECK(fabs(decoded[i * stride] - expected) <= 1 / scale + 0.00000006);
                    uint32_t raw = 0;
                    for (unsigned b = 0; b < pcm.bytes; b++)
                        raw |= (uint32_t)memory[1 + i * pcm.bytes + b] << (8 * (pcm.big_endian ? pcm.bytes - b - 1 : b));
                    if (isfinite(values[i]) && values[i] == 0.5f)
                        CHECK((raw & ((UINT64_C(1) << pcm.bits) - 1)) == (UINT64_C(1) << (pcm.bits - 2)));
                } else if (isnan(expected))
                    CHECK(isnan(decoded[i * stride]));
                else
                    CHECK(decoded[i * stride] == values[i]);
                for (unsigned pad = 1; pad < stride; pad++)
                    CHECK(decoded[i * stride + pad] == 17);
            }
            CHECK(memory[0] == 0xab && memory[1 + 13 * pcm.bytes] == 0xab);
        }
    }
    CHECK(supported == 18);
    uint32_t frames = 999;
    CHECK(lnd_asio_buffer_size(0, 32, 512, 64, -1, &frames) == LND_OK && frames == 64);
    CHECK(lnd_asio_buffer_size(96, 32, 512, 64, -1, &frames) == LND_ERR_INVALID_ARG);
    CHECK(lnd_asio_buffer_size(96, 32, 512, 64, 32, &frames) == LND_OK && frames == 96);
    CHECK(lnd_asio_buffer_size(80, 32, 512, 64, 32, &frames) == LND_ERR_INVALID_ARG);
    CHECK(lnd_asio_buffer_size(64, 64, 64, 64, 0, &frames) == LND_OK);
    CHECK(lnd_asio_buffer_size(32, 64, 64, 64, 0, &frames) == LND_ERR_INVALID_ARG);
    CHECK(lnd_asio_buffer_size(0, 0, 64, 64, 0, &frames) == LND_ERR_FORMAT);
    CHECK(lnd_asio_buffer_size(0, 32, 512, 63, -1, &frames) != LND_OK);
    CHECK(lnd_asio_buffer_size(UINT32_MAX, 32, 512, 64, -1, &frames) == LND_ERR_INVALID_ARG);
}

typedef struct concurrent_test {
    lnd_backend *backend;
    lnd_stream *stream;
    mock_driver *driver;
    lnd_atomic_u32 done;
    bool deferred;
} concurrent_test;

static void concurrent_pump(void *user) {
    concurrent_test *test = user;
    pump(test->driver, 1, !test->deferred);
    lnd_store(&test->done, 1);
}

static void concurrent_close(void *user) {
    concurrent_test *test = user;
    test->backend->vt->close(test->stream);
    lnd_store(&test->done, 1);
}

static void test_concurrent(lnd_backend *backend, lnd_device *device) {
    for (unsigned deferred = 0; deferred < 2; deferred++) {
        audio_state state = {.channels = 2};
        lnd_stream_cfg cfg = {.sample_rate_hz = 48000, .channels = 2, .period_frames = 64};
        lnd_stream *stream = nullptr;
        CHECK(backend->vt->open(backend, device, &cfg, audio_proc, &state, &stream) == LND_OK);
        CHECK(backend->vt->start(stream) == LND_OK);
        mock_driver *driver = lnd_load(&active_drivers[0]);
        lnd_store(&state.entered, 0);
        lnd_store(&state.gate, 1);
        concurrent_test pumping = {.driver = driver, .deferred = deferred};
        lnd_thread producer = {0};
        CHECK(lnd_thread_create(&producer, concurrent_pump, &pumping) == LND_OK);
        for (unsigned wait = 0; wait < 1000 && !lnd_load(&state.entered); wait++)
            Sleep(1);
        CHECK(lnd_load(&state.entered));
        uint64_t overloads = lnd_load(&stream->driver->overloads);
        pump(driver, 1, false);
        CHECK(lnd_load(&stream->driver->overloads) == overloads + 1);
        concurrent_test closing = {.backend = backend, .stream = stream};
        lnd_thread closer = {0};
        CHECK(lnd_thread_create(&closer, concurrent_close, &closing) == LND_OK);
        Sleep(10);
        CHECK(!lnd_load(&closing.done));
        lnd_store(&state.gate, 0);
        lnd_thread_join(&producer);
        lnd_thread_join(&closer);
        CHECK(lnd_load(&closing.done) && !lnd_load(&active_drivers[0]));
        backend->vt->poll(backend);
    }
}

static void test_backend(void) {
    lnd_backend backend = {.vt = &lnd_backend_asio_vt};
    CHECK(backend.vt->init(&backend) == LND_OK);
    lnd_device_list output = {0}, input = {0};
    CHECK(backend.vt->enumerate(&backend, LND_DEVICE_OUTPUT, &output) == LND_OK && output.count == 2);
    CHECK(backend.vt->enumerate(&backend, LND_DEVICE_INPUT, &input) == LND_OK && input.count == 2);
    CHECK(lnd_load(&loads) == lnd_load(&unloads));
    lnd_asio_driver *record = output.items[0]->backend_data;
    CHECK(record == input.items[0]->backend_data);
    lnd_stream_cfg cfg = {.sample_rate_hz = 48000, .channels = 2, .period_frames = 64, .periods = 2};
    audio_state out = {.channels = 2, .reenter = true}, in = {.channels = 4, .input = true};
    lnd_stream *a = nullptr, *b = nullptr, *other = nullptr;
    CHECK(backend.vt->open(&backend, output.items[0], &cfg, audio_proc, &out, &a) == LND_OK);
    CHECK(cfg.exclusive && cfg.format == LND_FORMAT_F32 && cfg.buffer_frames == 128 && cfg.latency_frames == 137);
    CHECK(backend.vt->open(&backend, output.items[0], &cfg, audio_proc, &out, &other) == LND_ERR_BUSY);
    CHECK(backend.vt->start(a) == LND_OK && lnd_load(&out.calls) == 1);
    mock_driver *driver = lnd_load(&active_drivers[0]);
    uint32_t create_count = lnd_load(&creates);
    CHECK(backend.vt->open_capture(&backend, input.items[0], &cfg, audio_proc, &in, &b) == LND_OK);
    CHECK(cfg.channels == 4 && cfg.latency_frames == 91);
    CHECK(backend.vt->start(b) == LND_OK && lnd_load(&creates) == create_count);
    pump(driver, 1, true);
    CHECK(lnd_load(&in.calls) == 1 && lnd_load(&out.calls) == 2);
    LND_ASIO_TIME time;
    CHECK(lnd_asio_read_time(record, &time) == LND_OK);
    CHECK(time.sample_position_frames == (UINT64_C(4) << 32) + 64 && time.system_time_ns == (UINT64_C(3) << 32) + 10);
    CHECK(time.timecode_position_frames == (UINT64_C(5) << 32) + 11 && time.timecode_speed_ratio == 0.5);
    unsigned call_count = lnd_load(&out.calls);
    pump(driver, 0, false);
    CHECK(wait_calls(&out, call_count + 1));
    CHECK(lnd_load(&in.calls) >= 2);
    for (uint32_t i = 0; i < driver->count; i++)
        if (!driver->buffers[i].isInput) {
            lnd_asio_pcm pcm;
            float samples[512];
            CHECK(lnd_asio_pcm_get(mock_type(false, driver->buffers[i].channelNum), &pcm));
            pcm.read(driver->buffers[i].buffers[0], samples, driver->frames, 1);
            CHECK(fabsf(samples[0] - (driver->buffers[i].channelNum + 1) * 0.0625f) < 0.0001f);
        }
    lnd_stream_cfg second_cfg = {.sample_rate_hz = 48000, .channels = 2, .period_frames = 64};
    audio_state second = {.channels = 2};
    CHECK(backend.vt->open(&backend, output.items[1], &second_cfg, audio_proc, &second, &other) == LND_OK);
    CHECK(backend.vt->start(other) == LND_OK);
    pump(lnd_load(&active_drivers[1]), 1, true);
    CHECK(lnd_load(&second.calls) == 2);
    backend.vt->close(other);
    call_count = lnd_load(&in.calls);
    CHECK(backend.vt->stop(a) == LND_OK);
    pump(driver, 1, true);
    CHECK(lnd_load(&in.calls) == call_count + 1);
    uint32_t before_close = lnd_load(&starts);
    backend.vt->close(a);
    CHECK(lnd_load(&starts) == before_close);
    CHECK(driver->running && lnd_load(&active_drivers[0]) == driver);
    CHECK(backend.vt->start(b) == LND_OK);
    CHECK(driver->callbacks.asioMessage(kAsioLatenciesChanged, 0, nullptr, nullptr) == 1);
    backend.vt->poll(&backend);
    CHECK(backend.vt->status(b) == LND_OK && record->abi == &driver->abi);
    CHECK(backend.vt->get_latency_frames(b) == 91);
    CHECK(driver->callbacks.asioMessage(kAsioResetRequest, 0, nullptr, nullptr) == 1);
    backend.vt->poll(&backend);
    CHECK(backend.vt->status(b) == LND_ERR_EXTERNAL && !record->abi);
    second_cfg.channels = 2;
    CHECK(backend.vt->open(&backend, output.items[0], &second_cfg, audio_proc, &out, &a) == LND_OK);
    CHECK(backend.vt->start(a) == LND_OK);
    backend.vt->stop(b);
    backend.vt->close(b);
    CHECK(record->started);
    driver = lnd_load(&active_drivers[0]);
    lnd_store(&hardware_rate, 96000);
    driver->callbacks.sampleRateDidChange(96000);
    backend.vt->poll(&backend);
    CHECK(backend.vt->status(a) == LND_ERR_EXTERNAL);
    backend.vt->close(a);
    second_cfg.sample_rate_hz = 48000;
    CHECK(backend.vt->open(&backend, output.items[0], &second_cfg, audio_proc, &out, &a) == LND_OK);
    CHECK(second_cfg.sample_rate_hz == 96000);
    CHECK(backend.vt->start(a) == LND_OK);
    driver = lnd_load(&active_drivers[0]);
    CHECK(driver->callbacks.asioMessage(kAsioBufferSizeChange, 128, nullptr, nullptr) == 1);
    backend.vt->poll(&backend);
    backend.vt->close(a);
    CHECK(backend.vt->open(&backend, output.items[0], &second_cfg, audio_proc, &out, &a) == LND_OK && second_cfg.period_frames == 128);
    backend.vt->close(a);
    lnd_store(&hardware_rate, 48000);
    lnd_store(&record->recovery_rate, 0);
    lnd_store(&record->recovery_buffer, 0);
    lnd_atomic_i32 *faults[] = {&fail_load, &fail_init, &fail_create, &bad_buffers, &bad_format, &fail_start};
    for (unsigned i = 0; i < LND_COUNTOF(faults); i++) {
        lnd_store(faults[i], 1);
        cfg = (lnd_stream_cfg){.sample_rate_hz = 48000, .channels = 2, .period_frames = 64};
        a = nullptr;
        int32_t result = backend.vt->open(&backend, output.items[0], &cfg, audio_proc, &out, &a);
        if (faults[i] == &fail_start) {
            CHECK(result == LND_OK && backend.vt->start(a) != LND_OK);
            backend.vt->close(a);
        } else
            CHECK(result != LND_OK && !a);
        CHECK(!record->abi && !lnd_load(&active_drivers[0]));
        lnd_store(faults[i], 0);
    }
    lnd_store(&fail_rate, 1);
    cfg = (lnd_stream_cfg){.sample_rate_hz = 96000, .channels = 2, .period_frames = 64};
    CHECK(backend.vt->open(&backend, output.items[0], &cfg, audio_proc, &out, &a) == LND_ERR_NO_DEVICE && !a);
    lnd_store(&fail_rate, 0);
    lnd_store(&fail_load, 2);
    CHECK(backend.vt->open(&backend, output.items[0], &cfg, audio_proc, &out, &a) == LND_ERR_EXTERNAL && !a);
    lnd_store(&fail_load, 0);
    uint32_t baseline = lnd_load(&live_allocations);
    bool succeeded = false;
    for (int32_t nth = 1; nth < 40; nth++) {
        lnd_store(&fail_alloc, nth);
        cfg = (lnd_stream_cfg){.sample_rate_hz = 48000, .channels = 2, .period_frames = 64};
        a = nullptr;
        int32_t result = backend.vt->open(&backend, output.items[0], &cfg, audio_proc, &out, &a);
        lnd_store(&fail_alloc, 0);
        if (result == LND_OK) {
            backend.vt->close(a);
            succeeded = true;
        } else
            CHECK(a == nullptr);
        CHECK(!record->abi && lnd_load(&live_allocations) == baseline);
        if (succeeded)
            break;
    }
    CHECK(succeeded);
    test_concurrent(&backend, output.items[0]);
    lnd_device_list_free(&input);
    lnd_device_list_free(&output);
    backend.vt->free(&backend);
    CHECK(lnd_load(&loads) == lnd_load(&unloads) && lnd_load(&creates) == lnd_load(&disposes));
}

static void test_public(void) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_REALTIME) == LND_OK);
    CHECK(LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("asio")) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    CHECK(LND_DeviceGetCount(LND_DEVICE_OUTPUT) == 2);
    CHECK(LND_DeviceGetCount(LND_DEVICE_INPUT) == 2);
    LND_DEVICE *output = LND_DeviceGet(LND_DEVICE_OUTPUT, 0);
    LND_DEVICE *input = LND_DeviceGet(LND_DEVICE_INPUT, 0);
    LND_ASIO_INFO info;
    CHECK(LND_DeviceGetAsioInfo(output, &info) == LND_OK && info.driver_version == 42);
    CHECK(info.input_channels == 4 && info.output_channels == 6 && !info.running);
    CHECK((info.capabilities & LND_ASIO_CAP_INPUT_MONITOR) != 0);
    LND_ASIO_CHANNEL_INFO channel;
    CHECK(LND_DeviceGetAsioChannelInfo(output, LND_DEVICE_OUTPUT, 5, &channel) == LND_OK && channel.valid_bits == 20 && channel.big_endian);
    CHECK(LND_DeviceGetAsioChannelInfo(output, LND_DEVICE_OUTPUT, 6, &channel) == LND_ERR_INVALID_ARG);
    uint32_t clocks;
    LND_ASIO_CLOCK_INFO clock;
    CHECK(LND_DeviceGetAsioClockCount(output, &clocks) == LND_OK && clocks == 2);
    CHECK(LND_DeviceGetAsioClock(output, 1, &clock) == LND_OK && clock.index == 19);
    CHECK(LND_DeviceSetAsioClock(output, 1) == LND_ERR_INVALID_ARG);
    CHECK(LND_DeviceSetAsioClock(output, 19) == LND_OK);
    CHECK(LND_DeviceGetAsioClock(output, 1, &clock) == LND_OK && clock.current);
    CHECK(LND_DeviceCheckAsioSampleRateHz(output, 96000) == LND_OK);
    CHECK(LND_DeviceCheckAsioSampleRateHz(output, 44100) == LND_ERR_UNSUPPORTED);
    CHECK(LND_DeviceCheckAsioSampleRateHz(output, 0) == LND_ERR_INVALID_ARG);
    CHECK(LND_DeviceShowAsioControlPanel(output) == LND_OK);
    LND_ASIO_CONFIG config = {.sample_rate_hz = 96000,
                              .buffer_frames = 128,
                              .input_channels = 2,
                              .output_channels = 2,
                              .input_map = {0, 1},
                              .output_map = {5, 2},
                              .flags = LND_ASIO_TIMECODE};
    CHECK(LND_DeviceSetAsioConfig(output, &config) == LND_OK);
    LND_ASIO_CONFIG copied;
    CHECK(LND_DeviceGetAsioConfig(input, &copied) == LND_OK && copied.output_map[0] == 5 && copied.buffer_frames == 128);
    config.output_map[1] = 5;
    CHECK(LND_DeviceSetAsioConfig(output, &config) == LND_ERR_INVALID_ARG);
    config.output_map[1] = 2;
    LND_DEVICE_INSTANCE *instance = LND_DeviceInstanceOpen(output);
    CHECK(instance != nullptr);
    if (instance) {
        CHECK(LND_DeviceInstanceIsExclusive(instance));
        CHECK((uintptr_t)LND_DeviceInstanceGetNode(instance) % alignof(lnd_node) == 0);
        CHECK(LND_DeviceInstanceGetSampleRateHz(instance) == 96000 && LND_DeviceInstanceGetPeriodFrames(instance) == 128);
        CHECK(LND_DeviceSetAsioConfig(output, &config) == LND_ERR_BUSY);
        CHECK(LND_DeviceSetAsioClock(output, 7) == LND_ERR_BUSY);
        LND_SOURCE *capture = LND_SourceCreateDevice(input, 2, 96000, LND_CAPTURE_NONBLOCKING);
        CHECK(capture != nullptr);
        mock_driver *driver = lnd_load(&active_drivers[0]);
        CHECK(driver && driver->buffers[2].channelNum == 5 && driver->buffers[3].channelNum == 2);
        pump(driver, 1, true);
        pump(driver, 0, true);
        pump(driver, 1, true);
        if (capture) {
            float samples[256];
            CHECK(LND_SourceRead(capture, samples, LND_FORMAT_F32, 128) == 128);
            CHECK(fabsf(samples[0] - 0.125f) < 0.0001f && fabsf(samples[1] - 0.25f) < 0.0001f);
            CHECK(LND_DeviceResetAsio(output) == LND_OK);
            bool reopened = false;
            for (unsigned wait = 0; wait < 1000; wait++) {
                if (LND_DeviceInstanceIsRunning(instance)) {
                    LND_ASIO_INFO current;
                    if (LND_DeviceGetAsioInfo(output, &current) == LND_OK && current.active_input_channels == 2 && current.running) {
                        reopened = true;
                        break;
                    }
                }
                Sleep(1);
            }
            CHECK(reopened && LND_DeviceInstanceGetSampleRateHz(instance) == 96000);
            CHECK(LND_SourceRead(capture, samples, LND_FORMAT_F32, 128) == 0);
            CHECK(LND_SourceGetStatus(capture) == LND_SOURCE_WAITING);
            driver = lnd_load(&active_drivers[0]);
            if (driver) {
                pump(driver, 0, true);
                pump(driver, 1, true);
                pump(driver, 0, true);
            }
            CHECK(LND_SourceRead(capture, samples, LND_FORMAT_F32, 128) == 128);
            CHECK(LND_SourceFree(capture) == LND_OK);
        }
        LND_ASIO_INPUT_MONITOR monitor = {.input_channel = -1, .output_channel = 2, .gain = 0.5f, .pan = -1, .enabled = true};
        CHECK(LND_DeviceSetAsioInputMonitor(output, &monitor) == LND_OK);
        CHECK(LND_DeviceSetAsioChannelGain(output, LND_DEVICE_INPUT, 0, 0.5f) == LND_OK);
        CHECK(LND_DeviceSetAsioChannelGain(output, LND_DEVICE_INPUT, 0, NAN) == LND_ERR_INVALID_ARG);
        float meter = 0;
        CHECK(LND_DeviceGetAsioChannelMeter(output, LND_DEVICE_OUTPUT, 1, &meter) == LND_OK && fabsf(meter - 0.5f) < 0.0001f);
        CHECK(LND_DeviceSendAsioTransport(output, LND_ASIO_TRANSPORT_LOCATE, (UINT64_C(1) << 32) + 2, 0) == LND_OK);
        LND_ASIO_TIME time;
        CHECK(LND_DeviceGetAsioTime(output, &time) == LND_OK);
        CHECK(LND_DeviceInstanceClose(instance) == LND_OK);
    }
    lnd_store(&omit_future, 1);
    CHECK(LND_DeviceSetAsioChannelGain(output, LND_DEVICE_OUTPUT, 0, 0.5f) == LND_ERR_UNSUPPORTED);
    config.flags = LND_ASIO_TIMECODE;
    CHECK(LND_DeviceSetAsioConfig(output, &config) == LND_ERR_UNSUPPORTED);
    lnd_store(&omit_future, 0);
    CHECK(LND_DeviceResetAsio(output) == LND_OK);
    LND_LibraryFree();
    CHECK(lnd_load(&loads) == lnd_load(&unloads));
}

int main(void) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = test_alloc, .realloc = test_realloc, .free = test_free}) == LND_OK);
    test_pcm();
    test_backend();
    test_public();
    CHECK(lnd_load(&live_allocations) == 0);
    CHECK(lnd_load(&callback_allocations) == 0);
    printf("asio: %u checks, %u failures\n", lnd_load(&checks), lnd_load(&failures));
    return lnd_load(&failures) ? 1 : 0;
}
