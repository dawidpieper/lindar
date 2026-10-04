#include "http.h"
#if LND_HTTP_HLS
#include "network/http/http_hls/session.h"
#endif

#if LND_HTTP_MP4
#include "network/http/http_mp4/mp4.h"
#endif

#if LND_MODULE_HTTP_FILE
#include "network/http/http_file/file.h"
#endif

#include <stdio.h>
#include <string.h>

enum { LND_CONTENT_UNKNOWN, LND_CONTENT_AUDIO, LND_CONTENT_PLAYLIST };
enum { LND_TRANSFER_RECEIVING, LND_TRANSFER_COMPLETE, LND_TRANSFER_RECONNECT };

typedef struct lnd_http_protocol {
    uint32_t redirects;
#if LND_MODULE_HTTP_FILE
    LND_IO *file;
    bool file_decoding;
#endif
#if LND_HTTP_HLS
    lnd_hls_session *hls;
#endif
#if LND_HTTP_MP4
    lnd_http_mp4 *mp4;
    bool mp4_tried;
#endif
    uint32_t depth;
    uint64_t received;
    uint64_t progress_frames;
    uint64_t range_start_bytes;
    uint64_t total_length_bytes;
    char *resume_url;
    bool resumable;
    bool range;
    bool headers;
    uint8_t content;
    uint8_t transfer_state;
} lnd_http_protocol;

static int32_t lnd_http_retry(lnd_http_session *s, lnd_http_protocol *p, int32_t error) {
    bool retryable = error == LND_ERR_IO && (!s->response.status || (s->response.status >= 200 && s->response.status < 300) || s->response.status == 408 ||
                                             s->response.status == 429 || s->response.status >= 500);
    bool resume = !s->info.live && p->resumable && (p->received || p->range_start_bytes);
    if (s->info.live && s->info.sample_rate_hz && s->stats.decoded_frames - p->progress_frames >= (uint64_t)s->info.sample_rate_hz * 5) s->attempts = 0;
    if (!retryable || s->attempts >= s->options.retry.attempts || (!s->info.live && p->received && !resume)) return error;
    uint64_t delay = s->options.retry.delay_ms;
    for (uint32_t i = 0; i < s->attempts && delay < s->options.retry.max_delay_ms; i++) delay = LND_MIN(delay * 2, s->options.retry.max_delay_ms);
    delay = LND_MAX(delay, s->response.retry_after_ms);
    s->retry_at = s->now + delay;
    s->attempts++;
    s->stats.reconnects++;
    s->info.error.retryable = true;
    if (resume) {
        if (p->received > UINT64_MAX - p->range_start_bytes) return LND_ERR_IO;
        p->range_start_bytes += p->received;
        p->range = true;
        p->headers = false;
        p->transfer_state = LND_TRANSFER_RECEIVING;
        p->received = 0;
        lnd_http_request_close(s);
        lnd_http_state(s, LND_HTTP_RECONNECTING);
        return LND_HTTP_PENDING;
    }
    lnd_http_request_close(s);
    if (p->received) {
        s->metadata_count = 0;
        lnd_decoder_reset(s->decoder);
        if (s->resampler) lnd_resample_source_reset(s->resampler);
        s->decode_source.pos = 0;
        lnd_store(&s->decode_source.status, LND_SOURCE_WAITING);
        lnd_http_event(s, LND_HTTP_EVENT_DISCONTINUITY, LND_OK, "Live connection restarted");
    }
    lnd_free(p->resume_url);
#if LND_MODULE_HTTP_FILE
    LND_IoFree(p->file);
#endif
    *p = (lnd_http_protocol){.progress_frames = s->stats.decoded_frames};
    *s->if_range = 0;
    s->input.size = s->input_offset = 0;
    s->input_end = s->decoder_end = false;
    s->icy_size = s->icy_received = 0;
    lnd_http_state(s, LND_HTTP_RECONNECTING);
    return LND_HTTP_PENDING;
}

