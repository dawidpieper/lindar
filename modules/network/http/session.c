#include "http.h"
#include "pcm/audio/channels.h"
#include "src/native.h"
#include "lnd_http_transports.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#if LND_OS_MODE && !LND_THREADS
#if LND_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif
#endif

static struct {
    lnd_mutex mutex;
    lnd_thread thread;
    lnd_event event;
    lnd_atomic_u32 stop;
    lnd_http_session *sessions;
    LND_HTTP_OPEN *opens;
    uint64_t now;
    uint64_t round[2];
    bool initialized;
    bool running;
} lnd_http;

static uint64_t lnd_http_clock(void) {
#if LND_THREADS
    return lnd_time_ns() / 1000000;
#elif LND_OS_MODE && LND_OS_WINDOWS
    return GetTickCount64();
#elif LND_OS_MODE
    struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) == 0 ? (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000 : 0;
#else
    return lnd_http.now;
#endif
}

static bool lnd_http_open_expired(const lnd_http_session *s) {
    return s->open_deadline && (s->worker ? lnd_http_clock() : s->now) >= s->open_deadline;
}

void LND_HttpOptionsInit(LND_HTTP_OPTIONS *o) {
    if (!o) return;
    *o = (LND_HTTP_OPTIONS){.size = sizeof *o,
                            .user_agent = "Lindar",
                            .max_redirects = 8,
                            .buffer = {.start_ms = 500,
                                       .resume_ms = 1000,
                                       .pcm_ms = 2000,
                                       .compressed_bytes = 4 * 1024 * 1024,
                                       .segment_bytes = 16 * 1024 * 1024,
                                       .playlist_bytes = 1024 * 1024,
                                       .playlist_entries = 8192,
                                       .event_count = 64},
                            .retry = {.attempts = 5, .delay_ms = 250, .max_delay_ms = 10000, .connect_timeout_ms = 10000, .receive_timeout_ms = 15000},
                            .file = {.max_bytes = LND_MODULE_HTTP_FILE ? 1024ull * 1024 * 1024 : 0},
                            .hls = {.adaptive = true}};
}

uint32_t LND_HttpGetCapabilities(void) {
    uint32_t caps = LND_HTTP_CAP_MANUAL;
#if LND_MODULE_HTTP_FILE
    caps |= LND_HTTP_CAP_FILE_CACHE;
#endif
#if LND_THREADS
    caps |= LND_HTTP_CAP_WORKER;
#endif
    for (size_t i = 0; lnd_http_transports[i]; i++) caps |= lnd_http_transports[i]->capabilities;
#if LND_HTTP_ICY
    caps |= LND_HTTP_CAP_ICY;
#endif
#if LND_HTTP_MP4
    caps |= LND_HTTP_CAP_MP4_RANGE;
#endif
#if LND_HTTP_HLS
    caps |= LND_HTTP_CAP_HLS;
#endif
#if LND_HTTP_LL_HLS
    caps |= LND_HTTP_CAP_LL_HLS;
#endif
#if LND_HTTP_AES128
    caps |= LND_HTTP_CAP_AES128;
#endif
    return caps;
}

void lnd_http_event(lnd_http_session *s, int32_t type, int32_t result, const char *text) {
    if (s->event_count == s->options.buffer.event_count) {
        s->event_head = (s->event_head + 1) % s->options.buffer.event_count;
        s->event_count--;
        s->stats.events_lost++;
    }
    uint32_t at = (s->event_head + s->event_count++) % s->options.buffer.event_count;
    LND_HTTP_EVENT *event = &s->events[at];
    *event =
        (LND_HTTP_EVENT){.type = type,
                         .state = s->info.state,
                         .result = result,
                         .request_id = s->request_id,
                         .position_us = s->info.sample_rate_hz ? (int64_t)(lnd_load(&s->played) / s->info.sample_rate_hz * 1000000 +
                                                                           lnd_load(&s->played) % s->info.sample_rate_hz * 1000000 / s->info.sample_rate_hz)
                                                               : 0};
    if (text) snprintf(event->text, sizeof event->text, "%s", text);
}

void lnd_http_state(lnd_http_session *s, int32_t state) {
    if (s->info.state == state) return;
    s->info.state = state;
    lnd_http_event(s, LND_HTTP_EVENT_STATE, LND_OK, nullptr);
}

void lnd_http_fail(lnd_http_session *s, int32_t error, const char *message) {
    if (s->info.state == LND_HTTP_FAILED) return;
    s->info.error = (LND_HTTP_ERROR){.code = error, .http_status = s->response.status, .backend_code = s->response.backend_code};
    if (message) snprintf(s->info.error.message, sizeof s->info.error.message, "%s", message);
    lnd_http_state(s, LND_HTTP_FAILED);
    lnd_http_event(s, LND_HTTP_EVENT_ERROR, error, message);
    if (error == LND_HTTP_ERR_TIMEOUT) {
        lnd_spinlock_lock(&s->pcm_lock);
        s->count = 0;
        lnd_spinlock_unlock(&s->pcm_lock);
    }
    lnd_store(&s->terminal, error);
    s->finished = true;
    lnd_http_request_close(s);
    lnd_http_protocol_free(s);
}

void lnd_http_request_close(lnd_http_session *s) {
    if (s->transfer) {
        lnd_callback_enter();
        s->transport->close(s->transfer);
        lnd_callback_leave();
    }
    s->transfer = nullptr;
    lnd_free(s->request_url);
    s->request_url = nullptr;
}

int32_t lnd_http_poll(lnd_http_session *s, void *data, size_t capacity, size_t *written) {
    lnd_callback_enter();
    int32_t r = s->transport->poll(s->transfer, &s->response, data, capacity, written);
    lnd_callback_leave();
    return r;
}

