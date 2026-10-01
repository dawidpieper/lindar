#include "asio.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/context.h"
#include "io/devices/context.h"

#include <objbase.h>
#include <string.h>

static void lnd_asio_control_thread(void *user) {
    lnd_asio_backend *backend = user;
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    backend->startup_result = SUCCEEDED(com) ? LND_OK : LND_ERR_EXTERNAL;
    if (SUCCEEDED(com)) {
        backend->window = CreateWindowExW(0, L"STATIC", L"Lindar ASIO", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!backend->window)
            backend->startup_result = LND_ERR_EXTERNAL;
    }
    SetEvent(backend->done_event);
    lnd_callback_enter();
    if (backend->startup_result == LND_OK) {
        while (!lnd_load(&backend->quit)) {
            DWORD wait = MsgWaitForMultipleObjects(1, &backend->command_event, FALSE, INFINITE, QS_ALLINPUT);
            if (wait == WAIT_OBJECT_0) {
                lnd_asio_job *job = lnd_exchange(&backend->job, nullptr);
                if (job) {
                    job->result = job->proc(backend, job->argument);
                    SetEvent(backend->done_event);
                }
            } else if (wait == WAIT_OBJECT_0 + 1) {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            } else
                break;
        }
    }
    lnd_callback_leave();
    if (backend->window)
        DestroyWindow(backend->window);
    if (SUCCEEDED(com))
        CoUninitialize();
}

int32_t lnd_asio_call(lnd_asio_backend *backend, lnd_asio_command proc, void *argument) {
    lnd_asio_job job = {.proc = proc, .argument = argument, .result = LND_ERR_EXTERNAL};
    lnd_mutex_lock(&backend->commands);
    lnd_store(&backend->job, &job);
    SetEvent(backend->command_event);
    HANDLE wait[] = {backend->done_event, backend->thread.handle};
    DWORD result = WaitForMultipleObjects(2, wait, FALSE, INFINITE);
    lnd_mutex_unlock(&backend->commands);
    return result == WAIT_OBJECT_0 ? job.result : LND_ERR_EXTERNAL;
}

static int32_t lnd_asio_init(lnd_backend *base) {
    lnd_asio_backend *backend = lnd_alloc_zero(sizeof *backend);
    if (!backend)
        return LND_ERR_OUT_OF_MEMORY;
    backend->backend = base;
    lnd_mutex_init(&backend->commands);
    backend->command_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    backend->done_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    int32_t result = backend->command_event && backend->done_event ? lnd_thread_create(&backend->thread, lnd_asio_control_thread, backend) : LND_ERR_EXTERNAL;
    if (result == LND_OK) {
        WaitForSingleObject(backend->done_event, INFINITE);
        result = backend->startup_result;
    }
    if (result != LND_OK) {
        lnd_thread_join(&backend->thread);
        if (backend->command_event)
            CloseHandle(backend->command_event);
        if (backend->done_event)
            CloseHandle(backend->done_event);
        lnd_mutex_free(&backend->commands);
        lnd_free(backend);
        return result;
    }
    base->data = backend;
    return LND_OK;
}

static int32_t lnd_asio_free_command(lnd_asio_backend *backend, void *argument) {
    LND_UNUSED(argument);
    while (backend->drivers) {
        lnd_asio_driver *driver = backend->drivers;
        backend->drivers = driver->next;
        lnd_asio_invalidate(driver, LND_ERR_STATE);
        lnd_free(driver->name);
        lnd_free(driver);
    }
    return LND_OK;
}

static void lnd_asio_free(lnd_backend *base) {
    lnd_asio_backend *backend = base->data;
    if (!backend)
        return;
    lnd_asio_call(backend, lnd_asio_free_command, nullptr);
    lnd_store(&backend->quit, 1);
    SetEvent(backend->command_event);
    lnd_thread_join(&backend->thread);
    CloseHandle(backend->command_event);
    CloseHandle(backend->done_event);
    lnd_mutex_free(&backend->commands);
    lnd_free(backend);
    base->data = nullptr;
}

