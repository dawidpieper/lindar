#include "session.h"

#include <stdio.h>
#include <string.h>
#if LND_HTTP_AES128
#include <openssl/evp.h>
#endif

enum { LND_HLS_IDLE, LND_HLS_PLAYLIST, LND_HLS_KEY, LND_HLS_MAP, LND_HLS_SEGMENT };

struct lnd_hls_session {
    lnd_hls_playlist playlist;
    lnd_hls_playlist master;
    LND_DEMUX *demux;
    char *playlist_url;
    char *map_url;
    char *key_url;
    char *selected_url;
    char *retry_url;
    char *pending_key;
    lnd_hls_range cached_map_range;
    uint8_t cached_map_iv[16];
    uint64_t retry_at;
    uint64_t select_at;
    bool switching;
    bool full_reload;
    uint32_t recoveries;
    bool preserve_buffer;
    bool drain;
    bool drain_to_seek;
    bool received_pending;
    lnd_http_session preload_request;
    lnd_http_bytes preload_data;
    lnd_hls_range preload_range;
    char *preload_url;
    bool preload_done;
    uint32_t preload_redirects;
    lnd_http_bytes download;
    lnd_http_bytes media;
    lnd_http_bytes init;
    size_t media_offset;
    uint32_t index;
    uint32_t redirects;
    uint32_t depth;
    uint32_t attempts;
    uint64_t next_reload;
    uint64_t last_sequence;
    uint64_t discontinuity;
    uint64_t requested_at;
    uint64_t key_sequence;
    uint64_t pending_key_sequence;
    int32_t last_part;
    int32_t kind;
    int64_t offset_us;
    int64_t played_until_us;
    int64_t seek_us;
    uint8_t key[16];
    bool have_last;
    bool have_key;
    bool complete;
    bool seeking;
    bool go_live;
    bool range;
    uint64_t range_start_bytes;
    uint64_t range_length_bytes;
};

static void lnd_hls_preload(lnd_http_session *s, lnd_hls_session *h) {
    if (!s->options.hls.low_latency || !LND_HTTP_LL_HLS || h->playlist.end || !h->playlist.preload) return;
    lnd_http_session *p = &h->preload_request;
    if (!h->preload_url || strcmp(h->preload_url, h->playlist.preload)) {
        lnd_http_request_close(p);
        lnd_free(h->preload_url);
        h->preload_url = lnd_http_copy(h->playlist.preload);
        h->preload_data.size = 0;
        h->preload_data.limit = s->options.buffer.segment_bytes;
        h->preload_done = false;
        h->preload_redirects = 0;
        h->preload_range = h->playlist.preload_range;
        p->transport = s->transport;
        p->transport_user = s->transport_user;
        p->options = s->options;
        p->headers = s->headers;
        p->worker = s->worker;
        p->now = s->now;
        if (!h->preload_url) return;
        if (lnd_http_request(p, h->preload_url, h->preload_range.present, h->preload_range.offset, h->preload_range.length) != LND_OK) return;
        s->stats.requests++;
    }
    if (!p->transfer || h->preload_done) return;
    uint8_t data[4096];
    size_t got = 0;
    int32_t r = lnd_http_poll(p, data, sizeof data, &got);
    if (p->response.headers_complete && p->response.status >= 300 && p->response.status < 400) {
        char url[LND_HTTP_URL_MAX];
        if (++h->preload_redirects <= s->options.max_redirects && lnd_http_url_resolve(p->request_url, p->response.location, url, sizeof url) == LND_OK &&
            (strncmp(p->request_url, "https:", 6) || strncmp(url, "http:", 5) || (s->options.flags & LND_HTTP_ALLOW_HTTP_REDIRECT))) {
            p->now = s->now;
            if (lnd_http_request(p, url, h->preload_range.present, h->preload_range.offset, h->preload_range.length) == LND_OK) {
                s->stats.requests++;
                return;
            }
        }
        r = LND_ERR_IO;
    }
    if (got > sizeof data || r < 0 || (p->response.headers_complete && p->response.status >= 400) || !lnd_http_bytes_append(&h->preload_data, data, got)) {
        lnd_http_request_close(p);
        h->preload_data.size = 0;
        return;
    }
    s->stats.received_bytes += got;
    if (got) p->last_data = s->now;
    if (r == LND_HTTP_DONE) {
        h->preload_done = p->response.headers_complete && p->response.status >= 200 && p->response.status < 300 &&
                          (!p->response.length_known || p->response.content_length_bytes == h->preload_data.size) &&
                          (!h->preload_range.present || (p->response.range && p->response.range_start_bytes == h->preload_range.offset));
        lnd_http_request_close(p);
    } else if (s->options.retry.receive_timeout_ms && s->now - p->last_data >= s->options.retry.receive_timeout_ms)
        lnd_http_request_close(p);
}