int32_t lnd_http_request_open(lnd_http_session *s, const char *url, const char *validator, bool range, uint64_t start, uint64_t length, void **transfer) {
    if (!lnd_http_url_valid(url) || (s->options.header_count && !s->headers)) return LND_ERR_INVALID_ARG;
    LND_HTTP_HEADER *headers = s->headers ? s->headers + s->options.header_count : nullptr;
    size_t header_count = 0;
    for (size_t i = 0; i < s->options.header_count; i++)
        if (!s->headers[i].origin || lnd_http_same_origin(s->headers[i].origin, url)) headers[header_count++] = s->headers[i];
    LND_HTTP_REQUEST request = {.url = url,
                                .user_agent = s->options.user_agent,
                                .headers = headers,
                                .header_count = header_count,
                                .proxy = s->options.proxy,
                                .ca_file = s->options.ca_file,
                                .if_range = validator && *validator ? validator : nullptr,
                                .range = range,
                                .range_start_bytes = start,
                                .range_length_bytes = length,
                                .flags = s->options.flags & ~(uint32_t)(LND_HTTP_PROBE_DURATION | LND_HTTP_NO_PROBE_DURATION),
                                .connect_timeout_ms = s->options.retry.connect_timeout_ms,
                                .receive_timeout_ms = s->options.retry.receive_timeout_ms,
                                .max_redirects = s->options.max_redirects,
                                .manual = !s->worker};
    lnd_callback_enter();
    int32_t r = s->transport->open(s->transport_user, &request, transfer);
    lnd_callback_leave();
    if (r != LND_OK || !*transfer) return r < 0 ? r : LND_ERR_IO;
    s->stats.requests++;
    return LND_OK;
}

int32_t lnd_http_request(lnd_http_session *s, const char *url, bool range, uint64_t start, uint64_t length) {
    if (!lnd_http_url_valid(url)) return LND_ERR_INVALID_ARG;
    char *copy = lnd_http_copy(url);
    if (!copy) return LND_ERR_OUT_OF_MEMORY;
    lnd_http_request_close(s);
    s->request_url = copy;
    for (char *p = copy; *p && *p != ':'; p++)
        if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
    s->response = (LND_HTTP_RESPONSE){0};
    int32_t r = lnd_http_request_open(s, copy, s->if_range, range, start, length, &s->transfer);
    if (r != LND_OK) lnd_http_request_close(s);
    s->last_data = s->now;
    return r;
}

static void lnd_http_destroy(lnd_http_session *s) {
    lnd_http_request_close(s);
    lnd_http_protocol_free(s);
    if (s->transport_owned) {
        lnd_callback_enter();
        s->transport->session_close(s->transport_user);
        lnd_callback_leave();
    }
    LND_DecoderFree(s->decoder);
    if (s->resampler) lnd_source_free(s->resampler);
    for (size_t i = 0; i < s->options.header_count; i++) {
        lnd_free((void *)s->headers[i].name);
        lnd_free((void *)s->headers[i].value);
        lnd_free((void *)s->headers[i].origin);
    }
    lnd_free(s->headers);
    lnd_free((void *)s->options.user_agent);
    lnd_free((void *)s->options.codec_name);
    lnd_free((void *)s->options.proxy);
    lnd_free((void *)s->options.ca_file);
    lnd_free((void *)s->options.file.directory);
    lnd_free((void *)s->options.hls.preferred_language);
    lnd_free(s->events);
    lnd_http_metadata_clear(s);
#if LND_MODULE_METADATA
    LND_MetadataFree(s->tags);
#endif
    lnd_free(s->metadata);
    lnd_free(s->url);
    lnd_free(s->ring);
    lnd_free(s->scratch);
    lnd_http_bytes_free(&s->input);
    lnd_free(s);
}

static uint64_t lnd_http_decoder_read(lnd_source *source, float *dst, uint64_t frames) {
    lnd_http_session *s = (lnd_http_session *)((char *)source - offsetof(lnd_http_session, decode_source));
    LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = source->channels, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_INTERLEAVED};
    int64_t got = LND_DecoderReadPcm(s->decoder, &pcm, 0, (size_t)frames);
    lnd_store(&source->status, LND_DecoderGetStatus(s->decoder));
    if (got > 0) lnd_store_relaxed(&source->pos, lnd_load_relaxed(&source->pos) + (uint64_t)got);
    return got < 0 ? 0 : (uint64_t)got;
}

static void lnd_http_decoder_no_free(lnd_source *source) {}
static const lnd_source_vt lnd_http_decoder_vt = {.read = lnd_http_decoder_read, .free = lnd_http_decoder_no_free};

static void lnd_http_decoder_duration(lnd_http_session *s, const LND_CODEC_INFO *info) {
    if (s->info.hls || s->info.length_kind == LND_LENGTH_EXACT || (!info->length_known && !info->length_frames) || !info->sample_rate_hz) return;
    uint64_t seconds = info->length_frames / info->sample_rate_hz;
    uint64_t fraction = info->length_frames % info->sample_rate_hz * 1000000 / info->sample_rate_hz;
    if (seconds > ((uint64_t)INT64_MAX - fraction) / 1000000) return;
    s->info.duration_us = s->info.seek_end_us = (int64_t)(seconds * 1000000 + fraction);
    s->info.length_kind = info->length_estimated ? LND_LENGTH_ESTIMATED : LND_LENGTH_EXACT;
}