static int32_t lnd_asio_register(void *user, const GUID *id, const char *key, const char *name) {
    lnd_asio_backend *backend = user;
    lnd_asio_driver **entry = &backend->drivers;
    while (*entry && memcmp(&(*entry)->clsid, id, sizeof *id))
        entry = &(*entry)->next;
    lnd_asio_driver *driver = *entry;
    if (!driver) {
        driver = lnd_alloc_zero(sizeof *driver);
        if (!driver)
            return LND_ERR_OUT_OF_MEMORY;
        driver->name = lnd_strdup(name);
        if (!driver->name) {
            lnd_free(driver);
            return LND_ERR_OUT_OF_MEMORY;
        }
        driver->owner = backend;
        driver->clsid = *id;
        memcpy(driver->id, key, strlen(key) + 1);
        driver->slot = -1;
        driver->clock_index = -1;
        *entry = driver;
    } else if (strcmp(driver->name, name)) {
        char *replacement = lnd_strdup(name);
        if (!replacement)
            return LND_ERR_OUT_OF_MEMORY;
        lnd_free(driver->name);
        driver->name = replacement;
    }
    driver->seen = true;
    return LND_OK;
}

typedef struct lnd_asio_enumeration {
    int32_t type;
    lnd_device_list *list;
} lnd_asio_enumeration;

static int32_t lnd_asio_enumerate_command(lnd_asio_backend *backend, void *argument) {
    lnd_asio_enumeration *request = argument;
    for (lnd_asio_driver *driver = backend->drivers; driver; driver = driver->next)
        driver->seen = false;
    int32_t result = lnd_asio_registry_enumerate(lnd_asio_register, backend, &backend->registry_fingerprint);
    if (result != LND_OK)
        return result;
    for (lnd_asio_driver *driver = backend->drivers; driver; driver = driver->next) {
        if (!driver->seen)
            continue;
        bool loaded = driver->abi != nullptr;
        result = lnd_asio_load(driver);
        driver->available = result == LND_OK;
        if (result == LND_ERR_OUT_OF_MEMORY)
            return result;
        uint32_t channels = request->type == LND_DEVICE_INPUT ? driver->info.input_channels : driver->info.output_channels;
        if (!loaded && driver->abi)
            lnd_asio_unload(driver);
        if (driver->available && !channels)
            continue;
        lnd_device *device = lnd_device_new(request->type, driver->name, driver->id);
        if (!device)
            return LND_ERR_OUT_OF_MEMORY;
        device->backend_data = driver;
        device->flags = LND_DEVICE_FLAG_EXCLUSIVE;
        if (!driver->available)
            device->flags |= LND_DEVICE_FLAG_STALE;
        device->sample_rate_hz = driver->info.sample_rate_hz;
        device->channels = LND_MIN(channels, LND_ASIO_MAX_CHANNELS);
        device->format = LND_FORMAT_F32;
        device->default_period = driver->info.preferred_buffer_frames;
        device->min_period = driver->info.min_buffer_frames;
        device->max_instances = 1;
        if (device->channels && device->sample_rate_hz)
            result = lnd_device_add_mode(device, device->sample_rate_hz, device->channels, LND_FORMAT_F32, LND_DEVICE_FLAG_EXCLUSIVE);
        else
            result = LND_OK;
        if (result == LND_OK)
            result = lnd_device_list_push(request->list, device);
        if (result != LND_OK) {
            lnd_device_free(device);
            return result;
        }
    }
    return LND_OK;
}

static int32_t lnd_asio_enumerate(lnd_backend *base, int32_t type, lnd_device_list *out) {
    lnd_asio_enumeration request = {.type = type, .list = out};
    return lnd_asio_call(base->data, lnd_asio_enumerate_command, &request);
}

static int32_t lnd_asio_poll_command(lnd_asio_backend *backend, void *argument) {
    uint32_t *events = argument;
    uint64_t now = lnd_time_ns();
    if (now >= backend->next_scan) {
        uint64_t fingerprint = backend->registry_fingerprint;
        if (lnd_asio_registry_enumerate(nullptr, nullptr, &fingerprint) == LND_OK && fingerprint != backend->registry_fingerprint) {
            backend->registry_fingerprint = fingerprint;
            *events |= LND_BACKEND_EVENT_DEVICES;
        }
        backend->next_scan = now + UINT64_C(2000000000);
    }
    for (lnd_asio_driver *driver = backend->drivers; driver; driver = driver->next) {
        uint32_t pending = lnd_exchange(&driver->pending, 0);
        if ((pending & LND_ASIO_LATENCIES) && driver->abi) {
            lnd_asio_refresh_latencies(driver);
        }
        if (pending & (LND_ASIO_RESET | LND_ASIO_RESYNC))
            lnd_asio_invalidate(driver, LND_ERR_EXTERNAL);
    }
    return LND_OK;
}

