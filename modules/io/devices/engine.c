#include "playback/graph/context.h"
#include "context.h"
#include "engine.h"
#include "src/module.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/error.h"
#include "utility/log/log.h"
#include "src/thread.h"
#include "capture.h"
#include "pcm/audio/convert.h"
#include "src/format.h"

static struct {
    lnd_thread thread;
    lnd_event event;
    lnd_atomic_u32 stop;
    lnd_spinlock wake_lock;
    bool running;
} lnd_maint;

static lnd_atomic_u32 lnd_default_events;

int32_t LND_DeviceNotifyDefaultChanged(int32_t type) {
    if (type != LND_DEVICE_OUTPUT && type != LND_DEVICE_INPUT) return lnd_error(LND_ERR_INVALID_ARG);
    uint32_t bit = type == LND_DEVICE_OUTPUT ? LND_BACKEND_EVENT_DEFAULT_OUTPUT : LND_BACKEND_EVENT_DEFAULT_INPUT;
    atomic_fetch_or_explicit(&lnd_default_events, bit, memory_order_release);
    lnd_engine_wake();
    return LND_OK;
}

enum { LND_INFO_RATE, LND_INFO_CHANNELS, LND_INFO_FORMAT, LND_INFO_PERIOD, LND_INFO_BUFFER, LND_INFO_LATENCY, LND_INFO_EXCLUSIVE };

static void lnd_instance_publish(lnd_instance *i) {
    lnd_store(&i->public_device, i->device);
    lnd_store(&i->info[LND_INFO_RATE], i->cfg.sample_rate_hz);
    lnd_store(&i->info[LND_INFO_CHANNELS], i->cfg.channels);
    lnd_store(&i->info[LND_INFO_FORMAT], (uint32_t)i->cfg.format);
    lnd_store(&i->info[LND_INFO_PERIOD], i->cfg.period_frames);
    lnd_store(&i->info[LND_INFO_BUFFER], i->cfg.buffer_frames);
    lnd_store(&i->info[LND_INFO_LATENCY], i->cfg.latency_frames);
    lnd_store(&i->info[LND_INFO_EXCLUSIVE], i->cfg.exclusive);
}

typedef struct lnd_event_item {
    int32_t event;
    lnd_instance *instance;
} lnd_event_item;

static void lnd_instance_proc(void *user, void *dst, uint64_t frames) {
    lnd_instance *i = user;
    if (i->cfg.format == LND_FORMAT_F32) {
        lnd_node_pull(i->master, &i->cursor, dst, (uint32_t)frames);
    } else {
        uint8_t *out = dst;
        size_t frame_bytes = (size_t)i->cfg.channels * lnd_format_bytes(i->cfg.format);
        uint64_t done = 0;
        while (done < frames) {
            uint32_t n = (uint32_t)LND_MIN((uint64_t)i->scratch_frames, frames - done);
            lnd_node_pull(i->master, &i->cursor, i->scratch, n);
            lnd_pcm_from_f32(i->cfg.format, i->scratch, out + done * frame_bytes, (size_t)n * i->cfg.channels);
            done += n;
        }
    }

#if LND_MODULE_SINK
    lnd_sinks_pump(i, frames);
#endif
    lnd_add(&i->position, frames);
}

static void lnd_capture_instance_proc(void *user, void *data, uint64_t frames) {
    lnd_instance *i = user;
    lnd_add(&i->position, frames);
    lnd_capture_push(i->capture, data, i->cfg.format, frames);
}

static int32_t lnd_instance_check_shared(const lnd_device *d, bool exclusive) {
    if (exclusive) {
        if (!(d->flags & LND_DEVICE_FLAG_EXCLUSIVE)) return LND_ERR_UNSUPPORTED;
        if (d->instances_count > 0) return LND_ERR_BUSY;
        return LND_OK;
    }
    if (!(d->flags & LND_DEVICE_FLAG_SHARED)) return LND_ERR_UNSUPPORTED;
    if (d->exclusive_open) return LND_ERR_BUSY;
    if (d->max_instances && d->instances_count >= d->max_instances) return LND_ERR_BUSY;
    if (!(d->flags & LND_DEVICE_FLAG_MULTI_INSTANCE) && d->instances_count >= 1) return LND_ERR_BUSY;
    return LND_OK;
}