static const char *lnd_hls_select(lnd_http_session *s, lnd_hls_session *h) {
    lnd_hls_variant *selected = nullptr;
    uint64_t limit = s->options.hls.max_bitrate_bps;
    if (s->options.hls.adaptive && s->stats.throughput_bps) {
        uint64_t available = s->stats.throughput_bps / 4 * 3;
        if (!limit || available < limit) limit = available;
    }
    for (uint32_t i = 0; i < h->master.variant_count; i++) {
        lnd_hls_variant *v = &h->master.variants[i];
        if (!lnd_decoder_supports_stream(s->decoder, v->codecs)) continue;
        if (!selected || (v->bitrate_bps <= limit && (selected->bitrate_bps > limit || v->bitrate_bps > selected->bitrate_bps)) || (!limit && v->bitrate_bps > selected->bitrate_bps) ||
            (selected->bitrate_bps > limit && v->bitrate_bps < selected->bitrate_bps))
            selected = v;
    }
    if (!selected) return nullptr;
    lnd_hls_audio *audio = nullptr;
    int best = -1;
    for (uint32_t i = 0; i < h->master.audio_count; i++) {
        lnd_hls_audio *a = &h->master.audio[i];
        if (!a->url || !selected->audio || strcmp(a->group, selected->audio)) continue;
        int score = a->default_track ? 2 : a->autoselect ? 1 : 0;
        if (s->options.hls.preferred_language && a->language && !strcmp(a->language, s->options.hls.preferred_language)) score += 8;
        if (score > best) {
            audio = a;
            best = score;
        }
    }
    s->info.bitrate_bps = selected->bitrate_bps;
    return audio ? audio->url : selected->url;
}

static int32_t lnd_hls_fetch(lnd_http_session *s, lnd_hls_session *h, int32_t kind, const char *url, lnd_hls_range range) {
    h->download.size = 0;
    h->download.limit = kind == LND_HLS_PLAYLIST ? s->options.buffer.playlist_bytes : kind == LND_HLS_KEY ? 16 : s->options.buffer.segment_bytes;
    h->kind = kind;
    h->range = range.present;
    h->range_start_bytes = range.offset;
    h->range_length_bytes = range.length;
    h->redirects = 0;
    h->requested_at = s->now;
    return lnd_http_request(s, url, range.present, range.offset, range.length);
}

static int32_t lnd_hls_query(const char *url, bool msn, uint64_t sequence, int32_t part, bool skip, char out[LND_HTTP_URL_MAX]) {
    size_t root = strcspn(url, "?");
    if (root >= LND_HTTP_URL_MAX) return LND_ERR_FORMAT;
    memcpy(out, url, root);
    size_t size = root;
    bool query = false;
    const char *p = url + root;
    if (*p == '?') p++;
    while (*p) {
        size_t n = strcspn(p, "&");
        size_t key = strcspn(p, "=&");
        bool reserved = (key == 8 && !memcmp(p, "_HLS_msn", 8)) || (key == 9 && (!memcmp(p, "_HLS_part", 9) || !memcmp(p, "_HLS_skip", 9)));
        if (n && !reserved) {
            if (size + n + 1 >= LND_HTTP_URL_MAX) return LND_ERR_FORMAT;
            out[size++] = query ? '&' : '?';
            memcpy(out + size, p, n);
            size += n;
            query = true;
        }
        p += n;
        if (*p) p++;
    }
    out[size] = 0;
    if (msn) {
        int n = snprintf(out + size, LND_HTTP_URL_MAX - size, "%c_HLS_msn=%llu", query ? '&' : '?', (unsigned long long)sequence);
        if (n < 0 || (size_t)n >= LND_HTTP_URL_MAX - size) return LND_ERR_FORMAT;
        size += (size_t)n;
        query = true;
        if (part >= 0) {
            n = snprintf(out + size, LND_HTTP_URL_MAX - size, "&_HLS_part=%u", (uint32_t)part);
            if (n < 0 || (size_t)n >= LND_HTTP_URL_MAX - size) return LND_ERR_FORMAT;
            size += (size_t)n;
        }
    }
    if (skip) {
        int n = snprintf(out + size, LND_HTTP_URL_MAX - size, "%c_HLS_skip=YES", query ? '&' : '?');
        if (n < 0 || (size_t)n >= LND_HTTP_URL_MAX - size) return LND_ERR_FORMAT;
    }
    return LND_OK;
}

static int32_t lnd_hls_switch(lnd_http_session *s, lnd_hls_session *h) {
    char url[LND_HTTP_URL_MAX];
    bool report = false;
    uint64_t sequence = 0;
    int32_t part = -1;
    if (s->options.hls.low_latency && h->playlist.blocking) {
        for (uint32_t i = 0; i < h->playlist.report_count; i++) {
            const lnd_hls_report *r = &h->playlist.reports[i];
            if (strcmp(r->url, h->selected_url)) continue;
            report = true;
            sequence = r->sequence;
            part = r->part;
            break;
        }
    }
    int32_t r = lnd_hls_query(h->selected_url, report, sequence, part, false, url);
    return r == LND_OK ? lnd_hls_fetch(s, h, LND_HLS_PLAYLIST, url, (lnd_hls_range){0}) : r;
}

