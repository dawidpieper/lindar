#include "backend.h"
#include "context.h"
#include "src/config.h"
#include "src/error.h"
#include "lnd_device_backends.h"

#include <string.h>

uint32_t LND_DeviceBackendGetCount(void) { return sizeof lnd_backends / sizeof *lnd_backends - 1; }

const LND_DEVICE_BACKEND *LND_DeviceBackendGet(uint32_t index) {
    return index < LND_DeviceBackendGetCount() ? lnd_backends[index] : nullptr;
}

const LND_DEVICE_BACKEND *LND_DeviceBackendFind(const char *name) {
    if (!name) return nullptr;
    for (size_t i = 0; lnd_backends[i]; i++)
        if (!strcmp(lnd_backends[i]->name, name)) return lnd_backends[i];
    return nullptr;
}

static bool lnd_backend_known(const LND_DEVICE_BACKEND *backend) {
    for (size_t i = 0; lnd_backends[i]; i++)
        if (lnd_backends[i] == backend) return true;
    return false;
}

const char *LND_DeviceBackendGetName(const LND_DEVICE_BACKEND *backend) {
    return lnd_backend_known(backend) ? backend->name : nullptr;
}

int32_t LND_DeviceSetPreferredBackend(const LND_DEVICE_BACKEND *backend) {
    if (backend && !lnd_backend_known(backend)) return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_config_set(LND_CFG_DEVICES_BACKEND, (uintptr_t)backend);
}

const LND_DEVICE_BACKEND *LND_DeviceGetPreferredBackend(void) { return lnd_cfg_ptr(LND_CFG_DEVICES_BACKEND); }

const lnd_backend_vt *lnd_backend_default(void) { return lnd_backends[0]; }

const LND_DEVICE_BACKEND *LND_DeviceGetActiveBackend(void) {
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    const LND_DEVICE_BACKEND *backend = lnd_device_ctx.backend_ready ? lnd_device_ctx.backend.vt : nullptr;
    lnd_context_unlock();
    return backend;
}