static int32_t lnd_http_plain_playlist(lnd_http_session *s, lnd_http_protocol *p) {
    if (++p->depth > 8) return LND_ERR_FORMAT;
    if (!lnd_http_bytes_append(&s->input, "", 1)) return LND_ERR_OUT_OF_MEMORY;
    char *data = (char *)s->input.data;
    char *line = data;
    bool pls = !strncmp(data, "[playlist]", 10);
    char *target = nullptr;
    while (*line) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
        while (*line == ' ' || *line == '\t') line++;
        if (pls && !strncmp(line, "File", 4)) {
            char *equal = strchr(line, '=');
            if (equal) {
                target = equal + 1;
                break;
            }
        } else if (!pls && *line && *line != '#' && *line != '[') {
            target = line;
            break;
        }
        if (!end) break;
        line = end + 1;
    }
    if (!target) return LND_ERR_FORMAT;
    char url[LND_HTTP_URL_MAX];
    if (lnd_http_url_resolve(s->request_url, target, url, sizeof url) != LND_OK) return LND_ERR_FORMAT;
    s->input.size = s->input_offset = 0;
    lnd_free(p->resume_url);
    p->resume_url = nullptr;
    p->resumable = false;
    *s->if_range = 0;
    p->content = LND_CONTENT_UNKNOWN;
    p->transfer_state = LND_TRANSFER_RECEIVING;
    p->headers = p->range = false;
    p->range_start_bytes = p->total_length_bytes = 0;
    p->received = 0;
    return lnd_http_request(s, url, false, 0, 0);
}

enum { LND_PROTOCOL_AGAIN = 3 };

static int32_t lnd_http_drain(lnd_http_session *s, lnd_http_protocol *p) {

    if (s->input_offset < s->input.size && !s->decoder_end) {
        size_t used = 0;
        int32_t r;
#if LND_MODULE_HTTP_FILE
        if (p->file) {
            used = s->input.size - s->input_offset;
            r = lnd_http_file_write(s, p->file, s->input.data + s->input_offset, used);
        } else
#endif
            r = lnd_http_feed(s, s->input.data + s->input_offset, s->input.size - s->input_offset, &used);
        s->input_offset += used;
        if (r < 0) return r;
    }
    if (s->decoder_end || s->input_offset == s->input.size) s->input_offset = s->input.size = 0;
    if (p->transfer_state != LND_TRANSFER_RECEIVING && p->transfer_state != LND_TRANSFER_RECONNECT && !s->input.size && !s->input_end) {
        if (s->response.icy_interval_bytes && (s->icy_size || !s->icy_remaining)) return LND_ERR_FORMAT;
        s->input_end = true;
#if LND_MODULE_HTTP_FILE
        if (p->file) {
            int32_t r = LND_DecoderTakeIo(s->decoder, p->file);
            if (r != LND_OK) return r;
            p->file = nullptr;
            p->file_decoding = true;
        } else
#endif
            LND_DecoderEnd(s->decoder);
    }
    uint64_t consumed = lnd_decoder_consumed(s->decoder);
    uint64_t decoded = s->stats.decoded_frames;
    int32_t r = LND_HTTP_PENDING;
#if LND_MODULE_HTTP_FILE
    if (!p->file)
#endif
        r = lnd_http_decode(s);
#if LND_MODULE_HTTP_FILE
    if (r == LND_ERR_UNSUPPORTED) {
        r = lnd_http_file_begin(s, &p->file);
        if (r == LND_OK) return LND_PROTOCOL_AGAIN;
    }
#endif
    if (r < 0) return r;
    if (s->finished) {
        lnd_http_request_close(s);
        return LND_HTTP_DONE;
    }
    if (p->transfer_state == LND_TRANSFER_RECONNECT && !s->input.size && consumed == lnd_decoder_consumed(s->decoder) && decoded == s->stats.decoded_frames &&
        (LND_DecoderGetStatus(s->decoder) == LND_SOURCE_WAITING || LND_DecoderGetStatus(s->decoder) == LND_SOURCE_EOF))
        return lnd_http_retry(s, p, LND_ERR_IO);
    if (s->input.size) {
        if (LND_DecoderGetBufferedBytes(s->decoder) == s->options.buffer.compressed_bytes && LND_DecoderGetStatus(s->decoder) == LND_SOURCE_WAITING)
            return LND_ERR_UNSUPPORTED;
        return LND_HTTP_PENDING;
    }

    return LND_OK;
}