static int32_t lnd_hls_delta(lnd_hls_playlist *p, lnd_hls_playlist *old, uint32_t limit) {
    if (!p->skipped) return LND_OK;
    if (p->skipped > limit || p->count > limit - p->skipped) return LND_ERR_FORMAT;
    uint32_t skipped = (uint32_t)p->skipped;
    lnd_hls_segment *segments = lnd_alloc_zero(((size_t)skipped + p->count) * sizeof *segments);
    if (!segments) return LND_ERR_OUT_OF_MEMORY;
    int64_t duration = 0;
    uint32_t found = 0;
    for (uint32_t i = 0; i < old->count && found < skipped; i++) {
        lnd_hls_segment *s = &old->segments[i];
        if (s->part >= 0 || s->sequence != p->sequence + found) continue;
        if (s->duration_us > INT64_MAX - duration) {
            lnd_free(segments);
            return LND_ERR_FORMAT;
        }
        segments[found] = *s;
        segments[found].start_us = duration;
        duration += s->duration_us;
        found++;
    }
    if (found != skipped || duration > INT64_MAX - p->duration_us) {
        lnd_free(segments);
        return LND_ERR_FORMAT;
    }
    for (uint32_t i = 0; i < p->count; i++) {
        if (p->segments[i].start_us > INT64_MAX - duration) {
            lnd_free(segments);
            return LND_ERR_FORMAT;
        }
        lnd_hls_segment *s = &p->segments[i], *previous = i ? &p->segments[i - 1] : &segments[skipped - 1];
        if (!s->key_set && previous->key) {
            s->key_sequence = previous->key_sequence;
            s->key = lnd_http_copy(previous->key);
            s->explicit_iv = previous->explicit_iv;
            memcpy(s->iv, previous->iv, 16);
            if (!s->explicit_iv) {
                memset(s->iv, 0, 16);
                for (unsigned j = 0; j < 8; j++) s->iv[15 - j] = (uint8_t)(s->sequence >> (8 * j));
            }
            if (!s->key) {
                lnd_free(segments);
                return LND_ERR_OUT_OF_MEMORY;
            }
        }
        if (!s->map_set && previous->map) {
            s->map_key_sequence = previous->map_key_sequence;
            s->map = lnd_http_copy(previous->map);
            s->map_key = lnd_http_copy(previous->map_key);
            s->map_range = previous->map_range;
            memcpy(s->map_iv, previous->map_iv, 16);
            if (!s->map || (previous->map_key && !s->map_key)) {
                lnd_free(segments);
                return LND_ERR_OUT_OF_MEMORY;
            }
        }
        segments[skipped + i] = *s;
        segments[skipped + i].start_us += duration;
    }
    for (uint32_t i = 0; i < old->count; i++) {
        lnd_hls_segment *s = &old->segments[i];
        if (s->part < 0 && s->sequence >= p->sequence && s->sequence < p->sequence + skipped) s->url = s->key = s->map = s->map_key = nullptr;
    }
    lnd_free(p->segments);
    p->segments = segments;
    p->count += skipped;
    p->capacity = p->count;
    p->duration_us += duration;
    return LND_OK;
}

static bool lnd_hls_same_range(lnd_hls_range a, lnd_hls_range b) {
    return a.present == b.present && (!a.present || (a.offset == b.offset && a.length == b.length));
}