static int32_t lnd_instance_check_capture_policy(const lnd_device *d, bool exclusive, bool loopback) {
    if (d->flags & LND_DEVICE_FLAG_STALE) return LND_ERR_NO_DEVICE;
    if (loopback) {
        if (!(d->flags & LND_DEVICE_FLAG_LOOPBACK) || exclusive) return LND_ERR_UNSUPPORTED;
    } else if (d->type != LND_DEVICE_INPUT) {
        return LND_ERR_UNSUPPORTED;
    }
    return lnd_instance_check_shared(d, exclusive);
}

static uint32_t lnd_device_nearest_exclusive_rate(const lnd_device *d, uint32_t sample_rate_hz) {
    uint32_t best = 0;
    for (uint32_t k = 0; k < d->modes_count; k++) {
        const LND_DEVICE_MODE *m = &d->modes[k];
        if (!(m->flags & LND_DEVICE_FLAG_EXCLUSIVE)) continue;
        uint32_t diff = m->sample_rate_hz > sample_rate_hz ? m->sample_rate_hz - sample_rate_hz : sample_rate_hz - m->sample_rate_hz;
        uint32_t best_diff = best > sample_rate_hz ? best - sample_rate_hz : sample_rate_hz - best;
        if (!best || diff < best_diff) best = m->sample_rate_hz;
    }
    return best;
}

static int32_t lnd_instance_check_policy(const lnd_device *d, bool exclusive) {
    if (d->type != LND_DEVICE_OUTPUT) return LND_ERR_UNSUPPORTED;
    if (d->flags & LND_DEVICE_FLAG_STALE) return LND_ERR_NO_DEVICE;
    return lnd_instance_check_shared(d, exclusive);
}

static void lnd_instance_stop_stream(lnd_instance *i) {
    if (i->stream && i->running) lnd_device_ctx.backend.vt->stop(i->stream);
    i->running = false;
}

static void lnd_instance_close_stream(lnd_instance *i) {
    lnd_instance_stop_stream(i);
    if (i->stream) lnd_device_ctx.backend.vt->close(i->stream);
    i->stream = nullptr;
}

static int32_t lnd_instance_prepare_scratch(lnd_instance *i) {
    lnd_free_aligned(i->scratch);
    i->scratch = nullptr;
    i->scratch_frames = 0;
    if (i->cfg.format == LND_FORMAT_F32) return LND_OK;
    i->scratch_frames = LND_MAX(i->cfg.buffer_frames, i->master->block);
    i->scratch = lnd_alloc_aligned((size_t)i->scratch_frames * i->cfg.channels * sizeof(float), LND_CACHE_LINE);
    return i->scratch ? LND_OK : LND_ERR_OUT_OF_MEMORY;
}

static void lnd_instance_reset_sources(lnd_instance *i) {
    for (lnd_edge *e = i->master->inputs; e; e = e->next_in) {
        if (e->src->type == LND_NODE_SOURCE && e->src->outputs_count == 1) lnd_source_node_reset(e->src);
    }
}

static void lnd_instance_destroy(lnd_instance *i) {
    lnd_instance_close_stream(i);
    if (i->capture) {
        i->capture->instance = nullptr;
        lnd_capture_end(i->capture);
    }
    if (i->master) {

#if LND_MODULE_SINK
        lnd_sinks_detach_instance(i);
#endif
        lnd_instance_reset_sources(i);
        lnd_node_destroy(i->master);
    }
    lnd_free_aligned(i->scratch);
    lnd_free(i);
}

static void lnd_instance_log(const lnd_instance *i, const char *what) {
    LND_LOG_I("%s %s: %s %u Hz %u ch %s period %u buffer %u latency %u%s%s", i->capture ? "capture" : "instance", what, i->device->name, i->cfg.sample_rate_hz,
              i->cfg.channels, lnd_format_name(i->cfg.format), i->cfg.period_frames, i->cfg.buffer_frames, i->cfg.latency_frames,
              i->cfg.exclusive ? " exclusive" : "", i->cfg.loopback ? " loopback" : "");
}