static int32_t lnd_http_headers(lnd_http_session *s, lnd_http_protocol *p) {

    if (s->response.status >= 300 && s->response.status < 400) {
        if (++p->redirects > s->options.max_redirects || !*s->response.location) return LND_ERR_IO;
        char url[LND_HTTP_URL_MAX];
        if (lnd_http_url_resolve(s->request_url, s->response.location, url, sizeof url) != LND_OK) return LND_ERR_FORMAT;
        if (!strncmp(s->request_url, "https:", 6) && !strncmp(url, "http:", 5) && !(s->options.flags & LND_HTTP_ALLOW_HTTP_REDIRECT))
            return LND_ERR_UNSUPPORTED;
        int32_t r = lnd_http_request(s, url, p->range, p->range_start_bytes, 0);
        if (r != LND_OK) return lnd_http_retry(s, p, r);
        p->headers = false;
        return LND_PROTOCOL_AGAIN;
    }
    if (s->response.status < 200 || s->response.status >= 300) return lnd_http_retry(s, p, LND_ERR_IO);
    if (!p->headers) {
        p->headers = true;
        if (p->range && (!s->response.range || s->response.range_start_bytes != p->range_start_bytes || s->response.total_length_bytes != p->total_length_bytes ||
                         strcmp(s->response.etag, s->if_range)))
            return LND_ERR_IO;
        if (!p->range && s->response.length_known && s->response.accepts_ranges && s->response.etag[0] == '"' && !s->response.icy_interval_bytes) {
            p->resumable = true;
            p->total_length_bytes = s->response.content_length_bytes;
            snprintf(s->if_range, sizeof s->if_range, "%s", s->response.etag);
            lnd_free(p->resume_url);
            p->resume_url = lnd_http_copy(s->request_url);
            if (!p->resume_url) return LND_ERR_OUT_OF_MEMORY;
        }
        s->icy_remaining = s->response.icy_interval_bytes;
        if ((s->response.icy_interval_bytes || *s->response.station) && s->options.content_mode == LND_HTTP_AUTO) s->info.live = true;
        snprintf(s->info.station, sizeof s->info.station, "%s", s->response.station);
        lnd_http_state(s, LND_HTTP_PROBING);
    }

    return LND_OK;
}