static int32_t lnd_hls_update_playlist(lnd_http_session *s, lnd_hls_session *h, const uint8_t *data, size_t bytes, const char *url) {
    lnd_hls_playlist p = {0};
    int32_t r = lnd_hls_parse(data, bytes, url, s->options.buffer.playlist_entries, &p);
    if (r != LND_OK) return r;
    if (p.variant_count) {
        if (++h->depth > 4) {
            lnd_hls_playlist_free(&p);
            return LND_ERR_FORMAT;
        }
        lnd_hls_playlist_free(&h->master);
        h->master = p;
        const char *selected = lnd_hls_select(s, h);
        if (!selected) return LND_ERR_UNSUPPORTED;
        lnd_free(h->selected_url);
        h->selected_url = lnd_http_copy(selected);
        if (!h->selected_url) return LND_ERR_OUT_OF_MEMORY;
        char *copy = lnd_http_copy(selected);
        if (!copy) return LND_ERR_OUT_OF_MEMORY;
        lnd_free(h->playlist_url);
        h->playlist_url = copy;
        return LND_HTTP_PENDING;
    }
    if (!p.count && p.end) {
        lnd_hls_playlist_free(&p);
        return LND_ERR_FORMAT;
    }
    r = lnd_hls_delta(&p, &h->playlist, s->options.buffer.playlist_entries);
    if (r != LND_OK) {
        bool delta = p.skipped != 0;
        lnd_hls_playlist_free(&p);
        if (delta && !h->full_reload) {
            h->full_reload = true;
            return LND_OK;
        }
        return r;
    }
    h->full_reload = false;
    int64_t offset = h->offset_us;
    bool aligned = !h->playlist.count;
    const lnd_hls_segment *program = nullptr;
    for (uint32_t i = 0; i < h->playlist.count; i++)
        if (h->playlist.segments[i].program_us != INT64_MIN) {
            program = &h->playlist.segments[i];
            break;
        }
    for (uint32_t i = 0; program && i < p.count && !aligned; i++) {
        const lnd_hls_segment *segment = &p.segments[i];
        if (segment->program_us == INT64_MIN) continue;
        int64_t base = h->offset_us + program->start_us;
        int64_t delta = segment->program_us - program->program_us;
        if ((delta > 0 && base > INT64_MAX - delta) || (delta < 0 && base < INT64_MIN - delta) || base + delta < INT64_MIN + segment->start_us) {
            lnd_hls_playlist_free(&p);
            return LND_ERR_FORMAT;
        }
        offset = base + delta - segment->start_us;
        aligned = true;
    }
    for (uint32_t i = 0, j = 0; !aligned && !h->switching && i < p.count && j < h->playlist.count;) {
        const lnd_hls_segment *a = &p.segments[i], *b = &h->playlist.segments[j];
        if (a->sequence < b->sequence) {
            i++;
            continue;
        }
        if (a->sequence > b->sequence) {
            j++;
            continue;
        }
        if (a->discontinuity == b->discontinuity) {
            offset = h->offset_us + b->start_us - a->start_us;
            aligned = true;
        } else {
            i++;
            j++;
        }
    }
    if (!aligned && h->switching && p.end) {
        offset = 0;
        aligned = true;
    }
    if (!aligned && h->have_last) {
        offset = h->played_until_us;
        if (s->options.flags & LND_HTTP_RESUME_LIVE) h->seeking = h->go_live = true;
        lnd_http_event(s, LND_HTTP_EVENT_DISCONTINUITY, LND_OK, "Playlist window advanced");
    }
    if (h->switching) {
        h->switching = false;
        h->preserve_buffer = true;
        h->seeking = true;
        h->seek_us = h->played_until_us;
        lnd_http_event(s, LND_HTTP_EVENT_VARIANT, LND_OK, h->selected_url);
    }
    if (offset > 0 && p.duration_us > INT64_MAX - offset) {
        lnd_hls_playlist_free(&p);
        return LND_ERR_FORMAT;
    }
    h->offset_us = offset;
    lnd_hls_playlist_free(&h->playlist);
    h->playlist = p;
    h->index = 0;
    s->info.hls = true;
    s->info.live = !p.end;
    s->info.seekable = p.count != 0;
    s->info.seek_start_us = LND_MAX(offset, 0);
    s->info.seek_end_us = offset + p.duration_us;
    if (p.count) {
        const lnd_hls_segment *last = &p.segments[p.count - 1];
        s->info.seek_end_us = LND_MAX(s->info.seek_end_us, offset + last->start_us + last->duration_us);
    }
    s->info.live_edge_us = s->info.seek_end_us;
    if (!p.end && s->options.buffer.dvr_ms)
        s->info.seek_start_us = LND_MAX(s->info.seek_start_us, s->info.seek_end_us - (int64_t)s->options.buffer.dvr_ms * 1000);
    if (p.end) {
        s->info.duration_us = s->info.seek_end_us;
        s->info.length_kind = LND_LENGTH_ESTIMATED;
    }
    if (!h->have_last && !h->seeking) {
        int64_t start = 0;
        if (p.has_start)
            start = p.start_offset_us < 0 ? s->info.seek_end_us + p.start_offset_us : offset + p.start_offset_us;
        else if (!p.end) {
            int64_t delay = s->options.hls.live_delay_ms                        ? (int64_t)s->options.hls.live_delay_ms * 1000
                            : s->options.hls.low_latency && p.part_hold_back_us ? p.part_hold_back_us
                            : p.hold_back_us                                    ? p.hold_back_us
                                                                                : p.target_us * 3;
            start = s->info.live_edge_us - delay;
        }
        if (start > offset) {
            h->seeking = true;
            h->seek_us = start;
        }
    }
    h->complete = false;
    h->next_reload = s->now + (uint64_t)LND_MAX(p.part_target_us && s->options.hls.low_latency ? p.part_target_us : p.target_us / 2, 100000) / 1000;
    return LND_OK;
}

int32_t lnd_hls_begin(lnd_http_session *s, const uint8_t *data, size_t bytes, const char *url, lnd_hls_session **out) {
    lnd_hls_session *h = lnd_alloc_zero(sizeof *h);
    if (!h) return LND_ERR_OUT_OF_MEMORY;
    h->playlist_url = lnd_http_copy(url);
    h->last_part = -1;
    h->media.limit = s->options.buffer.segment_bytes;
    h->init.limit = s->options.buffer.segment_bytes;
    if (!h->playlist_url) {
        lnd_hls_free(h);
        return LND_ERR_OUT_OF_MEMORY;
    }
    int32_t r = lnd_hls_update_playlist(s, h, data, bytes, url);
    if (r < 0) {
        lnd_hls_free(h);
        return r;
    }
    lnd_http_request_close(s);
    h->kind = LND_HLS_IDLE;
    s->response = (LND_HTTP_RESPONSE){0};
    s->info.hls = true;
    *out = h;
    return LND_OK;
}

