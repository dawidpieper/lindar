#include "http.h"

#include <ctype.h>
#include <string.h>

char *lnd_http_copy(const char *text) {
    if (!text) return nullptr;
    size_t n = strlen(text);
    char *copy = lnd_alloc(n + 1);
    if (copy) memcpy(copy, text, n + 1);
    return copy;
}

bool lnd_http_bytes_append(lnd_http_bytes *b, const void *data, size_t n) {
    if (b->size > b->limit || n > b->limit - b->size) return false;
    size_t need = b->size + n;
    if (need > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : LND_MIN((size_t)4096, b->limit);
        while (capacity < need) capacity = capacity > b->limit / 2 ? b->limit : capacity * 2;
        void *grown = lnd_realloc(b->data, capacity);
        if (!grown) return false;
        b->data = grown;
        b->capacity = capacity;
    }
    if (n) memcpy(b->data + b->size, data, n);
    b->size += n;
    return true;
}

void lnd_http_bytes_free(lnd_http_bytes *b) {
    lnd_free(b->data);
    *b = (lnd_http_bytes){0};
}

static bool lnd_http_equal(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

static const char *lnd_http_authority(const char *url) {
    if (strlen(url) >= 7 && lnd_http_equal(url, "http://", 7)) return url + 7;
    if (strlen(url) >= 8 && lnd_http_equal(url, "https://", 8)) return url + 8;
    return nullptr;
}

static bool lnd_http_host(const char *url, const char **host, size_t *bytes, uint32_t *port) {
    const char *a = lnd_http_authority(url);
    if (!a) return false;
    const char *end = a + strcspn(a, "/?#");
    const char *colon = nullptr;
    *port = a - url == 8 ? 443 : 80;
    if (*a == '[') {
        const char *close = memchr(a, ']', (size_t)(end - a));
        if (!close || close == a + 1 || (close + 1 != end && close[1] != ':')) return false;
        *host = a + 1;
        *bytes = (size_t)(close - a - 1);
        if (close + 1 != end) colon = close + 1;
    } else {
        colon = memchr(a, ':', (size_t)(end - a));
        *host = a;
        *bytes = (size_t)((colon ? colon : end) - a);
    }
    if (!*bytes) return false;
    if (colon) {
        if (++colon == end) return false;
        uint32_t value = 0;
        for (; colon < end; colon++) {
            if (*colon < '0' || *colon > '9' || value > 6553) return false;
            value = value * 10 + (uint32_t)(*colon - '0');
            if (value > 65535) return false;
        }
        *port = value;
    }
    return true;
}

bool lnd_http_url_valid(const char *url) {
    if (!url || strlen(url) >= LND_HTTP_URL_MAX) return false;
    const char *authority = lnd_http_authority(url);
    if (!authority || !*authority || *authority == '/' || *authority == '?' || *authority == '#') return false;
    size_t n = strcspn(authority, "/?#");
    if (memchr(authority, '@', n)) return false;
    const char *host;
    size_t host_bytes;
    uint32_t port;
    if (!lnd_http_host(url, &host, &host_bytes, &port)) return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        if (*p <= 32 || *p == 127 || *p == '\\') return false;
    return true;
}

bool lnd_http_same_origin(const char *a, const char *b) {
    if (!a || !b) return false;
    const char *aa = lnd_http_authority(a), *bb = lnd_http_authority(b);
    if (!aa || !bb || aa - a != bb - b) return false;
    size_t na, nb;
    uint32_t pa, pb;
    if (!lnd_http_host(a, &aa, &na, &pa) || !lnd_http_host(b, &bb, &nb, &pb)) return false;
    return pa == pb && na == nb && lnd_http_equal(aa, bb, na);
}

int32_t lnd_http_url_resolve(const char *base, const char *relative, char *out, size_t capacity) {
    if (!base || !relative || !out || !capacity || !lnd_http_url_valid(base)) return LND_ERR_INVALID_ARG;
    char joined[LND_HTTP_URL_MAX];
    size_t prefix = 0;
    if (lnd_http_authority(relative)) {
        if (strlen(relative) >= sizeof joined) return LND_ERR_INVALID_ARG;
        strcpy(joined, relative);
    } else {
        const char *colon = strchr(relative, ':');
        if (colon && (size_t)(colon - relative) < strcspn(relative, "/?#")) return LND_ERR_UNSUPPORTED;
        const char *authority = lnd_http_authority(base);
        const char *path = authority + strcspn(authority, "/?#");
        if (relative[0] == '/' && relative[1] == '/')
            prefix = (size_t)(authority - base) - 2;
        else if (*relative == '/')
            prefix = (size_t)(path - base);
        else if (*relative == '?')
            prefix = strcspn(base, "?#");
        else if (!*relative || *relative == '#')
            prefix = strcspn(base, "#");
        else {
            const char *end = base + strcspn(base, "?#");
            const char *slash = end;
            while (slash > path && slash[-1] != '/') slash--;
            prefix = (size_t)(slash - base);
            if (path == end) prefix = (size_t)(path - base);
        }
        size_t n = strlen(relative);
        bool slash = *relative && *relative != '/' && *relative != '?' && *relative != '#' && prefix && base[prefix - 1] != '/';
        if (prefix + slash + n >= sizeof joined) return LND_ERR_INVALID_ARG;
        memcpy(joined, base, prefix);
        if (slash) joined[prefix++] = '/';
        memcpy(joined + prefix, relative, n + 1);
    }
    char *fragment = strchr(joined, '#');
    if (fragment) *fragment = 0;
    if (!lnd_http_url_valid(joined)) return LND_ERR_INVALID_ARG;
    const char *authority = lnd_http_authority(joined);
    size_t root = (size_t)(authority - joined) + strcspn(authority, "/?");
    size_t end = strcspn(joined, "?");
    size_t write = root;
    if (root < end && joined[root] == '/') {
        size_t read = root;
        while (read < end) {
            size_t start = ++read;
            while (read < end && joined[read] != '/') read++;
            size_t n = read - start;
            if (n == 1 && joined[start] == '.') {
                if (read == end && (write == root || joined[write - 1] != '/')) joined[write++] = '/';
                continue;
            }
            if (n == 2 && joined[start] == '.' && joined[start + 1] == '.') {
                while (write > root && joined[write - 1] != '/') write--;
                if (write > root) write--;
                if (read == end && (write == root || joined[write - 1] != '/')) joined[write++] = '/';
                continue;
            }
            joined[write++] = '/';
            memmove(joined + write, joined + start, n);
            write += n;
        }
        if (write == root) joined[write++] = '/';
    }
    memmove(joined + write, joined + end, strlen(joined + end) + 1);
    if (strlen(joined) >= capacity) return LND_ERR_INVALID_ARG;
    strcpy(out, joined);
    return LND_OK;
}