static int32_t lnd_http_prepare(lnd_http_session *s) {
    LND_CODEC_INFO info;
    if (LND_DecoderGetInfo(s->decoder, &info) != LND_OK) return LND_HTTP_PENDING;
    lnd_http_decoder_duration(s, &info);
    if (s->ring) {
        if (info.channels != s->format.channels || info.sample_rate_hz != s->format.sample_rate_hz) {
            if (!info.channels || info.channels > LND_MAX_CHANNELS || !info.sample_rate_hz || info.sample_rate_hz > 768000) return LND_ERR_FORMAT;
            if (info.channels > s->scratch_channels) {
                float *scratch = lnd_realloc(s->scratch, 1024 * info.channels * sizeof(float));
                if (!scratch) return LND_ERR_OUT_OF_MEMORY;
                s->scratch = scratch;
                s->scratch_channels = info.channels;
            }
            if (s->resampler) lnd_source_free(s->resampler);
            s->resampler = nullptr;
            s->format = info;
            s->decode_source = (lnd_source){.vt = &lnd_http_decoder_vt, .channels = info.channels, .sample_rate_hz = info.sample_rate_hz, .live = true};
            if (s->info.sample_rate_hz != info.sample_rate_hz) {
                s->resampler = lnd_resample_source_create(&s->decode_source, false, s->info.sample_rate_hz, 1024, 2);
                if (!s->resampler) return LND_ERR_OUT_OF_MEMORY;
            }
        }
        return LND_OK;
    }
    uint32_t sample_rate_hz = s->options.output_sample_rate_hz ? s->options.output_sample_rate_hz : info.sample_rate_hz;
    uint32_t channels = s->options.output_channels ? s->options.output_channels : info.channels;
    uint64_t capacity = (uint64_t)sample_rate_hz * s->options.buffer.pcm_ms / 1000;
    if (!capacity || capacity > INT32_MAX || capacity > SIZE_MAX / (channels * sizeof(float))) return LND_ERR_INVALID_ARG;
    s->ring = lnd_alloc((size_t)capacity * channels * sizeof(float));
    s->scratch = lnd_alloc(1024 * info.channels * sizeof(float));
    s->scratch_channels = info.channels;
    if (!s->ring || !s->scratch) return LND_ERR_OUT_OF_MEMORY;
    s->capacity = (uint32_t)capacity;
    s->format = info;
    s->info.sample_rate_hz = sample_rate_hz;
    s->info.channels = channels;
    const LND_CODEC *codec = LND_DecoderGetCodec(s->decoder);
    if (codec) snprintf(s->info.codec, sizeof s->info.codec, "%s", codec->name);
    s->decode_source = (lnd_source){.vt = &lnd_http_decoder_vt, .channels = info.channels, .sample_rate_hz = info.sample_rate_hz, .live = true};
    if (sample_rate_hz != info.sample_rate_hz) {
        s->resampler = lnd_resample_source_create(&s->decode_source, false, sample_rate_hz, 1024, 2);
        if (!s->resampler) return LND_ERR_OUT_OF_MEMORY;
    }
    s->ready = true;
    s->buffering = true;
    lnd_http_state(s, LND_HTTP_BUFFERING);
    return LND_OK;
}

int32_t lnd_http_decode(lnd_http_session *s) {
    int32_t status = LND_DecoderStep(s->decoder);
    lnd_http_decoder_metadata(s);
    if (status < 0) return status;
    int32_t result = lnd_http_prepare(s);
    if (result != LND_OK) return result;
    if (!lnd_spinlock_try(&s->pcm_lock)) return LND_HTTP_PENDING;
    uint32_t available = s->capacity - s->count;
    lnd_spinlock_unlock(&s->pcm_lock);
    if (!available) return LND_HTTP_PENDING;
    uint32_t want = LND_MIN(available, 1024u);
    lnd_source *input = s->resampler ? s->resampler : &s->decode_source;
    uint64_t got = lnd_source_read(input, s->scratch, want);
    status = lnd_source_status(input);
    if (lnd_http_open_expired(s)) return LND_HTTP_ERR_TIMEOUT;
    if (status < 0) return status;
    if (s->discard_us > 0) {
        uint64_t us = (uint64_t)s->discard_us;
        s->discard_frames = us / 1000000 * s->info.sample_rate_hz + (us % 1000000 * s->info.sample_rate_hz + (s->discard_round ? 500000 : 0)) / 1000000;
        s->discard_us = 0;
    }
    size_t skip = (size_t)LND_MIN(got, s->discard_frames);
    s->discard_frames -= skip;
    s->stats.decoded_frames += got;
    got -= skip;
    float *samples = s->scratch + skip * s->format.channels;
    if (got) {
        if (!lnd_spinlock_try(&s->pcm_lock)) {
            lnd_spinlock_lock(&s->pcm_lock);
        }
        uint32_t at = (s->head + s->count) % s->capacity;
        uint32_t first = LND_MIN((uint32_t)got, s->capacity - at);
        lnd_channels_map(samples, s->format.channels, s->ring + (size_t)at * s->info.channels, s->info.channels, first);
        if (got > first) lnd_channels_map(samples + (size_t)first * s->format.channels, s->format.channels, s->ring, s->info.channels, (size_t)got - first);
        s->count += (uint32_t)got;
        if (s->seek_commit) {
            uint64_t us = (uint64_t)s->seek_target_us;
            lnd_store(&s->played, us / 1000000 * s->info.sample_rate_hz + us % 1000000 * s->info.sample_rate_hz / 1000000);
        }
        uint32_t threshold = (uint32_t)((uint64_t)(s->stats.stalls ? s->options.buffer.resume_ms : s->options.buffer.start_ms) * s->info.sample_rate_hz / 1000);
        if (s->buffering && s->count >= threshold) s->buffering = false;
        if (!s->buffering) s->open_deadline = 0;
        bool buffering = s->buffering;
        lnd_spinlock_unlock(&s->pcm_lock);
        if (s->seek_commit) {
            s->seek_commit = false;
            lnd_http_event(s, LND_HTTP_EVENT_SEEK, LND_OK, nullptr);
        }
        if (!buffering && s->info.state != LND_HTTP_DRAINING) lnd_http_state(s, LND_HTTP_READY);
    }
    if (status == LND_SOURCE_EOF) s->decoder_end = true;
    if (s->decoder_end && s->input_end) {
        s->open_deadline = 0;
        LND_CODEC_INFO info;
        if (LND_DecoderGetInfo(s->decoder, &info) == LND_OK) lnd_http_decoder_duration(s, &info);
        if (s->seek_commit) {
            s->seek_commit = false;
            uint64_t us = (uint64_t)s->seek_target_us;
            lnd_store(&s->played, us / 1000000 * s->info.sample_rate_hz + us % 1000000 * s->info.sample_rate_hz / 1000000);
            lnd_http_event(s, LND_HTTP_EVENT_SEEK, LND_OK, nullptr);
        }
        s->finished = true;
        lnd_store(&s->terminal, LND_READ_EOF);
        lnd_http_state(s, LND_HTTP_DRAINING);
    }
    return LND_OK;
}

