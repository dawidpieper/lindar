#include "lindar_files.h"
#include "lindar.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) { fprintf(stderr, "Usage: convert input-file output-file [encoder]\n"); return 2; }
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_SOURCE *source = LND_SourceCreateFile(argv[1], LND_ENCODED_SOURCE_LIGHTWEIGHT | LND_ENCODED_SOURCE_DIRECT, nullptr);
    LND_ENCODER_PARAMS params = {.encoder_name = argc == 4 ? argv[3] : nullptr, .format = LND_FORMAT_S16LE};
    params.channels = LND_SourceGetChannels(source);
    params.sample_rate_hz = LND_SourceGetSampleRateHz(source);
    LND_OUTPUT *output = source ? LND_OutputCreateFile(argv[2], &params) : nullptr;
    int result = 1;
    if (output) {
        int64_t frames = LND_OutputWriteSource(output, source, 0);
        int32_t finished = LND_OutputFinish(output);
        if (frames >= 0 && finished == LND_OK) {
            printf("%lld frames -> %s\n", (long long)frames, argv[2]);
            result = 0;
        }
    }
    if (result) fprintf(stderr, "%s\n", LND_ErrorGetString(LND_ErrorGetLast()));
    LND_OutputFree(output);
    LND_SourceFree(source);
    LND_LibraryFree();
    return result;
}
