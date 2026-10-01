#include "file_io.h"
#include "src/alloc.h"
#include "src/error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if LND_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
typedef wchar_t lnd_file_path;
#define lnd_fseek64 _fseeki64
#define lnd_ftell64 _ftelli64
#else
#include <unistd.h>
typedef char lnd_file_path;
#define lnd_fseek64 fseeko
#define lnd_ftell64 ftello
#endif

typedef struct lnd_io_file {
    FILE *file;
    uint64_t cur;
    bool writing;
    lnd_file_path *target;
    lnd_file_path *temporary;
} lnd_io_file;

static int64_t lnd_io_file_read_at(void *state, uint64_t pos, void *dst, size_t size) {
    lnd_io_file *f = state;
    if (pos > INT64_MAX) return LND_ERR_IO;
    if (f->cur != pos || f->writing) {
        if (lnd_fseek64(f->file, (int64_t)pos, SEEK_SET) != 0) return LND_ERR_IO;
        f->cur = pos;
    }
    f->writing = false;
    size_t r = fread(dst, 1, size, f->file);
    f->cur += r;
    return ferror(f->file) ? LND_ERR_IO : (int64_t)r;
}

static size_t lnd_io_file_write_at(void *state, uint64_t pos, const void *src, size_t size) {
    lnd_io_file *f = state;
    if (pos > INT64_MAX) return 0;
    if (f->cur != pos || !f->writing) {
        if (lnd_fseek64(f->file, (int64_t)pos, SEEK_SET) != 0) return 0;
        f->cur = pos;
    }
    f->writing = true;
    size_t w = fwrite(src, 1, size, f->file);
    f->cur += w;
    return w;
}

static int32_t lnd_io_file_flush(void *state) {
    lnd_io_file *f = state;
    return f->file && fflush(f->file) == 0 ? LND_OK : LND_ERR_IO;
}

static int32_t lnd_io_file_commit(void *state) {
    lnd_io_file *f = state;
    if (!f->temporary) return LND_OK;
    int result = fclose(f->file);
    f->file = nullptr;
    if (result) return LND_ERR_IO;
#if LND_OS_WINDOWS
    if (!MoveFileExW(f->temporary, f->target, MOVEFILE_REPLACE_EXISTING)) return LND_ERR_IO;
#else
    if (rename(f->temporary, f->target)) return LND_ERR_IO;
#endif
    lnd_free(f->temporary);
    f->temporary = nullptr;
    return LND_OK;
}

static int32_t lnd_io_file_close(void *state, bool borrowed) {
    lnd_io_file *f = state;
    int32_t result = f->file && fclose(f->file) ? LND_ERR_IO : LND_OK;
    if (f->temporary) {
#if LND_OS_WINDOWS
        DeleteFileW(f->temporary);
#else
        unlink(f->temporary);
#endif
    }
    lnd_free(f->temporary);
    lnd_free(f->target);
    lnd_free(f);
    return result;
}

static const lnd_io_vt lnd_io_file_vt = {
    .read_at = lnd_io_file_read_at,
    .write_at = lnd_io_file_write_at,
    .close = lnd_io_file_close,
    .flush = lnd_io_file_flush,
    .commit = lnd_io_file_commit,
};

lnd_io *lnd_io_from_file(FILE *file, bool writable) {
    if (!file) return nullptr;
    lnd_io_file *f = lnd_alloc_zero(sizeof *f);
    lnd_io *io = lnd_alloc_zero(sizeof *io);
    if (!f || !io) {
        lnd_free(f);
        lnd_free(io);
        fclose(file);
        return nullptr;
    }
    f->file = file;
    int64_t size = 0;
    if (!writable) {
        lnd_fseek64(file, 0, SEEK_END);
        size = lnd_ftell64(file);
        lnd_fseek64(file, 0, SEEK_SET);
    }
    io->references = 1;
    io->vt = &lnd_io_file_vt;
    io->state = f;
    io->size = size > 0 ? (uint64_t)size : 0;
    io->writable = writable;
    io->seekable = true;
    return io;
}

#if LND_OS_WINDOWS
static wchar_t *lnd_io_wide_path(const char *path) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (count <= 1) return lnd_error_null(LND_ERR_INVALID_ARG);
    wchar_t *wide = lnd_alloc((size_t)count * sizeof *wide);
    if (!wide) return nullptr;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count)) {
        lnd_free(wide);
        return lnd_error_null(LND_ERR_INVALID_ARG);
    }
    return wide;
}
#else
static char *lnd_io_utf8_path(const wchar_t *path) {
    size_t length = wcslen(path);
    if (length > (SIZE_MAX - 1) / 4) return lnd_error_null(LND_ERR_INVALID_ARG);
    char *text = lnd_alloc(length * 4 + 1), *out = text;
    if (!text) return nullptr;
    for (size_t i = 0; i < length; ++i) {
        uint32_t c = (uint32_t)path[i];
        if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
            lnd_free(text);
            return lnd_error_null(LND_ERR_INVALID_ARG);
        }
        if (c < 0x80) *out++ = (char)c;
        else {
            if (c >= 0x10000) {
                *out++ = (char)(0xf0 | (c >> 18));
                *out++ = (char)(0x80 | ((c >> 12) & 63));
            } else if (c >= 0x800) *out++ = (char)(0xe0 | (c >> 12));
            else *out++ = (char)(0xc0 | (c >> 6));
            if (c >= 0x800) *out++ = (char)(0x80 | ((c >> 6) & 63));
            *out++ = (char)(0x80 | (c & 63));
        }
    }
    *out = 0;
    return text;
}
#endif