int32_t lnd_http_feed(lnd_http_session *s, const uint8_t *data, size_t bytes, size_t *used) {
    *used = 0;
    uint32_t interval = s->response.icy_interval_bytes;
#if !LND_HTTP_ICY
    if (interval) return LND_ERR_UNSUPPORTED;
#endif
    while (*used < bytes) {
        if (interval && !s->icy_remaining) {
            if (!s->icy_size && !s->icy_received) {
                s->icy_size = data[(*used)++] * 16u;
                if (!s->icy_size) s->icy_remaining = interval;
                continue;
            }
            size_t take = LND_MIN(bytes - *used, s->icy_size - s->icy_received);
            memcpy(s->icy + s->icy_received, data + *used, take);
            s->icy_received += (uint32_t)take;
            *used += take;
            if (s->icy_received == s->icy_size) {
                s->icy[s->icy_size] = 0;
                char *title = strstr(s->icy, "StreamTitle='");
                if (title) {
                    title += 13;
                    char *end = strstr(title, "';");
                    if (end) *end = 0;
                    lnd_http_metadata_icy(s, title);
                }
                s->icy_size = s->icy_received = 0;
                s->icy_remaining = interval;
            }
            continue;
        }
        size_t take = interval ? LND_MIN(bytes - *used, s->icy_remaining) : bytes - *used;
        int64_t got = LND_DecoderFeed(s->decoder, data + *used, take);
        if (got < 0) return (int32_t)got;
        *used += (size_t)got;
        if (interval) s->icy_remaining -= (uint32_t)got;
        if ((size_t)got < take) return LND_HTTP_PENDING;
    }
    return LND_OK;
}

static int64_t lnd_http_read(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_http_session *s = user;
    if (!lnd_spinlock_try(&s->pcm_lock)) return 0;
    int32_t terminal = lnd_load(&s->terminal);
    if (s->buffering && !terminal) {
        lnd_spinlock_unlock(&s->pcm_lock);
        return 0;
    }
    size_t take = LND_MIN(frames, s->count);
    size_t first = LND_MIN(take, s->capacity - s->head);
    LND_PCM ring = {.data = s->ring, .frames = s->capacity, .channels = s->info.channels, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_INTERLEAVED};
    int32_t r = LND_PcmConvert(pcm, offset, &ring, s->head, first);
    if (r == LND_OK && take > first) r = LND_PcmConvert(pcm, offset + first, &ring, 0, take - first);
    if (r == LND_OK) {
        s->head = (uint32_t)((s->head + take) % s->capacity);
        s->count -= (uint32_t)take;
        lnd_add(&s->played, take);
        if (take < frames && !terminal) {
            s->buffering = true;
            lnd_store(&s->stalled, 1);
        }
    }
    lnd_spinlock_unlock(&s->pcm_lock);
    if (r != LND_OK) return r;
    if (!take && terminal) return terminal;
    return (int64_t)take;
}

static void lnd_http_source_close(void *user) { lnd_store(&((lnd_http_session *)user)->detached, 1); }

static void lnd_http_tick(bool worker, uint64_t now, uint32_t budget) {
    lnd_mutex_lock(&lnd_http.mutex);
    if (now < lnd_http.now) now = lnd_http.now;
    lnd_http.now = now;
    uint32_t active = 0, index = 0;
    for (lnd_http_session *s = lnd_http.sessions; s; s = s->next)
        if (s->worker == worker && !s->finished && !s->cancelled && !lnd_load(&s->detached)) active++;
    uint32_t start = active ? (uint32_t)(lnd_http.round[worker]++ % active) : 0;
    for (lnd_http_session **p = &lnd_http.sessions; *p;) {
        lnd_http_session *s = *p;
        if (lnd_load(&s->detached)) {
            *p = s->next;
            lnd_http_destroy(s);
            continue;
        }
        p = &s->next;
        if (s->worker != worker) continue;
        s->now = now;
        if (s->cancelled) {
            lnd_http_request_close(s);
            lnd_http_protocol_free(s);
            lnd_store(&s->terminal, LND_READ_EOF);
            lnd_http_state(s, LND_HTTP_CANCELLED);
            s->finished = true;
            continue;
        }
        if (lnd_exchange(&s->stalled, 0)) {
            s->stats.stalls++;
            lnd_http_state(s, LND_HTTP_BUFFERING);
        }
        if (!s->finished && active) {
            uint32_t distance = index >= start ? index - start : active - start + index;
            uint32_t units = budget / active + (distance < budget % active);
            index++;
            int32_t r = LND_HTTP_PENDING;
            if (!lnd_http_open_expired(s) && units) r = lnd_http_protocol_step(s, units);
            if (lnd_http_open_expired(s)) r = LND_HTTP_ERR_TIMEOUT;
            if (r < 0) lnd_http_fail(s, r, r == LND_HTTP_ERR_TIMEOUT ? "HTTP opening timed out" : "HTTP stream processing failed");
        }
        lnd_http_metadata_update(s);
        if (lnd_load(&s->terminal) == LND_READ_EOF && s->info.state == LND_HTTP_DRAINING) {
            lnd_spinlock_lock(&s->pcm_lock);
            bool empty = !s->count;
            lnd_spinlock_unlock(&s->pcm_lock);
            if (empty) lnd_http_state(s, LND_HTTP_ENDED);
        }
    }
    lnd_mutex_unlock(&lnd_http.mutex);
}

#if LND_THREADS
static void lnd_http_worker(void *user) {
    lnd_thread_com_init();
    while (!lnd_load(&lnd_http.stop)) {
        lnd_http_tick(true, lnd_http_clock(), 32);
        lnd_event_wait(&lnd_http.event, 5);
    }
    lnd_thread_com_free();
}
#endif

static bool lnd_http_header_allowed(const char *name) {
    char lower[32];
    size_t n = strlen(name);
    if (n >= sizeof lower) return true;
    for (size_t i = 0; i <= n; i++)
        lower[i] = name[i] >= 'A' && name[i] <= 'Z' ? name[i] + ('a' - 'A') : name[i];
    const char *reserved[] = {"host",       "content-length",  "transfer-encoding", "range",       "if-range",
                              "connection", "accept-encoding", "user-agent",        "icy-metadata"};
    for (size_t i = 0; i < sizeof reserved / sizeof *reserved; i++)
        if (!strcmp(lower, reserved[i])) return false;
    return true;
}

