#include "io/devices/backend.h"
#include "src/alloc.h"
#include "src/atomic.h"
#include "src/thread.h"
#include "src/format.h"

#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct lnd_alsa {
    uint64_t scan_time;
    uint64_t devices;
} lnd_alsa;

struct lnd_stream {
    snd_pcm_t *pcm;
    lnd_stream_cfg cfg;
    lnd_stream_proc proc;
    void *user;
    void *buffer;
    lnd_thread thread;
    lnd_event stop;
    lnd_atomic_u32 failed;
    bool capture;
    bool running;
};

static int32_t lnd_alsa_error(int r) {
    if (r == -ENOMEM) return LND_ERR_OUT_OF_MEMORY;
    if (r == -EBUSY) return LND_ERR_BUSY;
    if (r == -ENODEV || r == -ENOENT) return LND_ERR_NO_DEVICE;
    if (r == -EINVAL) return LND_ERR_FORMAT;
    return LND_ERR_EXTERNAL;
}

static uint64_t lnd_alsa_hash(void) {
    void **hints = nullptr;
    uint64_t hash = 14695981039346656037ull;
    if (snd_device_name_hint(-1, "pcm", &hints) < 0) return 0;
    for (void **p = hints; *p; p++) {
        char *name = snd_device_name_get_hint(*p, "NAME");
        if (name) {
            for (const unsigned char *c = (const unsigned char *)name; *c; c++)
                hash = (hash ^ *c) * 1099511628211ull;
            hash = (hash ^ 255u) * 1099511628211ull;
        }
        free(name);
    }
    snd_device_name_free_hint(hints);
    return hash;
}

static int32_t lnd_alsa_init(lnd_backend *b) {
    lnd_alsa *a = lnd_alloc_zero(sizeof *a);
    if (!a) return LND_ERR_OUT_OF_MEMORY;
    a->devices = lnd_alsa_hash();
    b->data = a;
    return LND_OK;
}

static void lnd_alsa_free(lnd_backend *b) {
    lnd_free(b->data);
    b->data = nullptr;
}

static int32_t lnd_alsa_device(lnd_device_list *out, int32_t type, const char *name, const char *label) {
    for (uint32_t i = 0; i < out->count; i++)
        if (!strcmp(out->items[i]->id, name)) return LND_OK;
    lnd_device *d = lnd_device_new(type, label ? label : name, name);
    if (!d) return LND_ERR_OUT_OF_MEMORY;
    for (char *p = d->name; *p; p++)
        if (*p == '\n' || *p == '\r') *p = ' ';
    bool hw = !strncmp(name, "hw:", 3);
    d->flags = LND_DEVICE_FLAG_SHARED | (hw ? LND_DEVICE_FLAG_EXCLUSIVE : LND_DEVICE_FLAG_MULTI_INSTANCE);
    if (!strcmp(name, "default")) d->flags |= LND_DEVICE_FLAG_DEFAULT;
    d->max_instances = hw ? 1 : 0;
    d->sample_rate_hz = 48000;
    d->channels = type == LND_DEVICE_OUTPUT ? 2 : 1;
    d->format = hw ? LND_FORMAT_S16 : LND_FORMAT_F32;
    d->default_period = 480;
    d->min_period = 1;
    int32_t r = lnd_device_list_push(out, d);
    if (r != LND_OK) lnd_device_free(d);
    return r;
}

static int32_t lnd_alsa_enumerate(lnd_backend *b, int32_t type, lnd_device_list *out) {
    int32_t r = lnd_alsa_device(out, type, "default", type == LND_DEVICE_OUTPUT ? "System Output" : "System Input");
    void **hints = nullptr;
    if (r != LND_OK || snd_device_name_hint(-1, "pcm", &hints) < 0) return r;
    for (void **p = hints; *p && r == LND_OK; p++) {
        char *name = snd_device_name_get_hint(*p, "NAME");
        char *desc = snd_device_name_get_hint(*p, "DESC");
        char *io = snd_device_name_get_hint(*p, "IOID");
        if (name && (!io || !strcmp(io, type == LND_DEVICE_OUTPUT ? "Output" : "Input"))) r = lnd_alsa_device(out, type, name, desc);
        free(name);
        free(desc);
        free(io);
    }
    snd_device_name_free_hint(hints);
    return r;
}

static uint32_t lnd_alsa_poll(lnd_backend *b) {
    lnd_alsa *a = b->data;
    uint64_t now = lnd_time_ns();
    if (now - a->scan_time < 1000000000ull) return 0;
    a->scan_time = now;
    uint64_t devices = lnd_alsa_hash();
    if (!devices || devices == a->devices) return 0;
    a->devices = devices;
    return LND_BACKEND_EVENT_DEVICES | LND_BACKEND_EVENT_DEFAULT_INPUT | LND_BACKEND_EVENT_DEFAULT_OUTPUT;
}