static int32_t lnd_hls_decrypt(lnd_hls_session *h, const char *key, const uint8_t iv[16]) {
    if (!key) return LND_OK;
#if LND_HTTP_AES128
    if (!h->have_key || !h->download.size || h->download.size % 16 || h->download.size > INT_MAX) return LND_ERR_FORMAT;
    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    if (!context) return LND_ERR_OUT_OF_MEMORY;
    int first = 0, last = 0;
    bool ok = EVP_DecryptInit_ex(context, EVP_aes_128_cbc(), nullptr, h->key, iv) == 1 &&
              EVP_DecryptUpdate(context, h->download.data, &first, h->download.data, (int)h->download.size) == 1 &&
              EVP_DecryptFinal_ex(context, h->download.data + first, &last) == 1;
    EVP_CIPHER_CTX_free(context);
    if (!ok) return LND_ERR_FORMAT;
    h->download.size = (size_t)first + (size_t)last;
    return LND_OK;
#else
    return LND_ERR_UNSUPPORTED;
#endif
}

static int32_t lnd_hls_received(lnd_http_session *s, lnd_hls_session *h) {
    if (h->kind == LND_HLS_PLAYLIST) {
        char clean[LND_HTTP_URL_MAX];
        int32_t clean_result = lnd_hls_query(s->request_url, false, 0, -1, false, clean);
        if (clean_result != LND_OK) return clean_result;
        char *base = lnd_http_copy(clean);
        if (!base) return LND_ERR_OUT_OF_MEMORY;
        int32_t r = lnd_hls_update_playlist(s, h, h->download.data, h->download.size, base);
        if (r == LND_OK) {
            lnd_free(h->playlist_url);
            h->playlist_url = base;
        } else
            lnd_free(base);
        return r < 0 ? r : LND_OK;
    }
    if (h->index >= h->playlist.count) return LND_ERR_STATE;
    lnd_hls_segment *segment = &h->playlist.segments[h->index];
    if (h->kind == LND_HLS_KEY) {
        if (h->download.size != 16) return LND_ERR_FORMAT;
        memcpy(h->key, h->download.data, 16);
        h->have_key = true;
        h->key_sequence = h->pending_key_sequence;
        lnd_free(h->key_url);
        h->key_url = h->pending_key;
        h->pending_key = nullptr;
        return h->key_url ? LND_OK : LND_ERR_OUT_OF_MEMORY;
    }
    int32_t result = lnd_hls_decrypt(h, h->kind == LND_HLS_MAP ? segment->map_key : segment->key, h->kind == LND_HLS_MAP ? segment->map_iv : segment->iv);
    if (result != LND_OK) return result;
    if (h->kind == LND_HLS_MAP) {
        h->cached_map_range = segment->map_range;
        memcpy(h->cached_map_iv, segment->map_iv, 16);
        h->init.size = 0;
        if (!lnd_http_bytes_append(&h->init, h->download.data, h->download.size)) return LND_ERR_OUT_OF_MEMORY;
        lnd_free(h->map_url);
        h->map_url = lnd_http_copy(segment->map);
        return h->map_url ? LND_OK : LND_ERR_OUT_OF_MEMORY;
    }
    if (h->have_last && segment->discontinuity != h->discontinuity) {
        LND_DemuxReset(h->demux);
        lnd_decoder_reset(s->decoder);
        if (s->resampler) lnd_resample_source_reset(s->resampler);
        lnd_store(&s->decode_source.status, LND_SOURCE_WAITING);
        s->decode_source.pos = 0;
        s->decoder_end = false;
        s->metadata_clock = false;
        lnd_http_event(s, LND_HTTP_EVENT_DISCONTINUITY, LND_OK, "HLS discontinuity");
    }
    h->media.size = h->media_offset = 0;
    result = lnd_http_media(s, &h->demux, segment->range.present ? segment->range.offset : 0, h->offset_us + segment->start_us, h->download.data,
                            h->download.size, h->init.data, h->init.size, &h->media);
    if (result != LND_OK) return result;
    h->recoveries = 0;
    h->last_sequence = segment->sequence;
    h->last_part = segment->part;
    h->discontinuity = segment->discontinuity;
    h->have_last = true;
    h->played_until_us = h->offset_us + segment->start_us + segment->duration_us;
    uint64_t elapsed = LND_MAX(s->now - h->requested_at, 1);
    uint64_t bytes = h->download.size;
    uint64_t sample_rate_hz = bytes <= UINT64_MAX / 8000 ? bytes * 8000 / elapsed : UINT64_MAX;
    s->stats.throughput_bps = s->stats.throughput_bps ? s->stats.throughput_bps / 4 * 3 + sample_rate_hz / 4 : sample_rate_hz;
    h->index++;
    return LND_OK;
}

static bool lnd_hls_already_played(const lnd_hls_session *h, const lnd_hls_segment *segment) {
    if (!h->have_last) return false;
    if (segment->sequence < h->last_sequence) return true;
    if (segment->sequence > h->last_sequence) return false;
    if (h->last_part == -1) return true;
    if (segment->part >= 0) return segment->part <= h->last_part;
    return h->offset_us + segment->start_us + segment->duration_us <= h->played_until_us + 1000;
}

