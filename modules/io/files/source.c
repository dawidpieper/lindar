#include "files.h"
#include "lindar_files.h"
#include "src/callback.h"
#include "formats/codecs/source.h"
#include "src/error.h"

LND_SOURCE *LND_SourceCreateFileWide(const wchar_t *path, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!path || !*path) return lnd_error_null(LND_ERR_INVALID_ARG);
    char ext[16];
    lnd_path_extension(path, ext, sizeof ext);
    return lnd_source_open_io(lnd_io_open_file(path), ext, flags, options);
}

LND_SOURCE *LND_SourceCreateFile(const char *path, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!path || !*path) return lnd_error_null(LND_ERR_INVALID_ARG);
    char ext[16];
    lnd_path_extension_utf8(path, ext, sizeof ext);
    return lnd_source_open_io(lnd_io_open_file_utf8(path), ext, flags, options);
}