static const snd_pcm_format_t lnd_alsa_formats[] = {
    SND_PCM_FORMAT_UNKNOWN, SND_PCM_FORMAT_U8,    SND_PCM_FORMAT_S16_LE,  SND_PCM_FORMAT_S24_3LE,
    SND_PCM_FORMAT_S32_LE,  SND_PCM_FORMAT_FLOAT, SND_PCM_FORMAT_FLOAT64,
};

static int32_t lnd_alsa_open_common(lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out, bool capture) {
    if (cfg->loopback || (cfg->exclusive && strncmp(d->id, "hw:", 3))) return LND_ERR_UNSUPPORTED;
    if (cfg->format < LND_FORMAT_NONE || cfg->format > LND_FORMAT_F64 || cfg->channels > LND_MAX_CHANNELS) return LND_ERR_FORMAT;
    lnd_stream *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    snd_pcm_hw_params_t *hw = nullptr;
    snd_pcm_sw_params_t *sw = nullptr;
    int r = snd_pcm_open(&s->pcm, d->id, capture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (r < 0) goto fail;
    if ((r = snd_pcm_hw_params_malloc(&hw)) < 0 || (r = snd_pcm_sw_params_malloc(&sw)) < 0) goto fail;
    if ((r = snd_pcm_hw_params_any(s->pcm, hw)) < 0 || (r = snd_pcm_hw_params_set_access(s->pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) goto fail;
    int32_t format = cfg->format ? cfg->format : d->format;
    if (!cfg->format && snd_pcm_hw_params_test_format(s->pcm, hw, lnd_alsa_formats[format]) < 0) format = LND_FORMAT_S16;
    if ((r = snd_pcm_hw_params_set_format(s->pcm, hw, lnd_alsa_formats[format])) < 0) goto fail;
    unsigned sample_rate_hz = cfg->sample_rate_hz ? cfg->sample_rate_hz : d->sample_rate_hz;
    unsigned channels = cfg->channels ? cfg->channels : d->channels;
    if ((r = snd_pcm_hw_params_set_rate_near(s->pcm, hw, &sample_rate_hz, nullptr)) < 0) goto fail;
    if (cfg->channels)
        r = snd_pcm_hw_params_set_channels(s->pcm, hw, channels);
    else
        r = snd_pcm_hw_params_set_channels_near(s->pcm, hw, &channels);
    if (r < 0) goto fail;
    snd_pcm_uframes_t period = cfg->period_frames ? cfg->period_frames : LND_MAX(1u, sample_rate_hz / 100);
    if ((r = snd_pcm_hw_params_set_period_size_near(s->pcm, hw, &period, nullptr)) < 0) goto fail;
    uint64_t wanted = (uint64_t)period * LND_MAX(cfg->periods, 2u);
    if (wanted > UINT32_MAX || !channels || channels > LND_MAX_CHANNELS || !sample_rate_hz) {
        r = -EINVAL;
        goto fail;
    }
    snd_pcm_uframes_t buffer = (snd_pcm_uframes_t)wanted;
    if ((r = snd_pcm_hw_params_set_buffer_size_near(s->pcm, hw, &buffer)) < 0 || (r = snd_pcm_hw_params(s->pcm, hw)) < 0 ||
        (r = snd_pcm_get_params(s->pcm, &buffer, &period)) < 0)
        goto fail;
    if (!period || period > UINT32_MAX || buffer > UINT32_MAX || period > SIZE_MAX / channels / lnd_format_bytes(format)) {
        r = -EINVAL;
        goto fail;
    }
    if ((r = snd_pcm_sw_params_current(s->pcm, sw)) < 0 || (r = snd_pcm_sw_params_set_avail_min(s->pcm, sw, period)) < 0 ||
        (r = snd_pcm_sw_params_set_start_threshold(s->pcm, sw, capture ? 1 : buffer - buffer % period)) < 0 || (r = snd_pcm_sw_params(s->pcm, sw)) < 0)
        goto fail;
    cfg->sample_rate_hz = sample_rate_hz;
    cfg->channels = channels;
    cfg->format = format;
    cfg->period_frames = (uint32_t)period;
    cfg->buffer_frames = (uint32_t)buffer;
    cfg->latency_frames = (uint32_t)buffer;
    s->cfg = *cfg;
    s->proc = proc;
    s->user = user;
    s->capture = capture;
    s->buffer = lnd_alloc_aligned((size_t)period * channels * lnd_format_bytes(format), LND_CACHE_LINE);
    if (!s->buffer) {
        r = -ENOMEM;
        goto fail;
    }
    if (lnd_event_init(&s->stop) != LND_OK) {
        r = -ENOMEM;
        goto fail;
    }
    snd_pcm_sw_params_free(sw);
    snd_pcm_hw_params_free(hw);
    *out = s;
    return LND_OK;
fail:
    if (sw) snd_pcm_sw_params_free(sw);
    if (hw) snd_pcm_hw_params_free(hw);
    if (s->pcm) snd_pcm_close(s->pcm);
    lnd_free_aligned(s->buffer);
    lnd_free(s);
    return lnd_alsa_error(r);
}

static int32_t lnd_alsa_open(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    return lnd_alsa_open_common(d, cfg, proc, user, out, false);
}

static int32_t lnd_alsa_open_capture(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    return lnd_alsa_open_common(d, cfg, proc, user, out, true);
}

static int lnd_alsa_recover(lnd_stream *s, int error) {
    if (error == -EINTR || error == -EAGAIN) return 0;
    if (error == -ESTRPIPE) {
        int r = snd_pcm_resume(s->pcm);
        if (r == -EAGAIN) return r;
        if (r >= 0) return 0;
    } else if (error != -EPIPE)
        return error;
    int r = snd_pcm_prepare(s->pcm);
    return r < 0 || !s->capture ? r : snd_pcm_start(s->pcm);
}

static void lnd_alsa_thread(void *user) {
    lnd_stream *s = user;
    lnd_thread_set_priority(s->cfg.priority);
    size_t frame_bytes = (size_t)s->cfg.channels * lnd_format_bytes(s->cfg.format);
    uint32_t offset = s->cfg.period_frames;
    while (!lnd_event_wait(&s->stop, 0)) {
        if (!s->capture && offset == s->cfg.period_frames) {
            s->proc(s->user, s->buffer, s->cfg.period_frames);
            offset = 0;
        }
        snd_pcm_sframes_t n = s->capture ? snd_pcm_readi(s->pcm, s->buffer, s->cfg.period_frames)
                                         : snd_pcm_writei(s->pcm, (unsigned char *)s->buffer + (size_t)offset * frame_bytes, s->cfg.period_frames - offset);
        if (n > 0) {
            if (s->capture)
                s->proc(s->user, s->buffer, (uint64_t)n);
            else
                offset += (uint32_t)n;
            continue;
        }
        if (n == 0 || n == -EAGAIN) n = snd_pcm_wait(s->pcm, 20);
        if (n < 0) {
            int r = lnd_alsa_recover(s, (int)n);
            if (r == -EAGAIN) {
                if (lnd_event_wait(&s->stop, 10)) break;
            } else if (r < 0) {
                lnd_store(&s->failed, 1);
                break;
            }
        }
    }
}

static int32_t lnd_alsa_start(lnd_stream *s) {
    if (s->running) return LND_OK;
    lnd_event_wait(&s->stop, 0);
    lnd_store(&s->failed, 0);
    int r = snd_pcm_prepare(s->pcm);
    if (r >= 0 && s->capture) r = snd_pcm_start(s->pcm);
    if (r < 0) return lnd_alsa_error(r);
    r = lnd_thread_create(&s->thread, lnd_alsa_thread, s);
    if (r == LND_OK)
        s->running = true;
    else
        snd_pcm_drop(s->pcm);
    return r;
}

static int32_t lnd_alsa_stop(lnd_stream *s) {
    if (!s->running) return LND_OK;
    lnd_event_signal(&s->stop);
    lnd_thread_join(&s->thread);
    snd_pcm_drop(s->pcm);
    s->running = false;
    return LND_OK;
}

static int32_t lnd_alsa_status(lnd_stream *s) { return lnd_load(&s->failed) ? LND_ERR_EXTERNAL : LND_OK; }

static void lnd_alsa_close(lnd_stream *s) {
    lnd_alsa_stop(s);
    snd_pcm_close(s->pcm);
    lnd_event_free(&s->stop);
    lnd_free_aligned(s->buffer);
    lnd_free(s);
}

const lnd_backend_vt lnd_backend_alsa_vt = {
    .name = "alsa",
    .init = lnd_alsa_init,
    .free = lnd_alsa_free,
    .enumerate = lnd_alsa_enumerate,
    .poll = lnd_alsa_poll,
    .open = lnd_alsa_open,
    .open_capture = lnd_alsa_open_capture,
    .start = lnd_alsa_start,
    .stop = lnd_alsa_stop,
    .status = lnd_alsa_status,
    .close = lnd_alsa_close,
};