static FILE *lnd_io_fopen(const wchar_t *path, const wchar_t *mode) {
#if LND_OS_WINDOWS
    return _wfopen(path, mode);
#else
    char *utf8 = lnd_io_utf8_path(path);
    if (!utf8) return nullptr;
    char m[8] = {0};
    for (size_t i = 0; mode[i] && i < sizeof m - 1; i++) m[i] = (char)mode[i];
    FILE *file = fopen(utf8, m);
    lnd_free(utf8);
    return file;
#endif
}

static FILE *lnd_io_fopen_utf8(const char *path, const char *mode) {
#if LND_OS_WINDOWS
    wchar_t *wide = lnd_io_wide_path(path);
    if (!wide) return nullptr;
    wchar_t m[8] = {0};
    for (size_t i = 0; mode[i] && i < LND_COUNTOF(m) - 1; i++) m[i] = (wchar_t)mode[i];
    FILE *file = _wfopen(wide, m);
    lnd_free(wide);
    return file;
#else
    return fopen(path, mode);
#endif
}

lnd_io *lnd_io_open_file(const wchar_t *path) { return path ? lnd_io_from_file(lnd_io_fopen(path, L"rb"), false) : nullptr; }

lnd_io *lnd_io_open_file_utf8(const char *path) { return path ? lnd_io_from_file(lnd_io_fopen_utf8(path, "rb"), false) : nullptr; }

lnd_io *lnd_io_create_file(const wchar_t *path) { return path ? lnd_io_from_file(lnd_io_fopen(path, L"w+b"), true) : nullptr; }

lnd_io *lnd_io_create_file_utf8(const char *path) { return path ? lnd_io_from_file(lnd_io_fopen_utf8(path, "w+b"), true) : nullptr; }

static lnd_io *lnd_io_atomic_path(lnd_file_path *target, size_t length) {
    lnd_file_path *temporary = length <= SIZE_MAX / sizeof *temporary - 96 ? lnd_alloc((length + 96) * sizeof *temporary) : nullptr;
    if (!temporary) {
        lnd_free(target);
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    FILE *file = nullptr;
#if LND_OS_WINDOWS
    memcpy(temporary, target, length * sizeof *target);
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        swprintf(temporary + length, 96, L".lnd-%lu-%lu-%llu-%u.tmp", GetCurrentProcessId(), GetCurrentThreadId(),
                 (unsigned long long)GetTickCount64(), attempt);
        HANDLE handle = CreateFileW(temporary, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS) continue;
            break;
        }
        int fd = _open_osfhandle((intptr_t)handle, _O_BINARY | _O_RDWR);
        if (fd < 0) CloseHandle(handle);
        else {
            file = _fdopen(fd, "w+b");
            if (!file) _close(fd);
        }
        if (!file) DeleteFileW(temporary);
        break;
    }
#else
    memcpy(temporary, target, length);
    memcpy(temporary + length, ".lnd-XXXXXX", 12);
    int fd = mkstemp(temporary);
    if (fd >= 0) {
        file = fdopen(fd, "w+b");
        if (!file) {
            close(fd);
            unlink(temporary);
        }
    }
#endif
    if (!file) {
        lnd_free(temporary);
        lnd_free(target);
        return lnd_error_null(LND_ERR_IO);
    }
    lnd_io *io = lnd_io_from_file(file, true);
    if (!io) {
#if LND_OS_WINDOWS
        DeleteFileW(temporary);
#else
        unlink(temporary);
#endif
        lnd_free(temporary);
        lnd_free(target);
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    lnd_io_file *state = io->state;
    state->temporary = temporary;
    state->target = target;
    return io;
}

lnd_io *lnd_io_create_atomic_file_utf8(const char *path) {
#if LND_OS_WINDOWS
    wchar_t *target = lnd_io_wide_path(path);
    return target ? lnd_io_atomic_path(target, wcslen(target)) : nullptr;
#else
    char *target = lnd_strdup(path);
    return target ? lnd_io_atomic_path(target, strlen(target)) : nullptr;
#endif
}

lnd_io *lnd_io_create_atomic_file(const wchar_t *path) {
#if LND_OS_WINDOWS
    size_t length = wcslen(path);
    if (length > SIZE_MAX / sizeof *path - 1) return lnd_error_null(LND_ERR_INVALID_ARG);
    wchar_t *target = lnd_alloc((length + 1) * sizeof *target);
    if (!target) return nullptr;
    memcpy(target, path, (length + 1) * sizeof *target);
    return lnd_io_atomic_path(target, length);
#else
    char *target = lnd_io_utf8_path(path);
    return target ? lnd_io_atomic_path(target, strlen(target)) : nullptr;
#endif
}