static uint32_t lnd_asio_poll(lnd_backend *base) {
    uint32_t events = 0;
    lnd_asio_call(base->data, lnd_asio_poll_command, &events);
    return events;
}

typedef struct lnd_asio_open_request {
    lnd_asio_driver *driver;
    lnd_stream_cfg *config;
    lnd_stream_proc proc;
    void *user;
    lnd_stream **out;
    unsigned direction;
} lnd_asio_open_request;

static int32_t lnd_asio_open_command(lnd_asio_backend *backend, void *argument) {
    LND_UNUSED(backend);
    lnd_asio_open_request *request = argument;
    lnd_asio_driver *driver = request->driver;
    if (lnd_load(&driver->streams[request->direction]))
        return LND_ERR_BUSY;
    if ((request->direction == LND_DEVICE_INPUT && (driver->config.flags & LND_ASIO_DISABLE_INPUT)) ||
        (request->direction == LND_DEVICE_OUTPUT && (driver->config.flags & LND_ASIO_DISABLE_OUTPUT)))
        return LND_ERR_UNSUPPORTED;
    bool existing = driver->buffers_created;
    int32_t result = lnd_asio_prepare(driver, request->config);
    if (result != LND_OK)
        return result;
    uint32_t channels = request->direction == LND_DEVICE_INPUT ? driver->info.active_input_channels : driver->info.active_output_channels;
    if (!channels)
        result = LND_ERR_UNSUPPORTED;
    if (existing && request->config->sample_rate_hz && !driver->config.sample_rate_hz && !lnd_load(&driver->recovery_rate) &&
        request->config->sample_rate_hz != driver->info.sample_rate_hz)
        result = LND_ERR_FORMAT;
    lnd_stream *stream = result == LND_OK ? lnd_alloc_zero(sizeof *stream) : nullptr;
    if (result == LND_OK && !stream)
        result = LND_ERR_OUT_OF_MEMORY;
    if (result != LND_OK) {
        if (!existing)
            lnd_asio_unload(driver);
        return result;
    }
    stream->driver = driver;
    stream->direction = request->direction;
    stream->proc = request->proc;
    stream->user = request->user;
    stream->cfg = *request->config;
    stream->cfg.sample_rate_hz = driver->info.sample_rate_hz;
    stream->cfg.channels = channels;
    stream->cfg.format = LND_FORMAT_F32;
    stream->cfg.exclusive = true;
    stream->cfg.period_frames = driver->info.buffer_frames;
    stream->cfg.periods = 2;
    stream->cfg.buffer_frames = driver->info.buffer_frames * 2;
    stream->cfg.latency_frames = request->direction == LND_DEVICE_INPUT ? driver->info.input_latency_frames : driver->info.output_latency_frames;
    *request->config = stream->cfg;
    atomic_store_explicit(&driver->streams[request->direction], stream, memory_order_seq_cst);
    *request->out = stream;
    return LND_OK;
}

static int32_t lnd_asio_open_common(lnd_backend *backend, lnd_device *device, lnd_stream_cfg *config, lnd_stream_proc proc, void *user, lnd_stream **out,
                                    unsigned direction) {
    if (!device || !config || !proc || !out || !device->backend_data)
        return LND_ERR_INVALID_ARG;
    *out = nullptr;
    if (config->loopback)
        return LND_ERR_UNSUPPORTED;
    lnd_asio_open_request request = {.driver = device->backend_data, .config = config, .proc = proc, .user = user, .out = out, .direction = direction};
    return lnd_asio_call(backend->data, lnd_asio_open_command, &request);
}

static int32_t lnd_asio_open(lnd_backend *backend, lnd_device *device, lnd_stream_cfg *config, lnd_stream_proc proc, void *user, lnd_stream **out) {
    return lnd_asio_open_common(backend, device, config, proc, user, out, LND_DEVICE_OUTPUT);
}

