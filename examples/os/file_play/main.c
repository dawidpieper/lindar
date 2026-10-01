#include "lindar_files.h"
#include "lindar_devices.h"
#include "lindar_graph.h"
#include "lindar.h"
#include "../common.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>

static volatile sig_atomic_t stopped;
static void stop(int signal_number) { (void)signal_number; stopped = 1; }

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "--null"))) { fprintf(stderr, "Usage: file_play input-file [--null]\n"); return 2; }
    if (argc == 3) {
        const LND_DEVICE_BACKEND *backend = LND_DeviceBackendFind("null");
        if (!backend || LND_DeviceSetPreferredBackend(backend) != LND_OK) return 1;
        LND_CONFIG_KEY *realtime = LND_ConfigFindKey("null.realtime");
        if (!realtime || LND_ConfigSet(realtime, 1) != LND_OK) return 1;
    }
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_SOURCE *source = LND_SourceCreateFile(argv[1], 0, nullptr);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_DEVICE_INSTANCE *instance = LND_DeviceInstanceOpen(LND_DEVICE_DEFAULT_OUTPUT);
    LND_NODE *output = LND_DeviceInstanceGetNode(instance);
    int result = 1;
    if (!source || !sound || !output || LND_SoundSetOutput(sound, output) != LND_OK || LND_SoundPlay(sound) != LND_OK) goto done;
    signal(SIGINT, stop);
    printf("Playing %s; Ctrl+C stops playback.\n", argv[1]);
    while (!stopped && LND_SoundGetState(sound) != LND_SOUND_STOPPED) {
        if (LND_LibraryUpdate() != LND_OK || LND_SourceGetStatus(source) < 0) goto done;
        sleep_ms(10);
    }
    result = LND_SourceGetStatus(source) < 0 ? 1 : 0;
done:
    if (result) fprintf(stderr, "%s\n", LND_ErrorGetString(LND_ErrorGetLast()));
    LND_SourceFree(source);
    LND_LibraryFree();
    return result;
}
