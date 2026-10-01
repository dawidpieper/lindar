#include "lindar_devices.h"
#include "lindar_files.h"
#include "lindar.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    char *end = nullptr;
    double seconds = argc > 2 ? strtod(argv[2], &end) : 1;
    if (argc < 2 || argc > 4 || (argc > 2 && (end == argv[2] || *end)) || !isfinite(seconds) || seconds <= 0 || seconds > 3600 ||
        (argc == 4 && strcmp(argv[3], "--null"))) { fprintf(stderr, "Usage: record output.wav [seconds [--null]]\n"); return 2; }
    if (argc == 4) {
        const LND_DEVICE_BACKEND *backend = LND_DeviceBackendFind("null");
        if (!backend || LND_DeviceSetPreferredBackend(backend) != LND_OK) return 1;
        LND_CONFIG_KEY *realtime = LND_ConfigFindKey("null.realtime");
        if (!realtime || LND_ConfigSet(realtime, 1) != LND_OK) return 1;
    }
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_SOURCE *source = LND_SourceCreateDevice(LND_DEVICE_DEFAULT_INPUT, 0, 0, 0);
    int result = 1;
    if (source) {
        uint64_t frames = (uint64_t)(seconds * LND_SourceGetSampleRateHz(source));
        if (frames) {
            int64_t written = LND_SourceRenderFile(source, argv[1], LND_FORMAT_S16LE, frames);
            printf("%lld of %llu frames -> %s\n", (long long)written, (unsigned long long)frames, argv[1]);
            result = written >= 0 && (uint64_t)written == frames ? 0 : 1;
        }
    }
    if (result) fprintf(stderr, "%s\n", LND_ErrorGetString(LND_ErrorGetLast()));
    LND_SourceFree(source);
    LND_LibraryFree();
    return result;
}