static int32_t lnd_hls_start_next(lnd_http_session *s, lnd_hls_session *h) {
    if (h->full_reload) return lnd_hls_fetch(s, h, LND_HLS_PLAYLIST, h->playlist_url, (lnd_hls_range){0});
    if (!h->playlist.count) return lnd_hls_fetch(s, h, LND_HLS_PLAYLIST, h->playlist_url, (lnd_hls_range){0});
    if (h->seeking) {
        int64_t target = h->go_live ? s->info.live_edge_us - LND_MAX((int64_t)s->options.hls.live_delay_ms * 1000, h->playlist.target_us * 3) : h->seek_us;
        target = LND_CLAMP(target, s->info.seek_start_us, s->info.seek_end_us);
        h->index = 0;
        while (h->index + 1 < h->playlist.count && h->offset_us + h->playlist.segments[h->index + 1].start_us <= target) h->index++;
        h->index = h->index > 2 ? h->index - 2 : 0;
        lnd_hls_segment *start = &h->playlist.segments[h->index];
        s->discard_us = target - h->offset_us - start->start_us;
        s->seek_target_us = target;
        s->seek_commit = !h->preserve_buffer;
        s->discard_round = h->preserve_buffer;
        h->have_last = false;
        h->seeking = h->go_live = false;
        h->media.size = h->media_offset = 0;
        LND_DemuxReset(h->demux);
        lnd_decoder_reset(s->decoder);
        if (s->resampler) lnd_resample_source_reset(s->resampler);
        lnd_store(&s->decode_source.status, LND_SOURCE_WAITING);
        s->decode_source.pos = 0;
        s->input_end = s->decoder_end = s->finished = false;
        lnd_store(&s->terminal, 0);
        if (!h->preserve_buffer) {
            lnd_spinlock_lock(&s->pcm_lock);
            s->head = s->count = 0;
            s->buffering = true;
            lnd_spinlock_unlock(&s->pcm_lock);
        }
        h->preserve_buffer = false;
    }
    if (h->master.variant_count && h->have_last && (s->variant_changed || s->now >= h->select_at)) {
        const char *selected = lnd_hls_select(s, h);
        h->select_at = s->now + 3000;
        s->variant_changed = false;
        if (selected && (!h->selected_url || strcmp(selected, h->selected_url))) {
            char *copy = lnd_http_copy(selected);
            if (!copy) return LND_ERR_OUT_OF_MEMORY;
            lnd_free(h->selected_url);
            h->selected_url = copy;
            h->switching = h->drain = true;
            LND_DecoderEnd(s->decoder);
            return LND_HTTP_PENDING;
        }
    }
    while (h->index < h->playlist.count) {
        lnd_hls_segment *segment = &h->playlist.segments[h->index];
        bool whole_available = false;
        if (segment->part >= 0) {
            for (uint32_t i = h->index + 1; i < h->playlist.count && h->playlist.segments[i].sequence == segment->sequence; i++)
                if (h->playlist.segments[i].part == -1) {
                    whole_available = true;
                    break;
                }
        }
        bool continuing_parts = h->have_last && h->last_sequence == segment->sequence && h->last_part >= 0;
        if (lnd_hls_already_played(h, segment) || (segment->part >= 0 && (!s->options.hls.low_latency || (whole_available && !continuing_parts)))) {
            h->index++;
            continue;
        }
        if (continuing_parts && segment->part < 0) {
            h->seek_us = h->played_until_us;
            h->preserve_buffer = h->drain = h->drain_to_seek = true;
            LND_DecoderEnd(s->decoder);
            return LND_HTTP_PENDING;
        }
        if (segment->gap) {
            lnd_http_event(s, LND_HTTP_EVENT_DISCONTINUITY, LND_OK, "HLS gap");
            h->index++;
            continue;
        }
        bool need_map = segment->map && (!h->map_url || strcmp(h->map_url, segment->map) || !lnd_hls_same_range(h->cached_map_range, segment->map_range) ||
                                         memcmp(h->cached_map_iv, segment->map_iv, 16));
        const char *key = need_map ? segment->map_key : segment->key;
        uint64_t key_sequence = need_map ? segment->map_key_sequence : segment->key_sequence;
        if (key && (!h->have_key || !h->key_url || strcmp(h->key_url, key) || h->key_sequence != key_sequence)) {
            h->pending_key_sequence = key_sequence;
            lnd_free(h->pending_key);
            h->pending_key = lnd_http_copy(key);
            if (!h->pending_key) return LND_ERR_OUT_OF_MEMORY;
            return lnd_hls_fetch(s, h, LND_HLS_KEY, key, (lnd_hls_range){0});
        }
        if (need_map) return lnd_hls_fetch(s, h, LND_HLS_MAP, segment->map, segment->map_range);
        if (h->preload_done && h->preload_url && !strcmp(h->preload_url, segment->url) && h->preload_range.present == segment->range.present &&
            (!segment->range.present ||
             (h->preload_range.offset == segment->range.offset && (!h->preload_range.length || h->preload_range.length == segment->range.length)))) {
            lnd_http_bytes swap = h->download;
            h->download = h->preload_data;
            h->preload_data = swap;
            h->preload_done = false;
            h->preload_data.size = 0;
            h->kind = LND_HLS_SEGMENT;
            h->requested_at = s->now;
            int32_t r = lnd_hls_received(s, h);
            h->kind = LND_HLS_IDLE;
            return r;
        }
        return lnd_hls_fetch(s, h, LND_HLS_SEGMENT, segment->url, segment->range);
    }
    if (h->playlist.end) {
        if (!h->complete) {
            h->media.size = h->media_offset = 0;
            int32_t r = lnd_http_media_end(s, h->demux, &h->media);
            if (r != LND_OK) return r;
            h->complete = true;
            if (h->media.size) return LND_HTTP_PENDING;
        }
        if (!s->input_end) {
            s->input_end = true;
            LND_DecoderEnd(s->decoder);
        }
        return LND_HTTP_DONE;
    }
    if (s->now < h->next_reload) return LND_HTTP_PENDING;
    char url[LND_HTTP_URL_MAX];
    bool blocking = s->options.hls.low_latency && h->playlist.blocking && h->have_last;
    uint64_t sequence = h->last_part < 0 ? h->last_sequence + 1 : h->last_sequence;
    int32_t part = h->last_part < 0 ? -1 : h->last_part + 1;
    int32_t result = lnd_hls_query(h->playlist_url, blocking, sequence, part, h->playlist.skip_until_us > 0, url);
    if (result != LND_OK) return result;
    return lnd_hls_fetch(s, h, LND_HLS_PLAYLIST, url, (lnd_hls_range){0});
}

