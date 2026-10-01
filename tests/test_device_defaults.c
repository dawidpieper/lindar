#include "stream_test.h"
#include "lindar_devices.h"
#include "io/devices/context.h"
#include "io/devices/engine.h"
#include "io/devices/capture.h"
#include "src/alloc.h"

struct lnd_stream {
    lnd_stream_proc proc;
    void *user;
    bool running;
};

static void *resize(void *user, void *memory, size_t bytes) {
    (void)user;
    void *result = realloc(memory, bytes);
    if (result && !memory)
        allocations++;
    return result;
}

static unsigned defaults[2], failed_opens, opened, closed, renamed;
static uint32_t changed;

static void backend_free(lnd_backend *b) { (void)b; }

static int32_t enumerate(lnd_backend *b, int32_t type, lnd_device_list *list) {
    (void)b;
    for (unsigned k = 0; k < 2; k++) {
        char id[32];
        snprintf(id, sizeof id, "%d:%u", type, k);
        char name[48];
        snprintf(name, sizeof name, "%s:%u", id, renamed);
        lnd_device *d = lnd_device_new(type, name, id);
        if (!d)
            return LND_ERR_OUT_OF_MEMORY;
        d->flags = LND_DEVICE_FLAG_SHARED | LND_DEVICE_FLAG_MULTI_INSTANCE | LND_DEVICE_FLAG_LOOPBACK;
        if (k == defaults[type])
            d->flags |= LND_DEVICE_FLAG_DEFAULT;
        d->sample_rate_hz = 8000;
        d->channels = k + 1;
        d->format = LND_FORMAT_F32;
        int32_t result = lnd_device_list_push(list, d);
        if (result != LND_OK) {
            lnd_device_free(d);
            return result;
        }
    }
    return LND_OK;
}

static uint32_t poll(lnd_backend *b) {
    (void)b;
    uint32_t result = changed;
    changed = 0;
    return result;
}

static int32_t open_stream(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    (void)b;
    if (failed_opens) {
        failed_opens--;
        return LND_ERR_NO_DEVICE;
    }
    lnd_stream *s = lnd_alloc_zero(sizeof *s);
    if (!s)
        return LND_ERR_OUT_OF_MEMORY;
    s->proc = proc;
    s->user = user;
    cfg->channels = d->channels;
    cfg->sample_rate_hz = d->sample_rate_hz;
    cfg->format = LND_FORMAT_F32;
    cfg->period_frames = cfg->buffer_frames = cfg->latency_frames = 8;
    *out = s;
    opened++;
    return LND_OK;
}

static int32_t start(lnd_stream *s) {
    s->running = true;
    return LND_OK;
}
static int32_t stop(lnd_stream *s) {
    s->running = false;
    return LND_OK;
}
static int32_t status(lnd_stream *s) { return s->running ? LND_OK : LND_ERR_NO_DEVICE; }
static void close_stream(lnd_stream *s) {
    closed++;
    lnd_free(s);
}

static const lnd_backend_vt backend = {.name = "test",
                                       .free = backend_free,
                                       .enumerate = enumerate,
                                       .poll = poll,
                                       .open = open_stream,
                                       .open_capture = open_stream,
                                       .start = start,
                                       .stop = stop,
                                       .status = status,
                                       .close = close_stream};

static void maintain(void) {
    lnd_context_lock();
    lnd_engine_maintain();
    lnd_context_unlock();
}

static void change(unsigned output, unsigned input) {
    defaults[LND_DEVICE_OUTPUT] = output;
    defaults[LND_DEVICE_INPUT] = input;
    changed = LND_BACKEND_EVENT_DEVICES | LND_BACKEND_EVENT_DEFAULT_INPUT | LND_BACKEND_EVENT_DEFAULT_OUTPUT;
    maintain();
}

static void capacity_bounds(void) {
    uint32_t mode_limit = (uint32_t)LND_MIN((uint64_t)UINT32_MAX, SIZE_MAX / sizeof(LND_DEVICE_MODE));
    uint32_t list_limit = (uint32_t)LND_MIN((uint64_t)UINT32_MAX, SIZE_MAX / sizeof(lnd_device *));
    lnd_device device = {.modes_count = mode_limit, .modes_cap = mode_limit, .instances_count = list_limit, .instances_cap = list_limit};
    lnd_device_list list = {.count = list_limit, .cap = list_limit};
    CHECK(lnd_device_add_mode(&device, 48000, 2, LND_FORMAT_S16, 0) == LND_ERR_OUT_OF_MEMORY);
    CHECK(lnd_device_list_push(&list, &device) == LND_ERR_OUT_OF_MEMORY);
    CHECK(lnd_device_attach_instance(&device, nullptr) == LND_ERR_OUT_OF_MEMORY);
    CHECK(device.modes_count == mode_limit && device.modes_cap == mode_limit && !device.modes);
    CHECK(device.instances_count == list_limit && device.instances_cap == list_limit && !device.instances);
    CHECK(list.count == list_limit && list.cap == list_limit && !list.items);
}

