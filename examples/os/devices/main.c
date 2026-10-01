#include "lindar_devices.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--null"))) { fprintf(stderr, "Usage: devices [--null]\n"); return 2; }
    if (argc == 2) {
        const LND_DEVICE_BACKEND *backend = LND_DeviceBackendFind("null");
        if (!backend || LND_DeviceSetPreferredBackend(backend) != LND_OK) return 1;
    }
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    if (LND_LibraryInit() != LND_OK) return 1;
    for (int32_t type = LND_DEVICE_OUTPUT; type <= LND_DEVICE_INPUT; type++) {
        for (uint32_t i = 0; i < LND_DeviceGetCount(type); i++) {
            LND_DEVICE *device = LND_DeviceGet(type, i);
            printf("%s %u: %s%s\n  %s, %u Hz, %u channels\n", type == LND_DEVICE_INPUT ? "input" : "output", i,
                   LND_DeviceGetName(device), LND_DeviceIsDefault(device) ? " [default]" : "", LND_DeviceGetId(device),
                   LND_DeviceGetSampleRateHz(device), LND_DeviceGetChannels(device));
        }
    }
    LND_LibraryFree();
    return 0;
}
