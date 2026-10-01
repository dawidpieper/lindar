#include "lindar_cast.h"
#include "src/alloc.h"
#include "src/atomic.h"
#include "src/callback.h"
#include "src/error.h"
#include "src/thread.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

struct LND_CAST {
    LND_CAST_OPTIONS options;
    lnd_thread thread;
    lnd_mutex mutex;
    lnd_event wake;
    lnd_atomic_u32 stop;
    lnd_atomic_u32 refs;
    LND_CAST_INFO info;
    uint8_t *queue;
    size_t head;
    bool finishing;
    bool output;
    char *title;
    char *host;
};

static bool text_valid(const char *s, size_t max) {
    if (!s) return true;
    for (size_t i = 0; s[i]; ++i)
        if (i >= max || (unsigned char)s[i] < 32 || s[i] == 127) return false;
    return true;
}

static bool url_valid(const char *url) {
    if (!url || !text_valid(url, 4096)) return false;
    CURLU *u = curl_url();
    if (!u) return false;
    char *scheme = nullptr, *user = nullptr;
    bool ok = !curl_url_set(u, CURLUPART_URL, url, 0) && !curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) &&
              (!strcmp(scheme, "http") || !strcmp(scheme, "https")) && curl_url_get(u, CURLUPART_USER, &user, 0) == CURLUE_NO_USER;
    curl_free(scheme);
    curl_free(user);
    curl_url_cleanup(u);
    return ok;
}

static int progress(void *user, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) { return lnd_load(&((LND_CAST *)user)->stop) != 0; }

static void configure(CURL *curl, LND_CAST *s, const char *url) {
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)s->options.timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)s->options.timeout_ms);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, s);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    if (s->options.ca_file) curl_easy_setopt(curl, CURLOPT_CAINFO, s->options.ca_file);
}

static int32_t send_bytes(LND_CAST *s, CURL *curl, const void *data, size_t size) {
    size_t sent = 0;
    uint64_t last = lnd_time_ns();
    while (sent < size && !lnd_load(&s->stop)) {
        size_t n = 0;
        CURLcode r = curl_easy_send(curl, (const uint8_t *)data + sent, size - sent, &n);
        if (r != CURLE_OK && r != CURLE_AGAIN) return LND_ERR_IO;
        sent += n;
        if (n)
            last = lnd_time_ns();
        else {
            if ((lnd_time_ns() - last) / 1000000 >= s->options.timeout_ms) return LND_ERR_IO;
            lnd_sleep_ms(5);
        }
    }
    return sent == size ? LND_OK : LND_ERR_STATE;
}

static int32_t response(LND_CAST *s, CURL *curl, bool shoutcast) {
    char data[4096];
    size_t size = 0;
    uint64_t start = lnd_time_ns();
    while (size + 1 < sizeof data && !lnd_load(&s->stop)) {
        size_t n = 0;
        CURLcode r = curl_easy_recv(curl, data + size, sizeof data - size - 1, &n);
        if (r != CURLE_OK && r != CURLE_AGAIN) return LND_ERR_IO;
        if (r == CURLE_OK && !n) return LND_ERR_IO;
        size += n;
        data[size] = 0;
        if (strstr(data, "\r\n\r\n") || strstr(data, "\n\n")) {
            if (shoutcast) return !strncmp(data, "OK2", 3) ? LND_OK : LND_ERR_IO;
            int status = 0;
            return sscanf(data, "HTTP/%*u.%*u %d", &status) == 1 && status >= 200 && status < 300 ? LND_OK : LND_ERR_IO;
        }
        if ((lnd_time_ns() - start) / 1000000 >= s->options.timeout_ms) return LND_ERR_IO;
        if (!n) lnd_sleep_ms(5);
    }
    return LND_ERR_IO;
}

static void base64(char *out, const uint8_t *data, size_t bytes) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0, at = 0; i < bytes; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16 | (i + 1 < bytes ? (uint32_t)data[i + 1] << 8 : 0) | (i + 2 < bytes ? data[i + 2] : 0);
        out[at++] = alphabet[v >> 18];
        out[at++] = alphabet[v >> 12 & 63];
        out[at++] = i + 1 < bytes ? alphabet[v >> 6 & 63] : '=';
        out[at++] = i + 2 < bytes ? alphabet[v & 63] : '=';
        out[at] = 0;
    }
}

