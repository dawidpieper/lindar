#include "lindar_files.h"
#include "src/callback.h"
#include "files.h"
#include "src/error.h"
#include "io/output/output.h"

LND_OUTPUT *LND_OutputCreateFileWide(const wchar_t *path, const LND_ENCODER_PARAMS *params) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!path || !*path || !lnd_output_params_valid(params)) return lnd_error_null(LND_ERR_INVALID_ARG);
    char ext[16];
    lnd_path_extension(path, ext, sizeof ext);
    if (!params->encoder_name && !ext[0]) return lnd_error_null(LND_ERR_UNSUPPORTED);
    int32_t result = lnd_output_validate(params, ext);
    if (result) return lnd_error_null(result);
    lnd_io *io = lnd_io_create_atomic_file(path);
    return io ? lnd_output_create_io(io, params, ext) : nullptr;
}

LND_OUTPUT *LND_OutputCreateFile(const char *path, const LND_ENCODER_PARAMS *params) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!path || !*path || !lnd_output_params_valid(params)) return lnd_error_null(LND_ERR_INVALID_ARG);
    char ext[16];
    lnd_path_extension_utf8(path, ext, sizeof ext);
    if (!params->encoder_name && !ext[0]) return lnd_error_null(LND_ERR_UNSUPPORTED);
    int32_t result = lnd_output_validate(params, ext);
    if (result) return lnd_error_null(result);
    lnd_io *io = lnd_io_create_atomic_file_utf8(path);
    return io ? lnd_output_create_io(io, params, ext) : nullptr;
}