static bool lnd_http_text_valid(const char *text, size_t limit) { return !text || (strlen(text) <= limit && !strchr(text, '\r') && !strchr(text, '\n')); }

LND_HTTP_OPEN *LND_HttpOpen(const char *url, const LND_HTTP_OPTIONS *options) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    LND_HTTP_OPTIONS o;
    LND_HttpOptionsInit(&o);
    if (options) {
        if (options->size != sizeof *options) return lnd_error_null(LND_ERR_INVALID_ARG);
        o = *options;
    }
    uint64_t opened_at = o.open_timeout_ms ? lnd_http_clock() : 0;
    if (!lnd_http_url_valid(url) || !lnd_http_text_valid(o.user_agent, 4096) || !lnd_http_text_valid(o.codec_name, 64) ||
        !lnd_http_text_valid(o.file.directory, 32768) || o.file.max_bytes > INT64_MAX ||
        o.flags & ~(uint32_t)(LND_HTTP_NO_USER_AGENT | LND_HTTP_ALLOW_HTTP_REDIRECT | LND_HTTP_RESUME_LIVE | LND_HTTP_PROBE_DURATION | LND_HTTP_NO_PROBE_DURATION) ||
        (o.flags & (LND_HTTP_PROBE_DURATION | LND_HTTP_NO_PROBE_DURATION)) == (LND_HTTP_PROBE_DURATION | LND_HTTP_NO_PROBE_DURATION) ||
        o.header_count > 128 || (o.header_count && !o.headers) ||
        o.content_mode < LND_HTTP_AUTO || o.content_mode > LND_HTTP_LIVE || o.execution < LND_HTTP_EXEC_AUTO || o.execution > LND_HTTP_EXEC_WORKER ||
        !o.buffer.pcm_ms || o.buffer.pcm_ms > 600000 || o.buffer.start_ms > o.buffer.pcm_ms || o.buffer.resume_ms > o.buffer.pcm_ms ||
        o.buffer.compressed_bytes < 4096 || o.buffer.segment_bytes < 4096 || o.buffer.playlist_bytes < 1024 || !o.buffer.event_count ||
        o.buffer.event_count > 65536 || !o.buffer.playlist_entries || o.buffer.playlist_entries > 100000 || o.output_channels > LND_MAX_CHANNELS ||
        o.output_sample_rate_hz > 768000 || o.retry.connect_timeout_ms > INT32_MAX || o.retry.receive_timeout_ms > INT32_MAX)
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!LND_MODULE_HTTP_FILE && (o.file.max_bytes || o.file.directory)) return lnd_error_null(LND_ERR_UNSUPPORTED);
    if ((o.hls.low_latency && (!LND_HTTP_HLS || !LND_HTTP_LL_HLS)) || (!LND_HTTP_HLS && o.hls.preferred_language)) return lnd_error_null(LND_ERR_UNSUPPORTED);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_ctx.initialized || !lnd_http.initialized) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_STATE);
    }
    bool worker =
        o.execution == LND_HTTP_EXEC_WORKER || (o.execution == LND_HTTP_EXEC_AUTO && LND_THREADS && lnd_cfg_u32(LND_CFG_RUN_MODE) != LND_MODE_SINGLE_THREADED);
    if (worker && !LND_THREADS) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_UNSUPPORTED);
    }
    const LND_HTTP_TRANSPORT *transport = o.transport;
    if (!transport) transport = LND_HTTP_DEFAULT_TRANSPORT;
    if (!transport || transport->size != sizeof *transport || !transport->open || !transport->poll || !transport->close ||
        (transport->session_open != nullptr) != (transport->session_close != nullptr)) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
    lnd_http_session *s = lnd_alloc_zero(sizeof *s);
    LND_HTTP_OPEN *open = lnd_alloc_zero(sizeof *open);
    if (!s || !open) {
        lnd_free(s);
        lnd_free(open);
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    s->options = o;
    s->options.header_count = 0;
    s->options.user_agent = lnd_http_copy(o.user_agent ? o.user_agent : "Lindar");
    s->options.codec_name = lnd_http_copy(o.codec_name);
    s->options.proxy = lnd_http_copy(o.proxy);
    s->options.ca_file = lnd_http_copy(o.ca_file);
    s->options.file.directory = lnd_http_copy(o.file.directory);
    s->options.hls.preferred_language = lnd_http_copy(o.hls.preferred_language);
    s->url = lnd_http_copy(url);
    s->events = lnd_alloc_zero((size_t)o.buffer.event_count * sizeof *s->events);
    s->metadata = lnd_alloc_zero((size_t)o.buffer.event_count * sizeof *s->metadata);
    s->headers = o.header_count ? lnd_alloc_zero(o.header_count * 2 * sizeof *s->headers) : nullptr;
    int32_t result = LND_OK;
    if (!s->url || !s->events || !s->metadata || !s->options.user_agent || (o.header_count && !s->headers) || (o.codec_name && !s->options.codec_name) ||
        (o.file.directory && !s->options.file.directory) || (o.proxy && !s->options.proxy) || (o.ca_file && !s->options.ca_file) ||
        (o.hls.preferred_language && !s->options.hls.preferred_language))
        result = LND_ERR_OUT_OF_MEMORY;
    for (size_t i = 0; result == LND_OK && i < o.header_count; i++) {
        const LND_HTTP_HEADER *h = &o.headers[i];
        if (!h->name || !*h->name || !h->value || !lnd_http_header_allowed(h->name) || h->flags & ~(uint32_t)LND_HTTP_HEADER_SENSITIVE ||
            !lnd_http_text_valid(h->name, 256) ||
            strspn(h->name, "!#$%&'*+-.^_\x60|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != strlen(h->name) ||
            !lnd_http_text_valid(h->value, 8192) || (h->origin && !lnd_http_url_valid(h->origin))) {
            result = LND_ERR_INVALID_ARG;
            break;
        }
        s->headers[i] = (LND_HTTP_HEADER){
            .name = lnd_http_copy(h->name), .value = lnd_http_copy(h->value), .origin = lnd_http_copy(h->origin ? h->origin : url), .flags = h->flags};
        s->options.header_count++;
        if (!s->headers[i].name || !s->headers[i].value || !s->headers[i].origin) result = LND_ERR_OUT_OF_MEMORY;
    }
    s->options.headers = s->headers;
    s->transport = transport;
    s->transport_user = o.transport_user;
    if (result == LND_OK && transport->session_open) {
        lnd_callback_enter();
        result = transport->session_open(o.transport_user, &s->transport_user);
        lnd_callback_leave();
        s->transport_owned = result == LND_OK;
    }
    s->worker = worker;
    s->probe_duration = !(o.flags & LND_HTTP_NO_PROBE_DURATION) && ((o.flags & LND_HTTP_PROBE_DURATION) || lnd_cfg_bool(LND_CFG_HTTP_PROBE_DURATION));
    s->input.limit = o.buffer.segment_bytes;
    s->now = lnd_http_clock();
    s->info.state = LND_HTTP_CONNECTING;
    s->info.live = o.content_mode == LND_HTTP_LIVE;
    LND_DECODER_OPTIONS decode = {.codec_name = o.codec_name, .input_bytes = o.buffer.compressed_bytes, .allow_buffered = true};
    if (result == LND_OK) {
        s->decoder = LND_DecoderCreate(&decode);
        if (!s->decoder) result = LND_ErrorGetLast();
    }
    if (result != LND_OK || !s->decoder) {
        lnd_http_destroy(s);
        lnd_free(open);
        lnd_context_unlock();
        return lnd_error_null(result != LND_OK ? result : LND_ERR_OUT_OF_MEMORY);
    }
    lnd_mutex_lock(&lnd_http.mutex);
    if (o.open_timeout_ms) {
        opened_at = LND_MAX(opened_at, lnd_http.now);
        s->open_deadline = opened_at > UINT64_MAX - o.open_timeout_ms ? UINT64_MAX : opened_at + o.open_timeout_ms;
    }
#if LND_THREADS
    if (worker && !lnd_http.running) {
        lnd_store(&lnd_http.stop, 0);
        result = lnd_event_init(&lnd_http.event);
        if (result == LND_OK) result = lnd_thread_create(&lnd_http.thread, lnd_http_worker, nullptr);
        if (result == LND_OK)
            lnd_http.running = true;
        else
            lnd_event_free(&lnd_http.event);
    }
#endif
    if (result == LND_OK) {
        s->next = lnd_http.sessions;
        lnd_http.sessions = s;
        open->session = s;
        open->next = lnd_http.opens;
        lnd_http.opens = open;
    }
    lnd_mutex_unlock(&lnd_http.mutex);
    lnd_context_unlock();
    if (result != LND_OK) {
        lnd_http_destroy(s);
        lnd_free(open);
        return lnd_error_null(result);
    }
    return open;
}

static lnd_http_session *lnd_http_find(const LND_SOURCE *source) {
    for (lnd_http_session *s = lnd_http.sessions; s; s = s->next)
        if (s->source == source && !lnd_load(&s->detached)) return s;
    return nullptr;
}

static int32_t lnd_http_enter(void) {
    if (!lnd_context_enter()) return LND_ERR_BUSY;
    if (!lnd_http.initialized) {
        lnd_context_unlock();
        return LND_ERR_STATE;
    }
    lnd_mutex_lock(&lnd_http.mutex);
    return LND_OK;
}

static void lnd_http_leave(void) {
    lnd_mutex_unlock(&lnd_http.mutex);
    lnd_context_unlock();
}

static void lnd_http_info(lnd_http_session *s, LND_HTTP_INFO *info) {
    *info = s->info;
    uint64_t frames = lnd_load(&s->played);
    if (info->sample_rate_hz)
        info->position_us = (int64_t)(frames / info->sample_rate_hz * 1000000 + frames % info->sample_rate_hz * 1000000 / info->sample_rate_hz);
}

int32_t LND_HttpOpenGetInfo(const LND_HTTP_OPEN *open, LND_HTTP_INFO *info) {
    if (!open || !info || lnd_callback_active()) return LND_ERR_INVALID_ARG;
    int32_t lock_result = lnd_http_enter();
    if (lock_result != LND_OK) return lock_result;
    int32_t r = open->session ? LND_OK : LND_ERR_STATE;
    if (open->session) lnd_http_info(open->session, info);
    lnd_http_leave();
    return r;
}

#if LND_MODULE_METADATA
static int32_t lnd_http_source_metadata(const LND_SOURCE *source, LND_METADATA *metadata) {
    lnd_mutex_lock(&lnd_http.mutex);
    lnd_http_session *s = lnd_http_find(source);
    int32_t result = !s ? LND_ERR_INVALID_ARG : s->tags_status ? s->tags_status : !s->tags ? LND_METADATA_ERR_NOT_FOUND : LND_OK;
    if (!result && metadata) result = lnd_tag_assign(metadata, s->tags);
    lnd_mutex_unlock(&lnd_http.mutex);
    return result;
}
#endif

static int32_t lnd_http_take_source(LND_HTTP_OPEN *open, LND_SOURCE **source, bool buffered) {
    if (!open || !source) return LND_ERR_INVALID_ARG;
    *source = nullptr;
    if (!lnd_context_enter()) return LND_ERR_BUSY;
    if (!lnd_http.initialized) {
        lnd_context_unlock();
        return LND_ERR_STATE;
    }
    lnd_mutex_lock(&lnd_http.mutex);
    lnd_http_session *s = open->session;
    int32_t result = !s                                 ? LND_ERR_STATE
                     : s->info.state == LND_HTTP_FAILED ? s->info.error.code
                     : s->cancelled                     ? LND_ERR_STATE
                     : !s->ready                        ? LND_HTTP_PENDING
                                                        : LND_OK;
    if (result == LND_OK && buffered && s->buffering && !s->finished) result = LND_HTTP_PENDING;
    if (result == LND_OK) {
        LND_SOURCE_CONFIG config = {.read = lnd_http_read,
                                    .close = lnd_http_source_close,
                                    .user = s,
                                    .channels = s->info.channels,
                                    .sample_rate_hz = s->info.sample_rate_hz,
                                    .block_frames = 1024,
                                    .flags = LND_SOURCE_LIVE};
        s->source = LND_SourceCreate(&config);
        if (!s->source)
            result = LND_ErrorGetLast();
        else {
#if LND_MODULE_METADATA
            s->source->metadata_provider = lnd_http_source_metadata;
#endif
            *source = s->source;
            open->session = nullptr;
        }
    }
    lnd_mutex_unlock(&lnd_http.mutex);
    lnd_context_unlock();
    return result;
}

int32_t LND_HttpOpenTakeSource(LND_HTTP_OPEN *open, LND_SOURCE **source) { return lnd_http_take_source(open, source, false); }

LND_SOURCE *LND_SourceCreateHttp(const char *url, uint32_t timeout_ms, const LND_HTTP_OPTIONS *options) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
#if !LND_OS_MODE
    return lnd_error_null(LND_ERR_UNSUPPORTED);
#else
    LND_HTTP_OPTIONS o;
    LND_HttpOptionsInit(&o);
    if (options) {
        if (options->size != sizeof *options) return lnd_error_null(LND_ERR_INVALID_ARG);
        o = *options;
    }
    o.open_timeout_ms = timeout_ms;
    uint64_t started = lnd_http_clock();
    LND_HTTP_OPEN *open = LND_HttpOpen(url, &o);
    if (!open) return nullptr;
    bool worker = open->session->worker;
    LND_SOURCE *source = nullptr;
    int32_t result;
    for (;;) {
        result = worker ? LND_OK : LND_HttpUpdate(lnd_http_clock(), 32);
        if (result == LND_OK) result = lnd_http_take_source(open, &source, true);
        if (timeout_ms && lnd_http_clock() - started >= timeout_ms) result = LND_HTTP_ERR_TIMEOUT;
        if (result != LND_HTTP_PENDING) break;
#if LND_THREADS
        lnd_sleep_ms(1);
#elif LND_OS_WINDOWS
        Sleep(1);
#else
        struct timespec delay = {.tv_nsec = 1000000};
        nanosleep(&delay, nullptr);
#endif
    }
    if (result != LND_OK && source) {
        LND_SourceFree(source);
        source = nullptr;
    }
    LND_HttpOpenFree(open);
    if (!source) {
        LND_HttpUpdate(lnd_http_clock(), 1);
        return lnd_error_null(result);
    }
    return source;
#endif
}

