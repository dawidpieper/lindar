#include "lindar_graph.h"
#include "lindar_devices.h"
#include "lindar.h"
#include "../common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--null"))) { fprintf(stderr, "Usage: device_tone [--null]\n"); return 2; }
    if (argc == 2) {
        const LND_DEVICE_BACKEND *backend = LND_DeviceBackendFind("null");
        if (!backend || LND_DeviceSetPreferredBackend(backend) != LND_OK) return 1;
        LND_CONFIG_KEY *realtime = LND_ConfigFindKey("null.realtime");
        if (!realtime || LND_ConfigSet(realtime, 1) != LND_OK) return 1;
    }
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    if (LND_LibraryInit() != LND_OK) return 1;
    int16_t samples[8000];
    for (size_t i = 0; i < 8000; i++) samples[i] = (int16_t)(8192 * sin(6.283185307179586 * 440 * i / 8000));
    LND_PCM pcm = {.data = samples, .frames = 8000, .channels = 1, .format = LND_FORMAT_S16LE};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .channels = 1, .sample_rate_hz = 8000});
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_DEVICE_INSTANCE *instance = LND_DeviceInstanceOpen(LND_DEVICE_DEFAULT_OUTPUT);
    LND_NODE *output = LND_DeviceInstanceGetNode(instance);
    int result = 1;
    if (!sound || !output || LND_SoundSetOutput(sound, output) != LND_OK || LND_SoundPlay(sound) != LND_OK) goto done;
    for (unsigned i = 0; i < 500 && LND_SoundGetState(sound) != LND_SOUND_STOPPED; i++) {
        if (LND_LibraryUpdate() != LND_OK) goto done;
        sleep_ms(10);
    }
    result = LND_SoundGetState(sound) == LND_SOUND_STOPPED ? 0 : 1;
    printf("440 Hz: %s\n", result ? "playback did not finish" : "finished");
done:
    if (result) fprintf(stderr, "%s\n", LND_ErrorGetString(LND_ErrorGetLast()));
    LND_SourceFree(source);
    LND_LibraryFree();
    return result;
}