static void lnd_instance_link(lnd_instance *i) {
    i->next = lnd_device_ctx.instances;
    if (i->next) i->next->prev = i;
    lnd_device_ctx.instances = i;
}

lnd_instance *lnd_instance_open_capture(lnd_device *d, uint32_t sample_rate_hz, uint32_t flags, bool follow_default) {
    if (!d) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_ctx.initialized) return lnd_error_null(LND_ERR_STATE);
    if (!lnd_device_ctx.backend.vt->open_capture) return lnd_error_null(LND_ERR_UNSUPPORTED);
    bool loopback = d->type == LND_DEVICE_OUTPUT;
    bool exclusive = (flags & LND_CAPTURE_EXCLUSIVE) != 0 || lnd_cfg_bool(LND_CFG_DEVICES_EXCLUSIVE)
                     || (!(d->flags & LND_DEVICE_FLAG_SHARED) && (d->flags & LND_DEVICE_FLAG_EXCLUSIVE));
    int32_t r = lnd_instance_check_capture_policy(d, exclusive, loopback);
    if (r != LND_OK) return lnd_error_null(r);
    lnd_instance *i = lnd_alloc_zero(sizeof *i);
    if (!i) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    i->device = d;
    lnd_store(&i->public_device, d);
    i->follow_default = follow_default;
    i->requested = (lnd_stream_cfg){
        .sample_rate_hz = exclusive ? lnd_device_nearest_exclusive_rate(d, sample_rate_hz ? sample_rate_hz : d->sample_rate_hz) : 0,
        .format = LND_FORMAT_F32,
        .period_frames = lnd_cfg_u32(LND_CFG_DEVICES_PERIOD_FRAMES),
        .periods = lnd_cfg_u32(LND_CFG_DEVICES_PERIODS),
        .exclusive = exclusive,
        .loopback = loopback,
        .priority = (int32_t)lnd_cfg_u32(LND_CFG_DEVICES_THREAD_PRIORITY),
    };
    i->cfg = i->requested;
    r = lnd_device_ctx.backend.vt->open_capture(&lnd_device_ctx.backend, d, &i->cfg, lnd_capture_instance_proc, i, &i->stream);
    if (r != LND_OK) {
        lnd_instance_destroy(i);
        return lnd_error_null(r);
    }
    uint64_t wanted = LND_MAX((uint64_t)lnd_cfg_u32(LND_CFG_GRAPH_BUFFER_FRAMES) * lnd_cfg_u32(LND_CFG_GRAPH_BUFFER_COUNT), (uint64_t)i->cfg.buffer_frames * 4);
    if (wanted > UINT32_MAX / 2u + 1u) {
        lnd_instance_destroy(i);
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    uint32_t ring = (uint32_t)wanted;
    i->capture = lnd_capture_new(i->cfg.channels, i->cfg.sample_rate_hz, ring);
    if (!i->capture) r = LND_ERR_OUT_OF_MEMORY;
    if (r == LND_OK) {
        i->capture->instance = i;
        i->capture->prefill = i->cfg.buffer_frames;
        r = lnd_device_attach_instance(d, i);
    }
    if (r == LND_OK) {
        lnd_instance_publish(i);
        r = lnd_device_ctx.backend.vt->start(i->stream);
        if (r != LND_OK) lnd_device_detach_instance(d, i);
    }
    if (r != LND_OK) {
        lnd_capture *c = i->capture;
        i->capture = nullptr;
        lnd_instance_destroy(i);
        lnd_capture_free(c);
        return lnd_error_null(r);
    }
    i->running = true;
    if (exclusive) d->exclusive_open = true;
    lnd_instance_link(i);
    lnd_instance_log(i, "opened");
    return i;
}

