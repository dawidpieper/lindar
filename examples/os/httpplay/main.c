#include "lindar_http.h"
#include "lindar.h"
#include "lindar_devices.h"
#include "lindar_graph.h"
#include "../common.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stopped;
static void stop(int signal_number) { (void)signal_number; stopped = 1; }

int main(int argc, char **argv) {
    bool silent = argc > 1 && !strcmp(argv[1], "--null");
    if (silent) { argc--; argv++; }
    if (argc < 2 || argc > 5) {
        fprintf(stderr, "httpplay [--null] URL [User-Agent] [language] [max-bitrate]\n");
        return 2;
    }
    if (silent) {
        const LND_DEVICE_BACKEND *backend = LND_DeviceBackendFind("null");
        if (!backend || LND_DeviceSetPreferredBackend(backend) != LND_OK) return 1;
        LND_CONFIG_KEY *realtime = LND_ConfigFindKey("null.realtime");
        if (!realtime || LND_ConfigSet(realtime, 1) != LND_OK) return 1;
    }
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    signal(SIGINT, stop);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.execution = LND_HTTP_EXEC_WORKER;
    if (argc > 2) options.user_agent = argv[2];
    if (argc > 3) options.hls.preferred_language = argv[3];
    if (argc > 4) options.hls.max_bitrate_bps = strtoull(argv[4], nullptr, 10);
    LND_HTTP_OPEN *open = LND_HttpOpen(argv[1], &options);
    LND_SOURCE *source = nullptr;
    int32_t result = open ? LND_HTTP_PENDING : LND_ErrorGetLast();
    while (open && !stopped && (result = LND_HttpOpenTakeSource(open, &source)) == LND_HTTP_PENDING) {
        LND_LibraryUpdate();
        sleep_ms(10);
    }
    LND_HttpOpenFree(open);
    if (source) {
        LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
        LND_DEVICE_INSTANCE *instance = LND_DeviceInstanceOpen(LND_DEVICE_DEFAULT_OUTPUT);
        LND_NODE *output = LND_DeviceInstanceGetNode(instance);
        result = sound && output ? LND_SoundSetOutput(sound, output) : LND_ERR_NO_DEVICE;
        if (result == LND_OK) result = LND_SoundPlay(sound);
        while (result == LND_OK && !stopped) {
            LND_LibraryUpdate();
            LND_HTTP_EVENT event;
            while (LND_SourcePollHttpEvent(source, &event) == LND_OK)
                if (*event.text) printf("%.3f: %s\n", (double)event.position_us / 1000000, event.text);
            LND_HTTP_INFO info;
            LND_SourceGetHttpInfo(source, &info);
            if (info.state == LND_HTTP_FAILED) {
                result = info.error.code;
                break;
            }
            if (LND_SourceGetStatus(source) == LND_SOURCE_EOF) break;
            sleep_ms(10);
        }
        LND_SourceFree(source);
    }
    if (result < 0) fprintf(stderr, "%s\n", LND_ErrorGetString(result));
    LND_LibraryFree();
    return result < 0 ? 1 : 0;
}