static int32_t lnd_http_classify(lnd_http_session *s, lnd_http_protocol *p) {
    if (p->content == LND_CONTENT_UNKNOWN && (s->input.size >= 12 || p->transfer_state != LND_TRANSFER_RECEIVING)) {
        const uint8_t *data_start = s->input.data;
        size_t n = s->input.size;
        if (n >= 3 && !memcmp(data_start, "\xef\xbb\xbf", 3)) {
            data_start += 3;
            n -= 3;
        }
        bool playlist = (n >= 7 && !memcmp(data_start, "#EXTM3U", 7)) || (n >= 10 && !memcmp(data_start, "[playlist]", 10)) ||
                        strstr(s->response.content_type, "mpegurl") || strstr(s->response.content_type, "scpls");
        p->content = playlist ? LND_CONTENT_PLAYLIST : LND_CONTENT_AUDIO;
#if LND_HTTP_MP4
        if (!p->mp4_tried && !s->info.live && p->resumable && n >= 12 && !memcmp(data_start + 4, "ftyp", 4)) {
            p->mp4_tried = true;
            int32_t r = lnd_http_mp4_begin(s, s->input.data, s->input.size, &p->mp4);
            s->input.size = s->input_offset = 0;
            return r;
        }
#endif
        if (p->content == LND_CONTENT_PLAYLIST) s->input.limit = s->options.buffer.playlist_bytes;
        else s->info.seekable = !s->info.live;
    }
    if (p->transfer_state != LND_TRANSFER_RECEIVING && p->content != LND_CONTENT_PLAYLIST && s->info.live) p->transfer_state = LND_TRANSFER_RECONNECT;
    if (p->transfer_state != LND_TRANSFER_RECEIVING && p->content == LND_CONTENT_PLAYLIST) {
        bool hls = false;
        for (size_t i = 0; i + 7 <= s->input.size; i++)
            if (!memcmp(s->input.data + i, "#EXT-X-", 7)) {
                hls = true;
                break;
            }
        if (hls) {
#if LND_HTTP_HLS
            int32_t r = lnd_hls_begin(s, s->input.data, s->input.size, s->request_url, &p->hls);
            s->input.size = s->input_offset = 0;
            *s->if_range = 0;
            return r;
#else
            return LND_ERR_UNSUPPORTED;
#endif
        }
        int32_t r = lnd_http_plain_playlist(s, p);
        return r < 0 ? r : LND_HTTP_PENDING;
    }

    return LND_OK;
}

int32_t lnd_http_protocol_step(lnd_http_session *s, uint32_t budget) {
    lnd_http_protocol *p = s->protocol;
    if (!p) {
        p = lnd_alloc_zero(sizeof *p);
        if (!p) return LND_ERR_OUT_OF_MEMORY;
        s->protocol = p;
    }
#if LND_HTTP_HLS
    if (p->hls) return lnd_hls_step(s, p->hls, budget);
#endif
#if LND_HTTP_MP4
    if (p->mp4) {
        int32_t r = lnd_http_mp4_step(s, p->mp4, budget);
        if (r != LND_ERR_UNSUPPORTED || s->ready) return r;
        lnd_http_mp4_free(p->mp4);
        p->mp4 = nullptr;
        p->content = LND_CONTENT_UNKNOWN;
        p->transfer_state = LND_TRANSFER_RECEIVING;
        p->headers = p->range = false;
        p->received = p->range_start_bytes = 0;
        lnd_http_request_close(s);
        lnd_decoder_reset(s->decoder);
        s->input.size = s->input_offset = 0;
        s->input_end = s->decoder_end = false;
    }
#endif
    if (!s->transfer && p->transfer_state == LND_TRANSFER_RECEIVING) {
        if (s->now < s->retry_at) return LND_HTTP_PENDING;
        int32_t r = lnd_http_request(s, p->resume_url ? p->resume_url : s->url, p->range, p->range_start_bytes, 0);
        if (r != LND_OK) return lnd_http_retry(s, p, r);
    }
    for (uint32_t step = 0; step < budget; step++) {
        if (p->content == LND_CONTENT_AUDIO) {
            int32_t r = lnd_http_drain(s, p);
            if (r == LND_PROTOCOL_AGAIN) continue;
            if (r != LND_OK) return r;
        }
        if (p->transfer_state != LND_TRANSFER_RECEIVING) return LND_HTTP_PENDING;
        uint8_t data[16384];
        size_t written = 0;
        int32_t status = lnd_http_poll(s, data, sizeof data, &written);
        if (written > sizeof data) return LND_ERR_IO;
        if (s->response.headers_complete) {
            int32_t r = lnd_http_headers(s, p);
            if (r == LND_PROTOCOL_AGAIN) continue;
            if (r != LND_OK) return r;
        }
        if (written) {
            s->last_data = s->now;
            p->received += written;
            s->stats.received_bytes += written;
            if (!lnd_http_bytes_append(&s->input, data, written)) return LND_ERR_OUT_OF_MEMORY;
        }
        if (status < 0) {
            if (status != LND_ERR_IO || !s->info.live || p->content != LND_CONTENT_AUDIO) return lnd_http_retry(s, p, status);
            p->transfer_state = LND_TRANSFER_RECONNECT;
        }
        if (status == LND_HTTP_DONE) {
            if (!p->headers || (s->response.length_known && p->received != s->response.content_length_bytes)) return lnd_http_retry(s, p, LND_ERR_IO);
            p->transfer_state = LND_TRANSFER_COMPLETE;
        }
        int32_t r = lnd_http_classify(s, p);
        if (r != LND_OK) return r;
        if (!written && status != LND_HTTP_DONE) {
            if (s->options.retry.receive_timeout_ms && s->now - s->last_data >= s->options.retry.receive_timeout_ms) return lnd_http_retry(s, p, LND_ERR_IO);
            return LND_HTTP_PENDING;
        }
    }
    return LND_HTTP_PENDING;
}

