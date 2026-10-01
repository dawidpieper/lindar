#include "lindar_null.h"
#include "src/callback.h"
#include "src/alloc.h"
#include "src/atomic.h"
#include "src/config.h"
#include "src/thread.h"
#include "src/format.h"
#include "io/devices/backend.h"

#include <string.h>

struct lnd_stream {
    lnd_stream_cfg cfg;
    lnd_stream_proc proc;
    void *user;
    lnd_thread thread;
    lnd_event stop;
    void *buffer;
    LND_NULL_SINK_PROC sink;
    void *sink_user;
    LND_NULL_SOURCE_PROC source;
    void *source_user;
    bool capture;
    bool realtime;
    bool running;
};

static int32_t lnd_null_init(lnd_backend *b) {
    LND_UNUSED(b);
    return LND_OK;
}

static void lnd_null_free(lnd_backend *b) { LND_UNUSED(b); }

static int32_t lnd_null_enumerate(lnd_backend *b, int32_t type, lnd_device_list *out) {
    LND_UNUSED(b);
    bool output = type == LND_DEVICE_OUTPUT;
    lnd_device *d = lnd_device_new(type, output ? "Null Output" : "Null Input", output ? "null:output" : "null:input");
    if (!d) return LND_ERR_OUT_OF_MEMORY;
    d->flags = LND_DEVICE_FLAG_SHARED | LND_DEVICE_FLAG_MULTI_INSTANCE | LND_DEVICE_FLAG_DEFAULT | (output ? LND_DEVICE_FLAG_LOOPBACK : 0);
    d->sample_rate_hz = 48000;
    d->channels = 2;
    d->format = LND_FORMAT_F32;
    d->default_period = 480;
    d->min_period = 48;
    d->max_instances = 0;
    static const uint32_t rates[] = {44100, 48000, 96000};
    int32_t r = LND_OK;
    for (size_t i = 0; i < LND_COUNTOF(rates) && r == LND_OK; i++) r = lnd_device_add_mode(d, rates[i], 2, LND_FORMAT_F32, LND_DEVICE_FLAG_SHARED);
    if (r == LND_OK) r = lnd_device_list_push(out, d);
    if (r != LND_OK) lnd_device_free(d);
    return r;
}

static uint32_t lnd_null_poll(lnd_backend *b) {
    LND_UNUSED(b);
    return 0;
}

static void lnd_null_thread(void *user) {
    lnd_stream *s = user;
    uint64_t period_ns = (uint64_t)s->cfg.period_frames * 1000000000ull / s->cfg.sample_rate_hz;
    uint64_t next = lnd_time_ns();
    size_t bytes = (size_t)s->cfg.period_frames * s->cfg.channels * lnd_format_bytes(s->cfg.format);
    for (;;) {
        if (s->capture) {
            if (s->source) {
                lnd_callback_enter();
                s->source(s->source_user, s->buffer, s->cfg.period_frames);
                lnd_callback_leave();
            } else memset(s->buffer, 0, bytes);
            s->proc(s->user, s->buffer, s->cfg.period_frames);
        } else {
            s->proc(s->user, s->buffer, s->cfg.period_frames);
            if (s->sink) {
                lnd_callback_enter();
                s->sink(s->sink_user, s->buffer, s->cfg.period_frames);
                lnd_callback_leave();
            }
        }
        if (s->realtime) {
            next += period_ns;
            uint64_t now = lnd_time_ns();
            uint32_t wait_ms = next > now ? (uint32_t)((next - now + 999999) / 1000000) : 0;
            if (lnd_event_wait(&s->stop, wait_ms)) break;
        } else if (lnd_event_wait(&s->stop, 0)) {
            break;
        }
    }
}

static int32_t lnd_null_open_common(lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out, bool capture) {
    if (capture && cfg->exclusive) return LND_ERR_UNSUPPORTED;
    lnd_stream *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    cfg->sample_rate_hz = cfg->sample_rate_hz ? cfg->sample_rate_hz : d->sample_rate_hz;
    cfg->channels = cfg->channels ? cfg->channels : d->channels;
    cfg->format = capture || !cfg->format ? LND_FORMAT_F32 : cfg->format;
    cfg->period_frames = cfg->period_frames ? cfg->period_frames : d->default_period;
    cfg->buffer_frames = cfg->period_frames * cfg->periods;
    cfg->latency_frames = cfg->buffer_frames;
    s->cfg = *cfg;
    s->proc = proc;
    s->user = user;
    s->capture = capture;
    s->sink = lnd_cfg_ptr(LND_CFG_NULL_SINK_PROC);
    s->sink_user = lnd_cfg_ptr(LND_CFG_NULL_SINK_USER);
    s->source = lnd_cfg_ptr(LND_CFG_NULL_SOURCE_PROC);
    s->source_user = lnd_cfg_ptr(LND_CFG_NULL_SOURCE_USER);
    s->realtime = lnd_cfg_bool(LND_CFG_NULL_REALTIME);
    s->buffer = lnd_alloc_aligned((size_t)cfg->period_frames * cfg->channels * lnd_format_bytes(cfg->format), LND_CACHE_LINE);
    if (!s->buffer || lnd_event_init(&s->stop) != LND_OK) {
        lnd_free_aligned(s->buffer);
        lnd_free(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    *out = s;
    return LND_OK;
}

static int32_t lnd_null_open(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    LND_UNUSED(b);
    return lnd_null_open_common(d, cfg, proc, user, out, false);
}

static int32_t lnd_null_open_capture(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    LND_UNUSED(b);
    return lnd_null_open_common(d, cfg, proc, user, out, true);
}

static int32_t lnd_null_start(lnd_stream *s) {
    if (s->running) return LND_OK;
    int32_t r = lnd_thread_create(&s->thread, lnd_null_thread, s);
    if (r == LND_OK) s->running = true;
    return r;
}

static int32_t lnd_null_stop(lnd_stream *s) {
    if (!s->running) return LND_OK;
    lnd_event_signal(&s->stop);
    lnd_thread_join(&s->thread);
    s->running = false;
    return LND_OK;
}

static int32_t lnd_null_status(lnd_stream *s) {
    LND_UNUSED(s);
    return LND_OK;
}

static void lnd_null_close(lnd_stream *s) {
    lnd_null_stop(s);
    lnd_event_free(&s->stop);
    lnd_free_aligned(s->buffer);
    lnd_free(s);
}

const lnd_backend_vt lnd_backend_null_vt = {
    .name = "null",
    .init = lnd_null_init,
    .free = lnd_null_free,
    .enumerate = lnd_null_enumerate,
    .poll = lnd_null_poll,
    .open = lnd_null_open,
    .open_capture = lnd_null_open_capture,
    .start = lnd_null_start,
    .stop = lnd_null_stop,
    .status = lnd_null_status,
    .close = lnd_null_close,
};

int32_t LND_NullSetOutputCallback(LND_NULL_SINK_PROC proc, void *user) { return lnd_config_set_callback(LND_CFG_NULL_SINK_PROC, LND_CFG_NULL_SINK_USER, (uintptr_t)proc, user); }
int32_t LND_NullSetInputCallback(LND_NULL_SOURCE_PROC proc, void *user) { return lnd_config_set_callback(LND_CFG_NULL_SOURCE_PROC, LND_CFG_NULL_SOURCE_USER, (uintptr_t)proc, user); }