lnd_instance *lnd_instance_open(lnd_device *d, bool follow_default) {
    if (!d) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_ctx.initialized) return lnd_error_null(LND_ERR_STATE);
    bool exclusive = lnd_cfg_bool(LND_CFG_DEVICES_EXCLUSIVE)
                     || (!(d->flags & LND_DEVICE_FLAG_SHARED) && (d->flags & LND_DEVICE_FLAG_EXCLUSIVE));
    int32_t r = lnd_instance_check_policy(d, exclusive);
    if (r != LND_OK) return lnd_error_null(r);
    lnd_instance *i = lnd_alloc_zero(sizeof *i);
    if (!i) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    i->device = d;
    lnd_store(&i->public_device, d);
    i->follow_default = follow_default;
    i->requested = (lnd_stream_cfg){
        .sample_rate_hz = lnd_cfg_u32(LND_CFG_GRAPH_SAMPLE_RATE_HZ),
        .channels = lnd_cfg_u32(LND_CFG_GRAPH_CHANNELS),
        .format = (int32_t)lnd_cfg_u32(LND_CFG_DEVICES_FORMAT),
        .period_frames = lnd_cfg_u32(LND_CFG_DEVICES_PERIOD_FRAMES),
        .periods = lnd_cfg_u32(LND_CFG_DEVICES_PERIODS),
        .exclusive = exclusive,
        .priority = (int32_t)lnd_cfg_u32(LND_CFG_DEVICES_THREAD_PRIORITY),
    };
    i->cfg = i->requested;
    r = lnd_device_ctx.backend.vt->open(&lnd_device_ctx.backend, d, &i->cfg, lnd_instance_proc, i, &i->stream);
    if (r != LND_OK) {
        lnd_instance_destroy(i);
        return lnd_error_null(r);
    }
    i->master = lnd_node_create_bus(i->cfg.channels, i->cfg.sample_rate_hz, i);
    if (!i->master) r = LND_ERR_OUT_OF_MEMORY;
    if (r == LND_OK) r = lnd_instance_prepare_scratch(i);
    if (r == LND_OK) r = lnd_device_attach_instance(d, i);
    if (r == LND_OK) {
        lnd_instance_publish(i);
        r = lnd_device_ctx.backend.vt->start(i->stream);
        if (r != LND_OK) lnd_device_detach_instance(d, i);
    }
    if (r != LND_OK) {
        lnd_instance_destroy(i);
        return lnd_error_null(r);
    }
    i->running = true;
    if (exclusive) d->exclusive_open = true;
    lnd_instance_link(i);
    if (!lnd_device_ctx.default_output) lnd_context_set_default_output(i);
    lnd_instance_log(i, "opened");
    return i;
}

static void lnd_instance_unlink(lnd_instance *i) {
    lnd_device_detach_instance(i->device, i);
    if (i->cfg.exclusive) i->device->exclusive_open = false;
    if (i->prev)
        i->prev->next = i->next;
    else
        lnd_device_ctx.instances = i->next;
    if (i->next) i->next->prev = i->prev;
    if (lnd_device_ctx.default_output == i) lnd_context_set_default_output(nullptr);
}

int32_t lnd_instance_close(lnd_instance *i) {
    lnd_instance_stop_stream(i);
    lnd_instance_unlink(i);
    lnd_instance_destroy(i);
    return LND_OK;
}

void lnd_instances_stop_all(void) {
    for (lnd_instance *i = lnd_device_ctx.instances; i; i = i->next)
        lnd_instance_stop_stream(i);
}

void lnd_instances_destroy_all(void) {
    lnd_store(&lnd_default_events, 0);
    while (lnd_device_ctx.instances) {
        lnd_instance *i = lnd_device_ctx.instances;
        lnd_instance_unlink(i);
        lnd_instance_destroy(i);
    }
}