#if LND_THREADS
typedef struct device_observer {
    LND_DEVICE *device;
    LND_DEVICE_INSTANCE *instance;
    lnd_atomic_u32 ready, stop;
    uint64_t reads, invalid;
} device_observer;

static void observe_device(void *user) {
    device_observer *s = user;
    lnd_store(&s->ready, 1);
    while (!lnd_load(&s->stop)) {
        if (LND_DeviceGetInstanceCount(s->device) < 1 || LND_DeviceGetInstanceCount(s->device) > 2 ||
            LND_DeviceGetChannels(s->device) != 1 || LND_DeviceGetSampleRateHz(s->device) != 8000 ||
            !(LND_DeviceGetFlags(s->device) & LND_DEVICE_FLAG_SHARED) || LND_DeviceGetInstance(s->device, 0) != s->instance)
            s->invalid++;
        LND_DEVICE_MODE mode;
        int32_t result = LND_DeviceGetMode(s->device, 0, &mode);
        if (result != LND_OK && result != LND_ERR_INVALID_ARG) s->invalid++;
        const char *name = LND_DeviceGetName(s->device);
        if (strncmp(name, "0:0:", 4) || strlen(name) != 5 || name[4] < '0' || name[4] > '2') s->invalid++;
        s->reads++;
    }
}

static void concurrent_devices(LND_DEVICE *device, LND_DEVICE_INSTANCE *instance) {
    const char *initial = LND_DeviceGetName(device);
    CHECK(!strcmp(initial, "0:0:0"));
    device_observer observer = {.device = device, .instance = instance};
    lnd_thread thread;
    CHECK(lnd_thread_create(&thread, observe_device, &observer) == LND_OK);
    while (!lnd_load(&observer.ready)) lnd_sleep_ms(1);
    for (unsigned i = 0; i < 128; i++) {
        LND_DEVICE_INSTANCE *extra = LND_DeviceInstanceOpen(device);
        CHECK(extra);
        renamed = i % 3;
        CHECK(LND_DeviceRefresh() == LND_OK);
        CHECK(LND_DeviceInstanceClose(extra) == LND_OK);
    }
    lnd_store(&observer.stop, 1);
    lnd_thread_join(&thread);
    CHECK(observer.reads && !observer.invalid);
    CHECK(!strcmp(initial, "0:0:0"));
    renamed = 0;
}
#endif

int main(void) {
    CHECK(LND_DeviceGetActiveBackend() == nullptr);
    CHECK(LND_DeviceGetDefaultHandle(LND_DEVICE_INPUT) == LND_DEVICE_DEFAULT_INPUT);
    CHECK(LND_DeviceGetDefaultHandle(LND_DEVICE_OUTPUT) == LND_DEVICE_DEFAULT_OUTPUT);
    CHECK(LND_DeviceGetDefaultHandle(-1) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    capacity_bounds();
    CHECK(LND_DeviceGetType(LND_DEVICE_DEFAULT_INPUT) == LND_DEVICE_INPUT);
    CHECK(LND_DeviceIsDefault(LND_DEVICE_DEFAULT_OUTPUT));
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .free = release, .realloc = resize}) == LND_OK);
    CHECK(LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("null")) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_REOPEN_INTERVAL_MS, 600000) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    CHECK(LND_DeviceGetActiveBackend() == LND_DeviceGetPreferredBackend());
    lnd_context_lock();
    lnd_engine_stop();
    lnd_device_ctx.backend.vt->free(&lnd_device_ctx.backend);
    lnd_device_ctx.backend = (lnd_backend){.vt = &backend};
    lnd_context_unlock();
    CHECK(LND_DeviceSetPreferred(LND_DEVICE_OUTPUT, LND_DEVICE_DEFAULT_INPUT) == LND_ERR_INVALID_ARG);
    CHECK(LND_DeviceSetPreferred(LND_DEVICE_OUTPUT, LND_DEVICE_DEFAULT_OUTPUT) == LND_OK);
    CHECK(LND_DeviceSetPreferred(LND_DEVICE_INPUT, LND_DEVICE_DEFAULT_INPUT) == LND_OK);
    CHECK(LND_DeviceGetPreferred(LND_DEVICE_OUTPUT) == LND_DEVICE_DEFAULT_OUTPUT);
    CHECK(LND_DeviceGetPreferred(LND_DEVICE_INPUT) == LND_DEVICE_DEFAULT_INPUT);
    CHECK(LND_DeviceSetPreferred(2, nullptr) == LND_ERR_INVALID_ARG);
    lnd_device *physical = LND_DeviceResolve(LND_DEVICE_DEFAULT_OUTPUT);
    CHECK(physical && !strcmp(LND_DeviceGetId(physical), "0:0"));
    LND_DEVICE_INSTANCE *fixed = LND_DeviceInstanceOpen(physical);
