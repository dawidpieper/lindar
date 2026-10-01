#include "files.h"

#include <string.h>

static void lnd_extension_finish(char *out, size_t cap, const char *src, size_t len) {
    size_t n = LND_MIN(len, cap - 1);
    for (size_t i = 0; i < n; i++) {
        char c = src[i];
        out[i] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
    }
    out[n] = 0;
}

void lnd_path_extension_utf8(const char *path, char *out, size_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (!path) return;
    const char *dot = nullptr;
    for (const char *p = path; *p; p++) {
        if (*p == '.') dot = p;
        else if (*p == '/' || *p == '\\') dot = nullptr;
    }
    if (dot && dot[1]) lnd_extension_finish(out, cap, dot + 1, strlen(dot + 1));
}

void lnd_path_extension(const wchar_t *path, char *out, size_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (!path) return;
    const wchar_t *dot = nullptr;
    for (const wchar_t *p = path; *p; p++) {
        if (*p == L'.') dot = p;
        else if (*p == L'/' || *p == L'\\') dot = nullptr;
    }
    if (!dot || !dot[1]) return;
    char tmp[16];
    size_t n = 0;
    for (const wchar_t *p = dot + 1; *p && n < sizeof tmp - 1; p++) {
        if (*p > 127) return;
        tmp[n++] = (char)*p;
    }
    lnd_extension_finish(out, cap, tmp, n);
}