static int32_t lnd_instance_reopen(lnd_instance *i, lnd_device *d) {
    lnd_instance_close_stream(i);
    if (d != i->device) {
        int32_t r = i->capture ? lnd_instance_check_capture_policy(d, i->requested.exclusive, i->requested.loopback)
                               : lnd_instance_check_policy(d, i->requested.exclusive);
        if (r != LND_OK) return r;
        r = lnd_device_attach_instance(d, i);
        if (r != LND_OK) return r;
        lnd_device_detach_instance(i->device, i);
        if (i->requested.exclusive) i->device->exclusive_open = false;
        i->device = d;
        lnd_store(&i->public_device, d);
        if (i->requested.exclusive) d->exclusive_open = true;
    }
    lnd_stream_cfg cfg = i->requested;
    int32_t r = i->capture ? lnd_device_ctx.backend.vt->open_capture(&lnd_device_ctx.backend, d, &cfg, lnd_capture_instance_proc, i, &i->stream)
                           : lnd_device_ctx.backend.vt->open(&lnd_device_ctx.backend, d, &cfg, lnd_instance_proc, i, &i->stream);
    if (r != LND_OK) {
        i->stream = nullptr;
        return r;
    }
    i->cfg = cfg;
    if (i->capture) {
        r = lnd_capture_set_input_channels(i->capture, cfg.channels);
        if (r != LND_OK) {
            lnd_instance_close_stream(i);
            return r;
        }
        lnd_store(&i->capture->prefill, cfg.buffer_frames);
        lnd_store(&i->capture->sample_rate_hz, cfg.sample_rate_hz);
        lnd_add(&i->capture->generation, 1);
    } else {
        if (cfg.channels != i->master->channels || cfg.sample_rate_hz != i->master->sample_rate_hz) {
            r = lnd_node_reconfigure(i->master, cfg.channels, cfg.sample_rate_hz);
            if (r != LND_OK) return r;
        }
        r = lnd_instance_prepare_scratch(i);
        if (r != LND_OK) return r;
        i->cursor = i->master->produced;
    }
    lnd_instance_publish(i);
    r = lnd_device_ctx.backend.vt->start(i->stream);
    if (r != LND_OK) return r;
    i->running = true;
    lnd_instance_log(i, "reopened");
    return LND_OK;
}