static int32_t lnd_hls_retry(lnd_http_session *s, lnd_hls_session *h, int32_t error) {
    if ((s->response.status == 404 || s->response.status == 410) && !h->playlist.end && h->kind != LND_HLS_PLAYLIST) {
        if (h->recoveries++ >= s->options.retry.attempts) return LND_ERR_IO;
        h->full_reload = true;
        h->kind = LND_HLS_IDLE;
        h->download.size = 0;
        lnd_http_request_close(s);
        return LND_HTTP_PENDING;
    }
    if ((s->response.status == 400 || s->response.status == 412) && h->kind == LND_HLS_PLAYLIST && !h->full_reload) {
        h->full_reload = true;
        h->kind = LND_HLS_IDLE;
        h->download.size = 0;
        lnd_http_request_close(s);
        return LND_HTTP_PENDING;
    }
    bool retryable = error == LND_ERR_IO && (!s->response.status || (s->response.status >= 200 && s->response.status < 300) || s->response.status == 408 ||
                                             s->response.status == 429 || s->response.status >= 500);
    if (!retryable || h->attempts++ >= s->options.retry.attempts) return error;
    char *url = lnd_http_copy(s->request_url ? s->request_url : h->retry_url);
    if (!url) return LND_ERR_OUT_OF_MEMORY;
    lnd_free(h->retry_url);
    h->retry_url = url;
    uint64_t delay = s->options.retry.delay_ms;
    for (uint32_t i = 1; i < h->attempts; i++) delay = LND_MIN(delay * 2, s->options.retry.max_delay_ms);
    delay = LND_MAX(delay, s->response.retry_after_ms);
    h->retry_at = s->now > UINT64_MAX - delay ? UINT64_MAX : s->now + delay;
    s->stats.reconnects++;
    h->download.size = 0;
    lnd_http_request_close(s);
    lnd_http_state(s, LND_HTTP_RECONNECTING);
    return LND_HTTP_PENDING;
}