int32_t LND_HttpOpenCancel(LND_HTTP_OPEN *open) {
    if (!open || lnd_callback_active()) return LND_ERR_INVALID_ARG;
    int32_t lock_result = lnd_http_enter();
    if (lock_result != LND_OK) return lock_result;
    int32_t result = open->session ? LND_OK : LND_ERR_STATE;
    if (open->session) open->session->cancelled = true;
    lnd_http_leave();
    return result;
}

int32_t LND_HttpOpenFree(LND_HTTP_OPEN *open) {
    if (!open || lnd_callback_active()) return LND_ERR_INVALID_ARG;
    int32_t lock_result = lnd_http_enter();
    if (lock_result != LND_OK) return lock_result;
    LND_HTTP_OPEN **p = &lnd_http.opens;
    while (*p && *p != open)
        p = &(*p)->next;
    if (!*p) {
        lnd_http_leave();
        return LND_ERR_INVALID_ARG;
    }
    *p = open->next;
    if (open->session) lnd_store(&open->session->detached, 1);
    lnd_free(open);
    lnd_http_leave();
    return LND_OK;
}

int32_t LND_SourceGetHttpInfo(const LND_SOURCE *source, LND_HTTP_INFO *info) {
    if (!source || !info || lnd_callback_active()) return LND_ERR_INVALID_ARG;
    int32_t lock_result = lnd_http_enter();
    if (lock_result != LND_OK) return lock_result;
    lnd_http_session *s = lnd_http_find(source);
    if (s) lnd_http_info(s, info);
    lnd_http_leave();
    return s ? LND_OK : LND_ERR_INVALID_ARG;
}