void lnd_engine_maintain(void) {
    if (lnd_callback_active()) {
        lnd_error(LND_ERR_BUSY);
        return;
    }

    lnd_event_item items[32];
    uint32_t n = 0;
    lnd_context_lock();
    if (lnd_ctx.closing) {
        lnd_context_unlock();
        return;
    }
    uint32_t ev = 0;
    if (lnd_device_ctx.backend_ready && lnd_device_ctx.backend.vt->poll) ev = lnd_device_ctx.backend.vt->poll(&lnd_device_ctx.backend);
    ev |= lnd_exchange(&lnd_default_events, 0);
    if (ev) {
        lnd_devices_rescan_all();
        if (ev & LND_BACKEND_EVENT_DEVICES) items[n++] = (lnd_event_item){LND_DEVICE_EVENT_DEVICES_CHANGED, nullptr};
        if (ev & LND_BACKEND_EVENT_DEFAULT_OUTPUT) items[n++] = (lnd_event_item){LND_DEVICE_EVENT_DEFAULT_OUTPUT_CHANGED, nullptr};
        if (ev & LND_BACKEND_EVENT_DEFAULT_INPUT) items[n++] = (lnd_event_item){LND_DEVICE_EVENT_DEFAULT_INPUT_CHANGED, nullptr};
    }
    uint64_t now = lnd_time_ns();
    uint64_t interval = (uint64_t)lnd_cfg_u32(LND_CFG_DEVICES_REOPEN_INTERVAL_MS) * 1000000ull;
    bool follow = lnd_cfg_bool(LND_CFG_DEVICES_FOLLOW_DEFAULT);
    for (lnd_instance *i = lnd_device_ctx.instances; i; i = i->next) {
        bool failed = !i->running || !i->stream || (lnd_device_ctx.backend.vt->status && lnd_device_ctx.backend.vt->status(i->stream) != LND_OK);
        lnd_device *target = i->device;
        if (failed && i->running) {
            LND_LOG_W("instance failed: %s", i->device->name);
            lnd_instance_stop_stream(i);
        }
        bool capture_input = i->capture && !i->requested.loopback;
        int32_t type = capture_input ? LND_DEVICE_INPUT : LND_DEVICE_OUTPUT;
        uint32_t default_event = capture_input ? LND_BACKEND_EVENT_DEFAULT_INPUT : LND_BACKEND_EVENT_DEFAULT_OUTPUT;
        bool route_changed = false;
        if (i->follow_default && follow) {
            lnd_device *def = lnd_devices_default(type);
            if (def) {
                target = def;
                route_changed = (ev & default_event) || (!failed && target != i->device);
                if (route_changed) {
                    failed = true;
                    lnd_instance_stop_stream(i);
                }
            }
        }
        if (!failed) {
            if (lnd_device_ctx.backend.vt->get_latency_frames) {
                i->cfg.latency_frames = lnd_device_ctx.backend.vt->get_latency_frames(i->stream);
                lnd_store(&i->info[LND_INFO_LATENCY], i->cfg.latency_frames);
            }
            i->failed_reported = false;
            continue;
        }
        if (!i->failed_reported && n < LND_COUNTOF(items)) {
            i->failed_reported = true;
            items[n++] = (lnd_event_item){LND_DEVICE_EVENT_INSTANCE_FAILED, i};
        }
        if (!route_changed && i->last_reopen && now - i->last_reopen < interval) continue;
        i->last_reopen = now;
        if (target->flags & LND_DEVICE_FLAG_STALE) target = (i->follow_default && follow) ? lnd_devices_default(type) : nullptr;
        if (!target || (target->flags & LND_DEVICE_FLAG_STALE)) continue;
        int32_t r = lnd_instance_reopen(i, target);
        if (r == LND_OK) {
            i->failed_reported = false;
            if (n < LND_COUNTOF(items)) items[n++] = (lnd_event_item){LND_DEVICE_EVENT_INSTANCE_REOPENED, i};
        } else {
            LND_LOG_W("reopen failed: %s", LND_ErrorGetString(r));
        }
    }
    lnd_nodes_gc();
    lnd_modules_maintain();
    LND_DEVICE_PROC proc = lnd_cfg_ptr(LND_CFG_DEVICES_DEVICE_PROC);
    void *user = lnd_cfg_ptr(LND_CFG_DEVICES_DEVICE_USER);
    lnd_context_unlock();
    if (!proc) return;
    lnd_callback_enter();
    for (uint32_t k = 0; k < n; k++)
        proc(user, items[k].event, items[k].instance);
    lnd_callback_leave();
}

static void lnd_maint_proc(void *user) {
    LND_UNUSED(user);
    while (!lnd_load(&lnd_maint.stop)) {
        lnd_event_wait(&lnd_maint.event, 200);
        if (lnd_load(&lnd_maint.stop)) break;
        lnd_engine_maintain();
    }
}

int32_t lnd_engine_start(void) {
    if (lnd_maint.running) return LND_OK;
    lnd_store(&lnd_maint.stop, 0);
    int32_t r = lnd_event_init(&lnd_maint.event);
    if (r != LND_OK) return r;
    r = lnd_thread_create(&lnd_maint.thread, lnd_maint_proc, nullptr);
    if (r != LND_OK) {
        lnd_event_free(&lnd_maint.event);
        return r;
    }
    lnd_spinlock_lock(&lnd_maint.wake_lock);
    lnd_maint.running = true;
    lnd_spinlock_unlock(&lnd_maint.wake_lock);
    return LND_OK;
}

void lnd_engine_stop(void) {
    if (lnd_callback_active()) {
        lnd_error(LND_ERR_BUSY);
        return;
    }

    if (!lnd_maint.running) return;
    lnd_store(&lnd_maint.stop, 1);
    lnd_event_signal(&lnd_maint.event);
    lnd_context_unlock();
    lnd_thread_join(&lnd_maint.thread);
    lnd_context_lock();
    lnd_spinlock_lock(&lnd_maint.wake_lock);
    lnd_maint.running = false;
    lnd_event_free(&lnd_maint.event);
    lnd_spinlock_unlock(&lnd_maint.wake_lock);
}

void lnd_engine_wake(void) {
    if (!lnd_spinlock_try(&lnd_maint.wake_lock)) return;
    if (lnd_maint.running) lnd_event_signal(&lnd_maint.event);
    lnd_spinlock_unlock(&lnd_maint.wake_lock);
}