static int32_t connect_source(LND_CAST *s, CURL *curl) {
    configure(curl, s, s->options.url);
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
    if (curl_easy_perform(curl) != CURLE_OK) return LND_ERR_IO;
    char header[8192];
    const LND_CAST_OPTIONS *o = &s->options;
    bool shoutcast = o->protocol == LND_CAST_SHOUTCAST;
    int n;
    if (shoutcast) {
        n = snprintf(header, sizeof header, "%s\r\n", o->password);
        int32_t r = send_bytes(s, curl, header, (size_t)n);
        memset(header, 0, (size_t)n);
        if (!r) r = response(s, curl, true);
        if (r) return r;
        n = snprintf(header, sizeof header,
                     "content-type:%s\r\nicy-name:%s\r\nicy-description:%s\r\nicy-genre:%s\r\nicy-url:%s\r\nicy-pub:%u\r\nicy-br:%u\r\n\r\n", o->content_type,
                     o->name, o->description, o->genre, o->website, o->public_stream, o->bitrate_kbps);
    } else {
        char credentials[1026], encoded[1372] = {0};
        int bytes = snprintf(credentials, sizeof credentials, "%s:%s", o->username, o->password);
        base64(encoded, (const uint8_t *)credentials, (size_t)bytes);
        n = snprintf(header, sizeof header,
                     "SOURCE %s HTTP/1.0\r\nHost: %s\r\nAuthorization: Basic %s\r\nContent-Type: %s\r\nIce-Name: %s\r\nIce-Description: %s\r\nIce-Genre: "
                     "%s\r\nIce-URL: %s\r\nIce-Public: %u\r\nIce-Bitrate: %u\r\n\r\n",
                     o->mount, s->host, encoded, o->content_type, o->name, o->description, o->genre, o->website, o->public_stream, o->bitrate_kbps);
        memset(credentials, 0, sizeof credentials);
        memset(encoded, 0, sizeof encoded);
    }
    if (n < 0 || (size_t)n >= sizeof header) return LND_ERR_INVALID_ARG;
    int32_t r = send_bytes(s, curl, header, (size_t)n);
    memset(header, 0, sizeof header);
    return r || shoutcast ? r : response(s, curl, false);
}

static size_t discard(char *data, size_t size, size_t count, void *user) { return size * count; }

