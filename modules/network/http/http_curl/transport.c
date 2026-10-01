#include "lindar_http_curl.h"
#include "network/http/http.h"

#include <curl/curl.h>
#if defined(__ANDROID__)
#include <ares.h>
#endif
#if LND_HTTP_OPENSSL
#include "lnd_http_ca.h"
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct lnd_curl_session {
    CURLM *multi;
    CURLSH *share;
} lnd_curl_session;

typedef struct lnd_curl_transfer {
    CURL *easy;
    CURLM *multi;
    struct curl_slist *headers;
    struct curl_slist *aliases;
    LND_HTTP_RESPONSE response;
    uint8_t buffer[65536];
    size_t head;
    size_t size;
    size_t header_bytes;
    int32_t error;
    bool paused;
    bool done;
} lnd_curl_transfer;

static void lnd_curl_text(char *dst, size_t capacity, const char *src, size_t bytes) {
    while (bytes && isspace((unsigned char)*src)) {
        src++;
        bytes--;
    }
    while (bytes && isspace((unsigned char)src[bytes - 1])) bytes--;
    size_t take = LND_MIN(capacity - 1, bytes);
    memcpy(dst, src, take);
    dst[take] = 0;
}

static bool lnd_curl_name(const char *line, size_t bytes, const char *name) {
    size_t n = strlen(name);
    if (bytes <= n || line[n] != ':') return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)line[i]) != tolower((unsigned char)name[i])) return false;
    return true;
}