static int32_t lnd_asio_open_capture(lnd_backend *backend, lnd_device *device, lnd_stream_cfg *config, lnd_stream_proc proc, void *user, lnd_stream **out) {
    return lnd_asio_open_common(backend, device, config, proc, user, out, LND_DEVICE_INPUT);
}

static int32_t lnd_asio_start_command(lnd_asio_backend *backend, void *argument) {
    LND_UNUSED(backend);
    lnd_stream *stream = argument;
    if (lnd_load(&stream->failed))
        return lnd_load(&stream->failed);
    atomic_store_explicit(&stream->active, 1, memory_order_seq_cst);
    int32_t result = lnd_asio_start(stream->driver);
    if (result != LND_OK) {
        lnd_asio_stream_quiesce(stream);
        lnd_store(&stream->failed, result);
    }
    return result;
}

static int32_t lnd_asio_stream_start(lnd_stream *stream) { return lnd_asio_call(stream->driver->owner, lnd_asio_start_command, stream); }

static int32_t lnd_asio_stop_command(lnd_asio_backend *backend, void *argument) {
    LND_UNUSED(backend);
    lnd_stream *stream = argument;
    lnd_asio_stream_quiesce(stream);
    if (lnd_load(&stream->driver->streams[stream->direction]) != stream)
        return LND_OK;
    lnd_stream *other = lnd_load(&stream->driver->streams[1 - stream->direction]);
    if (!other || !lnd_load(&other->active))
        lnd_asio_stop(stream->driver);
    return LND_OK;
}

static int32_t lnd_asio_stream_stop(lnd_stream *stream) { return lnd_asio_call(stream->driver->owner, lnd_asio_stop_command, stream); }

static int32_t lnd_asio_status(lnd_stream *stream) {
    int32_t failed = lnd_load(&stream->failed);
    return failed ? failed : (lnd_load(&stream->driver->pending) & (LND_ASIO_RESET | LND_ASIO_RESYNC)) ? LND_ERR_EXTERNAL : LND_OK;
}

static int32_t lnd_asio_close_command(lnd_asio_backend *backend, void *argument) {
    LND_UNUSED(backend);
    lnd_stream *stream = argument;
    lnd_asio_driver *driver = stream->driver;
    if (lnd_load(&driver->streams[stream->direction]) == stream) {
        lnd_asio_stream_quiesce(stream);
        for (;;) {
            uint32_t expected = 0;
            if (lnd_cas(&driver->processing, &expected, 1))
                break;
            Sleep(0);
        }
        atomic_store_explicit(&driver->streams[stream->direction], nullptr, memory_order_seq_cst);
        lnd_stream *other = lnd_load(&driver->streams[1 - stream->direction]);
        lnd_store(&driver->processing, 0);
        lnd_event_signal(&driver->render_event);
        if (!other)
            lnd_asio_unload(driver);
        else if (!lnd_load(&other->active))
            lnd_asio_stop(driver);
    }
    lnd_free(stream);
    return LND_OK;
}

static uint32_t lnd_asio_latency(lnd_stream *stream) { return stream->cfg.latency_frames; }

static void lnd_asio_close(lnd_stream *stream) { lnd_asio_call(stream->driver->owner, lnd_asio_close_command, stream); }

lnd_asio_driver *lnd_asio_device_driver(LND_DEVICE *device) {
    if (!device || !lnd_device_ctx.backend_ready || lnd_device_ctx.backend.vt != &lnd_backend_asio_vt)
        return nullptr;
    for (unsigned type = 0; type < 2; type++)
        for (uint32_t i = 0; i < lnd_device_ctx.devices[type].count; i++)
            if (lnd_device_ctx.devices[type].items[i] == device)
                return device->backend_data;
    return nullptr;
}

const lnd_backend_vt lnd_backend_asio_vt = {
    .name = "asio",
    .init = lnd_asio_init,
    .free = lnd_asio_free,
    .enumerate = lnd_asio_enumerate,
    .poll = lnd_asio_poll,
    .open = lnd_asio_open,
    .open_capture = lnd_asio_open_capture,
    .start = lnd_asio_stream_start,
    .stop = lnd_asio_stream_stop,
    .status = lnd_asio_status,
    .close = lnd_asio_close,
    .get_latency_frames = lnd_asio_latency,
};