#if LND_THREADS
    concurrent_devices(physical, fixed);
#endif
    LND_DEVICE_INSTANCE *following = LND_DeviceInstanceOpen(LND_DEVICE_DEFAULT_OUTPUT);
    CHECK(fixed && following && !fixed->follow_default && following->follow_default);
    CHECK(LND_DeviceInstanceOpen(LND_DEVICE_DEFAULT_INPUT) == nullptr);
    LND_SOURCE *source = LND_SourceCreateDevice(LND_DEVICE_DEFAULT_INPUT, 1, 8000, LND_CAPTURE_NONBLOCKING);
    CHECK(source != nullptr);
    lnd_instance *capture = nullptr;
    for (lnd_instance *i = lnd_device_ctx.instances; i; i = i->next)
        if (i->capture)
            capture = i;
    CHECK(capture && capture->follow_default);
    following->last_reopen = capture->last_reopen = lnd_time_ns();
    change(1, 1);
    CHECK(LND_DeviceInstanceIsRunning(following) && LND_DeviceInstanceIsRunning(capture));
    CHECK(!strcmp(LND_DeviceGetId(LND_DeviceInstanceGetDevice(following)), "0:1"));
    CHECK(LND_DeviceInstanceGetDevice(fixed) == physical);
    CHECK(LND_DeviceInstanceGetChannels(following) == 2);
    CHECK(LND_DeviceInstanceGetChannels(capture) == 2 && LND_SourceGetChannels(source) == 1);
    float samples[32], output[16];
    for (unsigned k = 0; k < 32; k++)
        samples[k] = 0.5f;
    LND_PCM pcm = {.data = output, .frames = 16, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_SourceReadPcm(source, &pcm, 0, 16) == 0);
    capture->stream->proc(capture->stream->user, samples, 16);
    CHECK(LND_SourceReadPcm(source, &pcm, 0, 16) == 16);
    CHECK(output[0] == 0.5f && output[15] == 0.5f);
    failed_opens = 2;
    change(0, 0);
    CHECK(!LND_DeviceInstanceIsRunning(following) && !LND_DeviceInstanceIsRunning(capture));
    CHECK(LND_DeviceInstanceIsRunning(fixed));
    following->last_reopen = capture->last_reopen = 0;
    maintain();
    CHECK(LND_DeviceInstanceIsRunning(following) && LND_DeviceInstanceIsRunning(capture));
    CHECK(!strcmp(LND_DeviceGetId(LND_DeviceInstanceGetDevice(following)), "0:0"));
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_FOLLOW_DEFAULT, 0) == LND_OK);
    change(1, 1);
    CHECK(!strcmp(LND_DeviceGetId(LND_DeviceInstanceGetDevice(following)), "0:0"));
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_FOLLOW_DEFAULT, 1) == LND_OK);
    maintain();
    CHECK(!strcmp(LND_DeviceGetId(LND_DeviceInstanceGetDevice(following)), "0:1"));
    unsigned before = opened;
    following->last_reopen = lnd_time_ns();
    CHECK(LND_DeviceNotifyDefaultChanged(LND_DEVICE_OUTPUT) == LND_OK);
    maintain();
    CHECK(opened == before + 1);
    CHECK(LND_DeviceNotifyDefaultChanged(5) == LND_ERR_INVALID_ARG);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(LND_DeviceInstanceClose(following) == LND_OK);
    CHECK(LND_DeviceInstanceClose(fixed) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 1) == LND_OK);
    following = LND_DeviceEnsureOutputInstance();
    CHECK(following && following->follow_default);
    CHECK(LND_DeviceInstanceClose(following) == LND_OK);
    finish();
    CHECK(opened == closed);
    CHECK(LND_DeviceGetActiveBackend() == nullptr);
    return report();
}