LND_DEVICE_INSTANCE *LND_DeviceInstanceOpen(LND_DEVICE *d) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    bool follow = lnd_device_is_default_handle(d);
    d = lnd_device_resolve(d);
    lnd_instance *i = d ? lnd_instance_open(d, follow) : lnd_error_null(LND_ERR_NO_DEVICE);
    lnd_context_unlock();
    return i;
}

int32_t LND_DeviceInstanceClose(LND_DEVICE_INSTANCE *i) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!i) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_context_gc();
    int32_t r = lnd_context_has_instance(i) ? lnd_instance_close(i) : LND_ERR_INVALID_ARG;
    lnd_context_unlock();
    return lnd_error(r);
}

LND_DEVICE_INSTANCE *LND_DeviceGetOutputInstance(void) {
    if (lnd_callback_active()) return lnd_cfg_ptr(LND_CFG_DEVICES_OUTPUT_INSTANCE);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_instance *instance = lnd_device_ctx.default_output;
    lnd_context_unlock();
    return instance;
}

LND_NODE *LND_DeviceGetOutputNode(void) {
    LND_DEVICE_INSTANCE *instance = LND_DeviceGetOutputInstance();
    return instance ? instance->master : nullptr;
}

LND_DEVICE_INSTANCE *LND_DeviceEnsureOutputInstance(void) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    lnd_instance *i = lnd_context_default_output();
    lnd_context_unlock();
    return i;
}

LND_DEVICE *LND_DeviceInstanceGetDevice(const LND_DEVICE_INSTANCE *i) { return i ? lnd_load(&i->public_device) : nullptr; }

LND_NODE *LND_DeviceInstanceGetNode(const LND_DEVICE_INSTANCE *i) { return i ? i->master : nullptr; }

uint32_t LND_DeviceInstanceGetSampleRateHz(const LND_DEVICE_INSTANCE *i) { return i ? lnd_load(&i->info[LND_INFO_RATE]) : 0; }

uint32_t LND_DeviceInstanceGetChannels(const LND_DEVICE_INSTANCE *i) { return i ? lnd_load(&i->info[LND_INFO_CHANNELS]) : 0; }

int32_t LND_DeviceInstanceGetFormat(const LND_DEVICE_INSTANCE *i) { return i ? (int32_t)lnd_load(&i->info[LND_INFO_FORMAT]) : LND_FORMAT_NONE; }

uint32_t LND_DeviceInstanceGetPeriodFrames(const LND_DEVICE_INSTANCE *i) { return i ? lnd_load(&i->info[LND_INFO_PERIOD]) : 0; }

uint32_t LND_DeviceInstanceGetBufferFrames(const LND_DEVICE_INSTANCE *i) { return i ? lnd_load(&i->info[LND_INFO_BUFFER]) : 0; }

uint32_t LND_DeviceInstanceGetLatencyFrames(const LND_DEVICE_INSTANCE *i) { return i ? lnd_load(&i->info[LND_INFO_LATENCY]) : 0; }

bool LND_DeviceInstanceIsExclusive(const LND_DEVICE_INSTANCE *i) { return i ? lnd_load(&i->info[LND_INFO_EXCLUSIVE]) : false; }

bool LND_DeviceInstanceIsRunning(const LND_DEVICE_INSTANCE *i) {
    if (!i) return false;
    if (lnd_callback_active()) return lnd_load(&i->running) != 0;
    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return false;
    }
    bool running = i->running && i->stream && (!lnd_device_ctx.backend.vt->status || lnd_device_ctx.backend.vt->status(i->stream) == LND_OK);
    lnd_context_unlock();
    return running;
}

uint64_t LND_DeviceInstanceGetPositionFrames(const LND_DEVICE_INSTANCE *i) { return i ? lnd_load(&i->position) : 0; }

LND_NODE *LND_DeviceEnsureOutputNode(void) {
    LND_DEVICE_INSTANCE *i = LND_DeviceEnsureOutputInstance();
    return i ? i->master : nullptr;
}