size_t lnd_http_protocol_buffered(const lnd_http_session *s) {
    const lnd_http_protocol *p = s->protocol;
    if (!p) return 0;
    size_t bytes = 0;
#if LND_HTTP_HLS
    bytes += lnd_hls_buffered(p->hls);
#endif
#if LND_HTTP_MP4
    bytes += lnd_http_mp4_buffered(p->mp4);
#endif
    return bytes;
}

void lnd_http_protocol_free(lnd_http_session *s) {
#if LND_HTTP_HLS
    lnd_http_protocol *p = s->protocol;
    if (p) lnd_hls_free(p->hls);
#endif
    lnd_http_protocol *protocol = s->protocol;
#if LND_HTTP_MP4
    if (protocol) lnd_http_mp4_free(protocol->mp4);
#endif
#if LND_MODULE_HTTP_FILE
    if (protocol) {
        LND_IoFree(protocol->file);
        if (protocol->file_decoding) lnd_decoder_reset(s->decoder);
    }
#endif
    if (protocol) lnd_free(protocol->resume_url);
    lnd_free(s->protocol);
    s->protocol = nullptr;
}

int32_t lnd_http_protocol_seek(lnd_http_session *s, int64_t time_us, bool live) {
#if LND_HTTP_HLS
    lnd_http_protocol *p = s->protocol;
    if (p && p->hls) return lnd_hls_seek(s, p->hls, time_us, live);
#endif
    lnd_http_protocol *state = s->protocol;
    if (!state || !s->info.seekable || live || s->info.live || s->cancelled) return LND_ERR_UNSUPPORTED;
#if LND_HTTP_MP4
    if (state->mp4) return lnd_http_mp4_seek(s, state->mp4, time_us);
    bool mp4_tried = state->mp4_tried;
#endif
    if (s->info.duration_us && time_us > s->info.duration_us) return LND_ERR_INVALID_ARG;
    lnd_http_request_close(s);
    lnd_free(state->resume_url);
#if LND_MODULE_HTTP_FILE
    LND_IoFree(state->file);
    s->stats.file_bytes = 0;
#endif
    *state = (lnd_http_protocol){0};
#if LND_HTTP_MP4
    state->mp4_tried = mp4_tried;
#endif
    *s->if_range = 0;
    lnd_decoder_reset(s->decoder);
    if (s->resampler) lnd_resample_source_reset(s->resampler);
    lnd_store(&s->decode_source.status, LND_SOURCE_WAITING);
    s->decode_source.pos = 0;
    s->input.size = s->input_offset = 0;
    s->input_end = s->decoder_end = s->finished = false;
    s->discard_us = time_us;
    s->discard_frames = 0;
    s->seek_target_us = time_us;
    s->seek_commit = true;
    s->attempts = 0;
    s->retry_at = 0;
    lnd_http_state(s, LND_HTTP_SEEKING);
    return LND_OK;
}