int32_t lnd_hls_step(lnd_http_session *s, lnd_hls_session *h, uint32_t budget) {
    for (uint32_t step = 0; step < budget; step++) {
        lnd_hls_preload(s, h);
        if (h->drain) {
            int32_t r = lnd_http_decode(s);
            if (r < 0) return r;
            if (!s->decoder_end) return LND_HTTP_PENDING;
            h->drain = false;
            if (h->received_pending) {
                h->received_pending = false;
                r = lnd_hls_received(s, h);
                h->kind = LND_HLS_IDLE;
            } else if (h->drain_to_seek) {
                h->drain_to_seek = false;
                h->seeking = true;
            } else
                r = lnd_hls_switch(s, h);
            if (r != LND_OK) return r;
        }
        if (h->retry_url) {
            if (s->now < h->retry_at) return LND_HTTP_PENDING;
            int32_t r = lnd_http_request(s, h->retry_url, h->range, h->range_start_bytes, h->range_length_bytes);
            lnd_free(h->retry_url);
            h->retry_url = nullptr;
            if (r != LND_OK) return r;
        }
        if (h->seeking) {
            lnd_http_request_close(s);
            h->kind = LND_HLS_IDLE;
            int32_t r = lnd_hls_start_next(s, h);
            if (r < 0) return r;
        }
        if (h->media_offset < h->media.size) {
            int64_t fed = LND_DecoderFeed(s->decoder, h->media.data + h->media_offset, h->media.size - h->media_offset);
            if (fed < 0) return (int32_t)fed;
            h->media_offset += (size_t)fed;
        }
        if (LND_DecoderGetBufferedBytes(s->decoder) || s->ready || s->input_end) {
            int32_t r = lnd_http_decode(s);
            if (r < 0) return r;
            if (s->finished) return LND_HTTP_DONE;
        }
        if (h->media_offset < h->media.size) return LND_HTTP_PENDING;
        if (s->ready) {
            lnd_spinlock_lock(&s->pcm_lock);
            bool full = s->count == s->capacity;
            lnd_spinlock_unlock(&s->pcm_lock);
            if (full) return LND_HTTP_PENDING;
        }
        if (h->kind == LND_HLS_IDLE) {
            int32_t r = lnd_hls_start_next(s, h);
            if (r < 0) return r;
            if (h->kind == LND_HLS_IDLE) return LND_HTTP_PENDING;
        }
        uint8_t buffer[16384];
        size_t got = 0;
        int32_t r = lnd_http_poll(s, buffer, sizeof buffer, &got);
        if (got > sizeof buffer) return LND_ERR_IO;
        if (s->response.headers_complete && s->response.status >= 300 && s->response.status < 400) {
            if (++h->redirects > s->options.max_redirects) return LND_ERR_IO;
            char url[LND_HTTP_URL_MAX];
            if (lnd_http_url_resolve(s->request_url, s->response.location, url, sizeof url) != LND_OK) return LND_ERR_FORMAT;
            if (!strncmp(s->request_url, "https:", 6) && !strncmp(url, "http:", 5) && !(s->options.flags & LND_HTTP_ALLOW_HTTP_REDIRECT))
                return LND_ERR_UNSUPPORTED;
            r = lnd_http_request(s, url, h->range, h->range_start_bytes, h->range_length_bytes);
            if (r != LND_OK) return r;
            continue;
        }
        if (r < 0 || (s->response.headers_complete && s->response.status >= 400)) return lnd_hls_retry(s, h, r < 0 ? r : LND_ERR_IO);
        if (got) {
            s->stats.received_bytes += got;
            s->last_data = s->now;
            if (!lnd_http_bytes_append(&h->download, buffer, got)) return LND_ERR_OUT_OF_MEMORY;
        }
        if (r == LND_HTTP_DONE) {
            if (!s->response.headers_complete || s->response.status < 200 || s->response.status >= 300 ||
                (s->response.length_known && h->download.size != s->response.content_length_bytes) ||
                (h->range && (!s->response.range || s->response.range_start_bytes != h->range_start_bytes || (h->range_length_bytes && h->download.size != h->range_length_bytes))))
                return LND_ERR_IO;
            if (h->kind == LND_HLS_SEGMENT && h->have_last && h->playlist.segments[h->index].discontinuity != h->discontinuity && s->ready) {
                h->received_pending = h->drain = true;
                LND_DecoderEnd(s->decoder);
                lnd_http_request_close(s);
                return LND_HTTP_PENDING;
            }
            r = lnd_hls_received(s, h);
            lnd_http_request_close(s);
            h->kind = LND_HLS_IDLE;
            h->attempts = 0;
            s->response = (LND_HTTP_RESPONSE){0};
            if (r != LND_OK) return r;
        } else if (!got) {
            uint64_t timeout = s->options.retry.receive_timeout_ms;
            if (h->kind == LND_HLS_PLAYLIST && h->playlist.blocking) timeout = LND_MAX(timeout, (uint64_t)h->playlist.target_us / 1000 * 4);
            if (timeout && s->now - s->last_data >= timeout) return lnd_hls_retry(s, h, LND_ERR_IO);
            return LND_HTTP_PENDING;
        }
    }
    return LND_HTTP_PENDING;
}

int32_t lnd_hls_seek(lnd_http_session *s, lnd_hls_session *h, int64_t time, bool live) {
    if (!h->playlist.count || (live && !s->info.live)) return LND_ERR_UNSUPPORTED;
    if (!live && (time < s->info.seek_start_us || time > s->info.seek_end_us)) return LND_ERR_INVALID_ARG;
    lnd_free(h->retry_url);
    h->retry_url = nullptr;
    h->drain = h->switching = h->preserve_buffer = h->drain_to_seek = h->received_pending = false;
    lnd_http_request_close(&h->preload_request);
    lnd_free(h->preload_url);
    h->preload_url = nullptr;
    h->preload_data.size = 0;
    h->preload_done = false;
    h->seek_us = time;
    h->seeking = true;
    h->go_live = live;
    s->finished = false;
    lnd_http_state(s, LND_HTTP_SEEKING);
    return LND_OK;
}

size_t lnd_hls_buffered(const lnd_hls_session *h) { return h ? h->download.size + h->media.size - h->media_offset + h->init.size + h->preload_data.size : 0; }

void lnd_hls_free(lnd_hls_session *h) {
    if (!h) return;
    lnd_http_request_close(&h->preload_request);
    lnd_http_bytes_free(&h->preload_data);
    lnd_free(h->preload_url);
    LND_DemuxFree(h->demux);
    lnd_hls_playlist_free(&h->playlist);
    lnd_hls_playlist_free(&h->master);
    lnd_free(h->playlist_url);
    lnd_free(h->map_url);
    lnd_free(h->key_url);
    lnd_free(h->selected_url);
    lnd_free(h->retry_url);
    lnd_free(h->pending_key);
    lnd_http_bytes_free(&h->download);
    lnd_http_bytes_free(&h->media);
    lnd_http_bytes_free(&h->init);
    memset(h->key, 0, sizeof h->key);
    lnd_free(h);
}