int32_t LND_SourceGetHttpStats(const LND_SOURCE *source, LND_HTTP_STATS *stats) {
    if (!source || !stats || lnd_callback_active()) return LND_ERR_INVALID_ARG;
    int32_t lock_result = lnd_http_enter();
    if (lock_result != LND_OK) return lock_result;
    lnd_http_session *s = lnd_http_find(source);
    if (s) {
        *stats = s->stats;
        stats->buffered_bytes = LND_DecoderGetBufferedBytes(s->decoder) + s->input.size - s->input_offset + lnd_http_protocol_buffered(s);
        lnd_spinlock_lock(&s->pcm_lock);
        stats->buffered_frames = s->count;
        uint64_t threshold = (uint64_t)(s->stats.stalls ? s->options.buffer.resume_ms : s->options.buffer.start_ms) * s->info.sample_rate_hz / 1000;
        stats->buffering_percent = !s->buffering || !threshold ? 100 : (uint32_t)LND_MIN(100u, (uint64_t)s->count * 100 / threshold);
        stats->content_size_known = !s->info.hls && s->response.length_known;
        stats->content_bytes = s->response.range ? s->response.total_length_bytes : s->response.content_length_bytes;
        stats->range_start_bytes = s->response.range ? s->response.range_start_bytes : 0;
        stats->download_complete = !s->info.live && (s->input_end || lnd_http_protocol_complete(s));
        lnd_spinlock_unlock(&s->pcm_lock);
    }
    lnd_http_leave();
    return s ? LND_OK : LND_ERR_INVALID_ARG;
}