static int32_t update_title(LND_CAST *s, const char *title) {
    CURL *curl = curl_easy_init();
    if (!curl) return LND_ERR_OUT_OF_MEMORY;
    char *song = curl_easy_escape(curl, title, 0);
    char *password = curl_easy_escape(curl, s->options.password, 0);
    char *mount = curl_easy_escape(curl, s->options.mount, 0);
    int32_t result = LND_ERR_OUT_OF_MEMORY;
    char url[16384];
    if (song && password && mount) {
        int n = s->options.protocol == LND_CAST_SHOUTCAST
                    ? snprintf(url, sizeof url, "%s/admin.cgi?mode=updinfo&pass=%s&song=%s", s->options.admin_url, password, song)
                    : snprintf(url, sizeof url, "%s/admin/metadata?mode=updinfo&mount=%s&song=%s", s->options.admin_url, mount, song);
        if (n > 0 && (size_t)n < sizeof url) {
            configure(curl, s, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            if (s->options.protocol == LND_CAST_ICECAST) {
                curl_easy_setopt(curl, CURLOPT_USERNAME, s->options.username);
                curl_easy_setopt(curl, CURLOPT_PASSWORD, s->options.password);
            }
            result = curl_easy_perform(curl) == CURLE_OK ? LND_OK : LND_ERR_IO;
        } else
            result = LND_ERR_INVALID_ARG;
    }
    curl_free(song);
    curl_free(password);
    curl_free(mount);
    curl_easy_cleanup(curl);
    return result;
}

static void worker(void *user) {
    LND_CAST *s = user;
    CURL *curl = curl_easy_init();
    int32_t result = curl ? connect_source(s, curl) : LND_ERR_OUT_OF_MEMORY;
    lnd_mutex_lock(&s->mutex);
    if (!result) s->info.state = s->finishing ? LND_CAST_DRAINING : LND_CAST_READY;
    lnd_mutex_unlock(&s->mutex);
    uint8_t buffer[16384];
    while (!result && !lnd_load(&s->stop)) {
        lnd_mutex_lock(&s->mutex);
        size_t n = LND_MIN(sizeof buffer, s->info.queued_bytes);
        size_t first = LND_MIN(n, s->options.queue_bytes - s->head);
        memcpy(buffer, s->queue + s->head, first);
        memcpy(buffer + first, s->queue, n - first);
        bool finished = s->finishing && !n;
        char *title = s->title;
        s->title = nullptr;
        lnd_mutex_unlock(&s->mutex);
        if (title) {
            int32_t r = update_title(s, title);
            lnd_free(title);
            lnd_mutex_lock(&s->mutex);
            s->info.metadata_error = r;
            lnd_mutex_unlock(&s->mutex);
        }
        if (finished) break;
        if (!n) {
            lnd_event_wait(&s->wake, 20);
            continue;
        }
        result = send_bytes(s, curl, buffer, n);
        if (!result) {
            lnd_mutex_lock(&s->mutex);
            s->head = (s->head + n) % s->options.queue_bytes;
            s->info.queued_bytes -= n;
            s->info.sent_bytes += n;
            lnd_mutex_unlock(&s->mutex);
        }
    }
    if (curl) curl_easy_cleanup(curl);
    lnd_mutex_lock(&s->mutex);
    s->info.error = result;
    s->info.state = result ? LND_CAST_FAILED : LND_CAST_ENDED;
    lnd_mutex_unlock(&s->mutex);
}

static void destroy(LND_CAST *s) {
    const char *strings[] = {s->options.url,  s->options.admin_url,   s->options.username, s->options.password, s->options.mount,  s->options.content_type,
                             s->options.name, s->options.description, s->options.genre,    s->options.website,  s->options.ca_file};
    for (size_t i = 0; i < sizeof strings / sizeof *strings; ++i)
        lnd_free((void *)strings[i]);
    curl_free(s->host);
    lnd_free(s->title);
    lnd_free(s->queue);
    lnd_event_free(&s->wake);
    lnd_mutex_free(&s->mutex);
    lnd_free(s);
    curl_global_cleanup();
}

LND_CAST *LND_CastCreate(const LND_CAST_OPTIONS *options) {
    if (!options || !options->password || !url_valid(options->url) || (options->admin_url && !url_valid(options->admin_url)) ||
        options->protocol < LND_CAST_SHOUTCAST || options->protocol > LND_CAST_ICECAST)
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    LND_CAST_OPTIONS o = *options;
    if (!o.username) o.username = "source";
    if (!o.mount) o.mount = "/stream";
    if (!o.content_type) o.content_type = "audio/mpeg";
    if (!o.timeout_ms) o.timeout_ms = 10000;
    if (!o.queue_bytes) o.queue_bytes = 262144;
    const char *strings[] = {o.url, o.admin_url, o.username, o.password, o.mount, o.content_type, o.name, o.description, o.genre, o.website, o.ca_file};
    for (unsigned i = 2; i < 11; ++i)
        if (!text_valid(strings[i], 512)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (o.mount[0] != '/' || strchr(o.mount, ' ') || o.timeout_ms > 300000 || o.queue_bytes < 4096 || o.queue_bytes > 64u * 1024 * 1024)
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (curl_global_init(CURL_GLOBAL_DEFAULT)) return lnd_error_null(LND_ERR_IO);
    LND_CAST *s = lnd_alloc_zero(sizeof *s);
    if (!s) {
        curl_global_cleanup();
        return nullptr;
    }
    s->options = o;
    const char **copies[] = {&s->options.url,   &s->options.admin_url,    &s->options.username, &s->options.password,
                             &s->options.mount, &s->options.content_type, &s->options.name,     &s->options.description,
                             &s->options.genre, &s->options.website,      &s->options.ca_file};
    bool ok = true;
    for (unsigned i = 0; i < 11; ++i) {
        *copies[i] = (i == 1 || i == 10) && !strings[i] ? nullptr : lnd_strdup(strings[i] ? strings[i] : "");
        if (!*copies[i] && i != 1 && i != 10) ok = false;
        if (strings[i] && !*copies[i]) ok = false;
    }
    lnd_mutex_init(&s->mutex);
    int32_t r = lnd_event_init(&s->wake);
    s->queue = lnd_alloc(o.queue_bytes);
    CURLU *url = curl_url();
    if (url) {
        if (curl_url_set(url, CURLUPART_URL, o.url, 0) || curl_url_get(url, CURLUPART_HOST, &s->host, 0)) ok = false;
        curl_url_cleanup(url);
    } else
        ok = false;
    s->info.capacity_bytes = o.queue_bytes;
    lnd_store(&s->refs, 1);
    if (!r && ok && s->queue)
        r = lnd_thread_create(&s->thread, worker, s);
    else if (!r)
        r = LND_ERR_OUT_OF_MEMORY;
    if (r) {
        destroy(s);
        return lnd_error_null(r);
    }
    return s;
}

int64_t LND_CastWrite(LND_CAST *s, const void *data, size_t bytes) {
    if (!s || (!data && bytes)) return LND_ERR_INVALID_ARG;
    lnd_mutex_lock(&s->mutex);
    if (s->finishing || s->info.state >= LND_CAST_ENDED) {
        int32_t r = s->info.error ? s->info.error : LND_ERR_STATE;
        lnd_mutex_unlock(&s->mutex);
        return r;
    }
    size_t n = LND_MIN(bytes, s->options.queue_bytes - s->info.queued_bytes);
    size_t tail = (s->head + s->info.queued_bytes) % s->options.queue_bytes;
    size_t first = LND_MIN(n, s->options.queue_bytes - tail);
    if (n) {
        memcpy(s->queue + tail, data, first);
        memcpy(s->queue, (const uint8_t *)data + first, n - first);
    }
    s->info.queued_bytes += n;
    lnd_mutex_unlock(&s->mutex);
    lnd_event_signal(&s->wake);
    return (int64_t)n;
}

int32_t LND_CastGetInfo(const LND_CAST *object, LND_CAST_INFO *info) {
    LND_CAST *s = (LND_CAST *)object;
    if (!s || !info) return LND_ERR_INVALID_ARG;
    lnd_mutex_lock(&s->mutex);
    *info = s->info;
    lnd_mutex_unlock(&s->mutex);
    return LND_OK;
}

int32_t LND_CastSetTitle(LND_CAST *s, const char *title) {
    if (!s || !title || !text_valid(title, 1024)) return LND_ERR_INVALID_ARG;
    if (!s->options.admin_url) return LND_ERR_UNSUPPORTED;
    char *copy = lnd_strdup(title);
    if (!copy) return LND_ERR_OUT_OF_MEMORY;
    lnd_mutex_lock(&s->mutex);
    lnd_free(s->title);
    s->title = copy;
    lnd_mutex_unlock(&s->mutex);
    lnd_event_signal(&s->wake);
    return LND_OK;
}

int32_t LND_CastEnd(LND_CAST *s) {
    if (!s) return LND_ERR_INVALID_ARG;
    lnd_mutex_lock(&s->mutex);
    s->finishing = true;
    if (s->info.state == LND_CAST_READY) s->info.state = LND_CAST_DRAINING;
    int32_t r = s->info.error;
    lnd_mutex_unlock(&s->mutex);
    lnd_event_signal(&s->wake);
    return r;
}

void LND_CastFree(LND_CAST *s) {
    if (!s || lnd_sub(&s->refs, 1) != 1) return;
    lnd_store(&s->stop, 1);
    lnd_event_signal(&s->wake);
    lnd_thread_join(&s->thread);
    destroy(s);
}

static size_t output_write(void *user, const void *data, size_t bytes) {
    LND_CAST *s = user;
    lnd_mutex_lock(&s->mutex);
    if (s->finishing || s->info.state >= LND_CAST_ENDED || bytes > s->options.queue_bytes - s->info.queued_bytes) {
        lnd_mutex_unlock(&s->mutex);
        return 0;
    }
    size_t tail = (s->head + s->info.queued_bytes) % s->options.queue_bytes;
    size_t first = LND_MIN(bytes, s->options.queue_bytes - tail);
    if (bytes) {
        memcpy(s->queue + tail, data, first);
        memcpy(s->queue, (const uint8_t *)data + first, bytes - first);
    }
    s->info.queued_bytes += bytes;
    lnd_mutex_unlock(&s->mutex);
    lnd_event_signal(&s->wake);
    return bytes;
}

static int32_t output_close(void *user) {
    LND_CAST *s = user;
    int32_t r = LND_CastEnd(s);
    LND_CastFree(s);
    return r;
}

LND_OUTPUT *LND_OutputCreateCast(LND_CAST *s, const LND_ENCODER_PARAMS *params) {
    if (!s || !params) return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_mutex_lock(&s->mutex);
    bool busy = s->output || s->finishing;
    if (!busy) {
        s->output = true;
        lnd_add(&s->refs, 1);
    }
    lnd_mutex_unlock(&s->mutex);
    if (busy) return lnd_error_null(LND_ERR_STATE);
    LND_ENCODER_PARAMS p = *params;
    p.flags |= LND_OUTPUT_STREAMING;
    const LND_IO_OUTPUT_PROCS procs = {.write = output_write, .close = output_close};
    LND_OUTPUT *output = LND_OutputCreateProc(&procs, s, &p);
    if (!output) {
        lnd_mutex_lock(&s->mutex);
        s->output = false;
        lnd_mutex_unlock(&s->mutex);
        LND_CastFree(s);
    }
    return output;
}