static bool lnd_curl_number(const char **text, uint64_t *out) {
    const char *p = *text;
    uint64_t value = 0;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') {
        unsigned digit = (unsigned)(*p++ - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *text = p;
    *out = value;
    return true;
}

static size_t lnd_curl_header(char *line, size_t width, size_t count, void *user) {
    lnd_curl_transfer *t = user;
    if (width && count > SIZE_MAX / width) return 0;
    size_t bytes = width * count;
    if (bytes > 65536 - LND_MIN(t->header_bytes, (size_t)65536)) return 0;
    t->header_bytes += bytes;
    if ((bytes >= 5 && !memcmp(line, "HTTP/", 5)) || (bytes >= 4 && !memcmp(line, "ICY ", 4))) {
        const char *space = memchr(line, ' ', bytes);
        if (!space || line + bytes - space < 4 || space[1] < '1' || space[1] > '5' || space[2] < '0' || space[2] > '9' || space[3] < '0' || space[3] > '9')
            return 0;
        t->response = (LND_HTTP_RESPONSE){.status = (space[1] - '0') * 100 + (space[2] - '0') * 10 + space[3] - '0'};
        return bytes;
    }
    const char *colon = memchr(line, ':', bytes);
    if (!colon) {
        if (bytes == 2 && line[0] == '\r' && line[1] == '\n') t->response.headers_complete = t->response.status >= 200;
        return bytes;
    }
    const char *value = colon + 1;
    size_t n = (size_t)(line + bytes - value);
    char number[128];
    if (lnd_curl_name(line, bytes, "Content-Type"))
        lnd_curl_text(t->response.content_type, sizeof t->response.content_type, value, n);
    else if (lnd_curl_name(line, bytes, "Location")) {
        if (n >= sizeof t->response.location) return 0;
        lnd_curl_text(t->response.location, sizeof t->response.location, value, n);
    } else if (lnd_curl_name(line, bytes, "ETag")) {
        if (n >= sizeof t->response.etag) return 0;
        lnd_curl_text(t->response.etag, sizeof t->response.etag, value, n);
    } else if (lnd_curl_name(line, bytes, "icy-name"))
        lnd_curl_text(t->response.station, sizeof t->response.station, value, n);
    else if (lnd_curl_name(line, bytes, "Content-Length") || lnd_curl_name(line, bytes, "icy-metaint") || lnd_curl_name(line, bytes, "Retry-After")) {
        if (n >= sizeof number) return 0;
        lnd_curl_text(number, sizeof number, value, n);
        const char *end = number;
        uint64_t length;
        if (!lnd_curl_number(&end, &length) || *end) {
            if (!lnd_curl_name(line, bytes, "Retry-After")) return 0;
            time_t date = curl_getdate(number, nullptr), now = time(nullptr);
            if (date >= 0 && now >= 0 && date > now) {
                uint64_t delay = (uint64_t)(date - now);
                t->response.retry_after_ms = delay > UINT32_MAX / 1000 ? UINT32_MAX : (uint32_t)delay * 1000;
            }
            return bytes;
        }
        if (lnd_curl_name(line, bytes, "Content-Length")) {
            if (t->response.length_known && length != t->response.content_length_bytes) return 0;
            t->response.length_known = true;
            t->response.content_length_bytes = length;
        } else if (lnd_curl_name(line, bytes, "icy-metaint")) {
            if (!length || length > UINT32_MAX) return 0;
            t->response.icy_interval_bytes = (uint32_t)length;
        } else
            t->response.retry_after_ms = length > UINT32_MAX / 1000 ? UINT32_MAX : (uint32_t)length * 1000;
    } else if (lnd_curl_name(line, bytes, "Accept-Ranges")) {
        lnd_curl_text(number, sizeof number, value, n);
        t->response.accepts_ranges = !strcmp(number, "bytes");
    } else if (lnd_curl_name(line, bytes, "Content-Range")) {
        if (n >= sizeof number) return 0;
        lnd_curl_text(number, sizeof number, value, n);
        uint64_t start, finish, total;
        const char *p = number;
        if (!strncmp(p, "bytes */", 8)) return bytes;
        if (strncmp(p, "bytes ", 6)) return 0;
        p += 6;
        if (!lnd_curl_number(&p, &start) || *p++ != '-' || !lnd_curl_number(&p, &finish) || *p++ != '/' || !lnd_curl_number(&p, &total) || *p ||
            start > finish || finish >= total)
            return 0;
        t->response.range = true;
        t->response.range_start_bytes = start;
        t->response.total_length_bytes = total;
    }
    return bytes;
}

static size_t lnd_curl_write(char *data, size_t width, size_t count, void *user) {
    lnd_curl_transfer *t = user;
    if (width && count > SIZE_MAX / width) return 0;
    size_t n = width * count;
    if (t->response.status < 200 || t->response.status >= 300) return n;
    if (n > sizeof t->buffer - t->size) {
        t->paused = true;
        return CURL_WRITEFUNC_PAUSE;
    }
    if (t->head + t->size + n > sizeof t->buffer) {
        memmove(t->buffer, t->buffer + t->head, t->size);
        t->head = 0;
    }
    memcpy(t->buffer + t->head + t->size, data, n);
    t->size += n;
    return n;
}

static void lnd_curl_close(void *transfer) {
    lnd_curl_transfer *t = transfer;
    if (!t) return;
    if (t->multi && t->easy) curl_multi_remove_handle(t->multi, t->easy);
    if (t->easy) curl_easy_cleanup(t->easy);
    curl_slist_free_all(t->headers);
    curl_slist_free_all(t->aliases);
    lnd_free(t);
}

static int32_t lnd_curl_open(void *user, const LND_HTTP_REQUEST *request, void **transfer) {
    if (request->manual) {
        const curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
        const char *host = strstr(request->url, "://") + 3;
        bool numeric = *host == '[';
        if (!numeric) {
            numeric = true;
            for (const char *p = host; *p && *p != '/' && *p != ':' && *p != '?'; p++)
                if (!isdigit((unsigned char)*p) && *p != '.') numeric = false;
        }
        if ((!version->ares && !numeric) || request->proxy) return LND_ERR_UNSUPPORTED;
    }
    lnd_curl_transfer *t = lnd_alloc_zero(sizeof *t);
    if (!t) return LND_ERR_OUT_OF_MEMORY;
    t->easy = curl_easy_init();
    lnd_curl_session *session = user;
    t->multi = session->multi;
    if (!t->easy || !t->multi) {
        lnd_curl_close(t);
        return LND_ERR_OUT_OF_MEMORY;
    }
#define LND_CURL_SET(key, value)                                                                                                                               \
    do {                                                                                                                                                       \
        if (curl_easy_setopt(t->easy, key, value) != CURLE_OK) {                                                                                               \
            lnd_curl_close(t);                                                                                                                                 \
            return LND_ERR_EXTERNAL;                                                                                                                            \
        }                                                                                                                                                      \
    } while (0)
    LND_CURL_SET(CURLOPT_URL, request->url);
    LND_CURL_SET(CURLOPT_PRIVATE, t);
    LND_CURL_SET(CURLOPT_SHARE, session->share);
    LND_CURL_SET(CURLOPT_PROTOCOLS_STR, "http,https");
    LND_CURL_SET(CURLOPT_FOLLOWLOCATION, 0L);
    LND_CURL_SET(CURLOPT_NOSIGNAL, 1L);
    LND_CURL_SET(CURLOPT_SSL_VERIFYPEER, 1L);
    LND_CURL_SET(CURLOPT_SSL_VERIFYHOST, 2L);
    LND_CURL_SET(CURLOPT_CONNECTTIMEOUT_MS, (long)request->connect_timeout_ms);
    LND_CURL_SET(CURLOPT_WRITEFUNCTION, lnd_curl_write);
    LND_CURL_SET(CURLOPT_WRITEDATA, t);
    LND_CURL_SET(CURLOPT_HEADERFUNCTION, lnd_curl_header);
    LND_CURL_SET(CURLOPT_HEADERDATA, t);
    LND_CURL_SET(CURLOPT_ACCEPT_ENCODING, "identity");
    LND_CURL_SET(CURLOPT_COOKIEFILE, "");
    if (!(request->flags & LND_HTTP_NO_USER_AGENT)) LND_CURL_SET(CURLOPT_USERAGENT, request->user_agent ? request->user_agent : "Lindar");
    if (request->proxy || request->manual) LND_CURL_SET(CURLOPT_PROXY, request->proxy ? request->proxy : "");
    if (request->ca_file) LND_CURL_SET(CURLOPT_CAINFO, request->ca_file);
#if LND_HTTP_OPENSSL
    else {
        struct curl_blob roots = {.data = (void *)lnd_http_ca, .len = sizeof lnd_http_ca - 1, .flags = CURL_BLOB_NOCOPY};
        LND_CURL_SET(CURLOPT_CAINFO_BLOB, &roots);
        LND_CURL_SET(CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
    }
#endif
    if (request->range) {
        char range[64];
        if (request->range_length_bytes) {
            if (request->range_start_bytes > UINT64_MAX - request->range_length_bytes) {
                lnd_curl_close(t);
                return LND_ERR_INVALID_ARG;
            }
            snprintf(range, sizeof range, "%llu-%llu", (unsigned long long)request->range_start_bytes,
                     (unsigned long long)(request->range_start_bytes + request->range_length_bytes - 1));
        } else
            snprintf(range, sizeof range, "%llu-", (unsigned long long)request->range_start_bytes);
        LND_CURL_SET(CURLOPT_RANGE, range);
    }
    if (request->if_range) {
        char line[272];
        snprintf(line, sizeof line, "If-Range: %s", request->if_range);
        t->headers = curl_slist_append(nullptr, line);
        if (!t->headers) {
            lnd_curl_close(t);
            return LND_ERR_OUT_OF_MEMORY;
        }
    }
    for (size_t i = 0; i < request->header_count; i++) {
        const LND_HTTP_HEADER *h = &request->headers[i];
        if (h->origin && !lnd_http_same_origin(h->origin, request->url)) continue;
        size_t n = strlen(h->name) + strlen(h->value) + 3;
        char *line = lnd_alloc(n);
        if (!line) {
            lnd_curl_close(t);
            return LND_ERR_OUT_OF_MEMORY;
        }
        snprintf(line, n, "%s: %s", h->name, h->value);
        struct curl_slist *grown = curl_slist_append(t->headers, line);
        lnd_free(line);
        if (!grown) {
            lnd_curl_close(t);
            return LND_ERR_OUT_OF_MEMORY;
        }
        t->headers = grown;
    }
#if LND_HTTP_ICY
    struct curl_slist *grown = curl_slist_append(t->headers, "Icy-MetaData: 1");
    if (!grown) {
        lnd_curl_close(t);
        return LND_ERR_OUT_OF_MEMORY;
    }
    t->headers = grown;
    t->aliases = curl_slist_append(nullptr, "ICY 200 OK");
    if (!t->aliases) {
        lnd_curl_close(t);
        return LND_ERR_OUT_OF_MEMORY;
    }
    LND_CURL_SET(CURLOPT_HTTP200ALIASES, t->aliases);
#endif
    LND_CURL_SET(CURLOPT_HTTPHEADER, t->headers);
#undef LND_CURL_SET
    if (curl_multi_add_handle(t->multi, t->easy) != CURLM_OK) {
        lnd_curl_close(t);
        return LND_ERR_EXTERNAL;
    }
    *transfer = t;
    return LND_OK;
}

static int32_t lnd_curl_poll(void *transfer, LND_HTTP_RESPONSE *response, void *data, size_t capacity, size_t *written) {
    lnd_curl_transfer *t = transfer;
    *written = 0;
    if (t->paused && !t->size) {
        t->paused = false;
        if (curl_easy_pause(t->easy, CURLPAUSE_CONT) != CURLE_OK) t->error = LND_ERR_IO;
    }
    int running = 0;
    if (!t->done && !t->error && curl_multi_perform(t->multi, &running) != CURLM_OK) t->error = LND_ERR_IO;
    int left = 0;
    CURLMsg *message;
    while ((message = curl_multi_info_read(t->multi, &left))) {
        if (message->msg == CURLMSG_DONE) {
            lnd_curl_transfer *finished = nullptr;
            curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &finished);
            if (finished) {
                finished->done = true;
                finished->response.backend_code = message->data.result;
                if (message->data.result != CURLE_OK) finished->error = LND_ERR_IO;
            }
        }
    }
    size_t n = LND_MIN(capacity, t->size);
    if (n) memcpy(data, t->buffer + t->head, n);
    t->head += n;
    t->size -= n;
    if (!t->size) t->head = 0;
    *written = n;
    *response = t->response;
    if (t->error && !t->size) return t->error;
    return t->done && !t->size ? LND_HTTP_DONE : LND_HTTP_PENDING;
}

static void lnd_curl_session_close(void *user) {
    lnd_curl_session *s = user;
    if (!s) return;
    if (s->multi) curl_multi_cleanup(s->multi);
    if (s->share) curl_share_cleanup(s->share);
    lnd_free(s);
}

static int32_t lnd_curl_session_open(void *user, void **out) {
#if defined(__ANDROID__)
    if (ares_library_android_initialized() != ARES_SUCCESS) return LND_ERR_STATE;
#endif
    lnd_curl_session *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->multi = curl_multi_init();
    s->share = curl_share_init();
    if (!s->multi || !s->share) {
        lnd_curl_session_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    if (curl_share_setopt(s->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) != CURLSHE_OK ||
        curl_multi_setopt(s->multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, 4L) != CURLM_OK || curl_multi_setopt(s->multi, CURLMOPT_MAXCONNECTS, 4L) != CURLM_OK) {
        lnd_curl_session_close(s);
        return LND_ERR_EXTERNAL;
    }
    *out = s;
    return LND_OK;
}

static bool lnd_curl_initialized;

int32_t lnd_http_curl_init(void) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return LND_ERR_EXTERNAL;
    lnd_curl_initialized = true;
    return LND_OK;
}

void lnd_http_curl_free(void) {
    if (!lnd_curl_initialized) return;
    curl_global_cleanup();
    lnd_curl_initialized = false;
}

const LND_HTTP_TRANSPORT lnd_http_curl = {.size = sizeof(LND_HTTP_TRANSPORT),
                                                 .capabilities = LND_HTTP_CAP_HTTP | LND_HTTP_CAP_HTTPS | LND_HTTP_CAP_MANUAL,
                                                 .open = lnd_curl_open,
                                                 .poll = lnd_curl_poll,
                                                 .close = lnd_curl_close,
                                                 .session_open = lnd_curl_session_open,
                                                 .session_close = lnd_curl_session_close};

const LND_HTTP_TRANSPORT *LND_HttpCurlGetTransport(void) { return &lnd_http_curl; }