int32_t LND_SourcePollHttpEvent(LND_SOURCE *source, LND_HTTP_EVENT *event) {
    if (!source || !event || lnd_callback_active()) return LND_ERR_INVALID_ARG;
    int32_t lock_result = lnd_http_enter();
    if (lock_result != LND_OK) return lock_result;
    lnd_http_session *s = lnd_http_find(source);
    if (s) lnd_http_metadata_update(s);
    int32_t result = !s ? LND_ERR_INVALID_ARG : !s->event_count ? LND_HTTP_PENDING : LND_OK;
    if (s && s->stats.events_lost != s->overflow_reported) {
        *event = (LND_HTTP_EVENT){.type = LND_HTTP_EVENT_OVERFLOW, .state = s->info.state};
        snprintf(event->text, sizeof event->text, "%llu events lost", (unsigned long long)(s->stats.events_lost - s->overflow_reported));
        s->overflow_reported = s->stats.events_lost;
        result = LND_OK;
    } else if (result == LND_OK) {
        *event = s->events[s->event_head];
        s->event_head = (s->event_head + 1) % s->options.buffer.event_count;
        s->event_count--;
    }
    lnd_http_leave();
    return result;
}

int32_t LND_SourceCancelHttp(LND_SOURCE *source) {
    if (!source || lnd_callback_active()) return LND_ERR_INVALID_ARG;
    int32_t lock_result = lnd_http_enter();
    if (lock_result != LND_OK) return lock_result;
    lnd_http_session *s = lnd_http_find(source);
    if (s) s->cancelled = true;
    lnd_http_leave();
    return s ? LND_OK : LND_ERR_INVALID_ARG;
}

static int32_t lnd_http_seek(LND_SOURCE *source, int64_t position, bool live, uint64_t *id) {
    if (!source || position < 0 || lnd_callback_active()) return LND_ERR_INVALID_ARG;
    if (!lnd_context_enter()) return LND_ERR_BUSY;
    if (!lnd_http.initialized) {
        lnd_context_unlock();
        return LND_ERR_STATE;
    }
    lnd_mutex_lock(&lnd_http.mutex);
    lnd_http_session *s = lnd_http_find(source);
    lnd_native_source *native = (lnd_native_source *)source;
    int32_t result = !s ? LND_ERR_INVALID_ARG : !lnd_spinlock_try(&native->lock) ? LND_ERR_BUSY : LND_OK;
    if (result == LND_OK) {
        result = lnd_http_protocol_seek(s, position, live);
        if (result == LND_OK) {
            lnd_http_metadata_clear(s);
            s->metadata_clock = false;
            s->request_id++;
            if (id) *id = s->request_id;
            lnd_spinlock_lock(&s->pcm_lock);
            s->head = s->count = 0;
            s->buffering = true;
            lnd_spinlock_unlock(&s->pcm_lock);
            lnd_store(&s->terminal, 0);
            lnd_store(&native->status, LND_SOURCE_WAITING);
            uint64_t us = (uint64_t)position;
            lnd_store(&native->position, us / 1000000 * s->info.sample_rate_hz + us % 1000000 * s->info.sample_rate_hz / 1000000);
            lnd_store(&native->end_requested, 0);
            if (native->sound) native->sound->ended = false;
#if LND_MODULE_GRAPH
            lnd_add(&native->revision, 1);
#endif
        }
        lnd_spinlock_unlock(&native->lock);
    }
    lnd_mutex_unlock(&lnd_http.mutex);
    lnd_context_unlock();
    return result;
}

int32_t LND_SourceSeekHttpMicroseconds(LND_SOURCE *source, int64_t position, uint64_t *id) { return lnd_http_seek(source, position, false, id); }
int32_t LND_SourceSeekHttpLive(LND_SOURCE *source, uint64_t *id) { return lnd_http_seek(source, 0, true, id); }

int32_t LND_SourceSetHttpVariant(LND_SOURCE *source, uint64_t bitrate_bps, const char *language) {
    if (!source || !lnd_http_text_valid(language, 64) || lnd_callback_active()) return LND_ERR_INVALID_ARG;
    int32_t lock_result = lnd_http_enter();
    if (lock_result != LND_OK) return lock_result;
    char *copy = lnd_http_copy(language);
    if (language && !copy) {
        lnd_http_leave();
        return LND_ERR_OUT_OF_MEMORY;
    }
    lnd_http_session *s = lnd_http_find(source);
    int32_t result = !s ? LND_ERR_INVALID_ARG : !s->info.hls ? LND_ERR_UNSUPPORTED : LND_OK;
    if (result == LND_OK) {
        lnd_free((void *)s->options.hls.preferred_language);
        s->options.hls.preferred_language = copy;
        s->options.hls.max_bitrate_bps = bitrate_bps;
        s->variant_changed = true;
        copy = nullptr;
    }
    lnd_http_leave();
    lnd_free(copy);
    return result;
}

int32_t LND_HttpUpdate(uint64_t now, uint32_t budget) {
    if (!budget || budget > 65536) return LND_ERR_INVALID_ARG;
    if (lnd_callback_active()) return LND_ERR_BUSY;
    if (!lnd_context_enter()) return LND_ERR_BUSY;
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        return LND_ERR_STATE;
    }
    lnd_http_tick(false, now, budget);
    lnd_context_unlock();
    return LND_OK;
}

void lnd_http_update(void) { lnd_http_tick(false, lnd_http_clock(), 16); }

int32_t lnd_http_init(void) {
    lnd_mutex_init(&lnd_http.mutex);
    lnd_http.initialized = true;
    return LND_OK;
}

void lnd_http_stop(void) {
    if (!lnd_http.initialized) return;
#if LND_THREADS
    if (lnd_http.running) {
        lnd_store(&lnd_http.stop, 1);
        lnd_event_signal(&lnd_http.event);
        lnd_thread_join(&lnd_http.thread);
        lnd_event_free(&lnd_http.event);
        lnd_http.running = false;
    }
#endif
}

void lnd_http_free(void) {
    if (!lnd_http.initialized) return;
    lnd_http_stop();
    while (lnd_http.opens) {
        LND_HTTP_OPEN *open = lnd_http.opens;
        lnd_http.opens = open->next;
        lnd_free(open);
    }
    while (lnd_http.sessions) {
        lnd_http_session *s = lnd_http.sessions;
        lnd_http.sessions = s->next;
        lnd_http_destroy(s);
    }
    lnd_mutex_free(&lnd_http.mutex);
    memset(&lnd_http, 0, sizeof lnd_http);
}
