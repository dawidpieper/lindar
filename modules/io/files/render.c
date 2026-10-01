#include "lindar_files.h"
#include "lindar.h"
#include "src/callback.h"
#include "src/error.h"
#include "src/format.h"
#include "io/output/output.h"

int64_t LND_SourceRenderFile(LND_SOURCE *source, const char *path, int32_t format, uint64_t max_frames) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!source || !path || !*path || (format && !lnd_format_valid(format)) || max_frames > INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    if (!max_frames) {
        LND_SOURCE_INFO info;
        int32_t result = LND_SourceGetInfo(source, &info);
        if (result != LND_OK) return result;
        if (info.length_kind == LND_LENGTH_UNKNOWN) return lnd_error(LND_ERR_UNSUPPORTED);
        uint64_t length = info.length_frames, position = info.position_frames;
        max_frames = info.length_kind == LND_LENGTH_ESTIMATED ? INT64_MAX : length > position ? length - position : 0;
        if (max_frames > INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    }
    LND_ENCODER_PARAMS params = {.channels = LND_SourceGetChannels(source), .sample_rate_hz = LND_SourceGetSampleRateHz(source), .format = format};
    LND_OUTPUT *output = LND_OutputCreateFile(path, &params);
    if (!output) return LND_ErrorGetLast();
    int64_t total = max_frames ? LND_OutputWriteSource(output, source, max_frames) : 0;
    if (total >= 0 && (uint64_t)total < max_frames && LND_SourceGetStatus(source) == LND_SOURCE_WAITING) {
        output->failed = LND_ERR_BUSY;
        total = LND_ERR_BUSY;
    }
    int32_t result = LND_OutputFree(output);
    return total < 0 ? lnd_error((int32_t)total) : result < 0 ? lnd_error(result) : total;
}
