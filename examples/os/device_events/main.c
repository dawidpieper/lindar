#include "lindar_devices.h"
#include "../common.h"
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

static void changed(void *user, int32_t event, LND_DEVICE_INSTANCE *instance) {
    atomic_uint *pending = user;
    (void)instance;
    atomic_fetch_or(pending, 1u << event);
}

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--null"))) { fprintf(stderr, "Usage: device_events [--null]\n"); return 2; }
    if (argc == 2) {
        const LND_DEVICE_BACKEND *backend = LND_DeviceBackendFind("null");
        if (!backend || LND_DeviceSetPreferredBackend(backend) != LND_OK) return 1;
    }
    atomic_uint pending = 0;
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_REALTIME);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    LND_DeviceSetEventCallback(changed, &pending);
    if (LND_LibraryInit() != LND_OK) return 1;
    puts("Watching device changes for five seconds...");
    int32_t result = LND_OK;
    for (unsigned i = 0; i < 100 && result == LND_OK; i++) {
        result = LND_LibraryUpdate();
        unsigned events = atomic_exchange(&pending, 0);
        for (int event = 0; event <= LND_DEVICE_EVENT_INSTANCE_REOPENED; event++)
            if (events & (1u << event)) printf("device event: %d\n", event);
        sleep_ms(50);
    }
    LND_LibraryFree();
    return result == LND_OK ? 0 : 1;
}
