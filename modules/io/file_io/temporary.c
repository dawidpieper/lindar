#include "file_io.h"
#include "src/alloc.h"
#include "src/error.h"
#include <stdlib.h>
#include <string.h>

#if LND_OS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

LND_IO *LND_IoOpenFile(const char *path) {
    if (!path) return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_io *io = lnd_io_open_file_utf8(path);
    return io ? io : lnd_error_null(LND_ERR_IO);
}

LND_IO *LND_IoCreateFile(const char *path) {
    if (!path || !*path) return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_io *io = lnd_io_create_file_utf8(path);
    return io ? io : lnd_error_null(LND_ERR_IO);
}

LND_IO *LND_IoCreateTemporary(const char *directory) {
    FILE *file = nullptr;
#if LND_OS_WINDOWS
    wchar_t path[32768];
    DWORD length;
    if (directory) {
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, directory, -1, path, LND_COUNTOF(path));
        if (count <= 1) return lnd_error_null(LND_ERR_INVALID_ARG);
        length = (DWORD)count - 1;
    } else {
        length = GetTempPathW(LND_COUNTOF(path), path);
        if (!length || length >= LND_COUNTOF(path)) return lnd_error_null(LND_ERR_IO);
    }
    if (length > LND_COUNTOF(path) - 80) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (path[length - 1] != L'/' && path[length - 1] != L'\\') path[length++] = L'\\';
    for (unsigned attempt = 0; attempt < 128; attempt++) {
        swprintf(path + length, LND_COUNTOF(path) - length, L"lindar-%lu-%lu-%llu-%u.tmp", GetCurrentProcessId(), GetCurrentThreadId(),
                 (unsigned long long)GetTickCount64(), attempt);
        HANDLE handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS) continue;
            break;
        }
        int fd = _open_osfhandle((intptr_t)handle, _O_BINARY | _O_RDWR);
        if (fd < 0) {
            CloseHandle(handle);
            break;
        }
        file = _fdopen(fd, "w+b");
        if (!file) _close(fd);
        break;
    }
#else
    if (!directory) directory = getenv("TMPDIR");
    if (!directory || !*directory) directory = "/tmp";
    size_t length = strlen(directory);
    if (length > SIZE_MAX - 32) return lnd_error_null(LND_ERR_INVALID_ARG);
    char *path = lnd_alloc(length + 32);
    if (!path) return nullptr;
    snprintf(path, length + 32, "%s/lindar-XXXXXX", directory);
    int fd = mkstemp(path);
    if (fd >= 0) {
        if (unlink(path) == 0) file = fdopen(fd, "w+b");
        if (!file) close(fd);
    }
    lnd_free(path);
#endif
    if (!file) return lnd_error_null(LND_ERR_IO);
    return lnd_io_from_file(file, true);
}
