#include "context.h"
#include "engine.h"
#include "src/config.h"
#include "src/error.h"
#include "utility/log/log.h"

lnd_device_context lnd_device_ctx;

int32_t lnd_context_ensure_backend(void) {
    if (lnd_cfg_u32(LND_CFG_RUN_MODE) != LND_MODE_REALTIME || !LND_OS_MODE || !LND_THREADS) return LND_ERR_UNSUPPORTED;
    if (lnd_device_ctx.backend_ready) return LND_OK;
    const lnd_backend_vt *vt = lnd_cfg_ptr(LND_CFG_DEVICES_BACKEND);
    if (!vt) vt = lnd_backend_default();
    if (!vt) return LND_ERR_UNSUPPORTED;
    lnd_device_ctx.backend.vt = vt;
    lnd_device_ctx.backend.data = nullptr;
    lnd_device_ctx.backend.wake = lnd_engine_wake;
    int32_t r = vt->init(&lnd_device_ctx.backend);
    if (r != LND_OK) {
        LND_LOG_E("backend %s init failed: %s", vt->name, LND_ErrorGetString(r));
        return r;
    }
    lnd_device_ctx.backend_ready = true;
    LND_LOG_I("backend: %s", vt->name);
    return LND_OK;
}

bool lnd_context_has_instance(const lnd_instance *i) {
    for (lnd_instance *p = lnd_device_ctx.instances; p; p = p->next) {
        if (p == i) return true;
    }
    return false;
}

lnd_instance *lnd_context_default_output(void) {
    if (lnd_device_ctx.default_output) return lnd_device_ctx.default_output;
    if (!lnd_ctx.initialized) return lnd_error_null(LND_ERR_STATE);
    if (!lnd_cfg_bool(LND_CFG_DEVICES_AUTO_OPEN)) return lnd_error_null(LND_ERR_NO_DEVICE);
    lnd_device *d = lnd_cfg_ptr(LND_CFG_DEVICES_OUTPUT_DEVICE);
    if (!d) d = LND_DEVICE_DEFAULT_OUTPUT;
    bool follow = lnd_device_is_default_handle(d);
    d = lnd_device_resolve(d);
    if (!d) return lnd_error_null(LND_ERR_NO_DEVICE);
    lnd_instance *i = lnd_instance_open(d, follow);
    if (i) lnd_context_set_default_output(i);
    return i;
}

int32_t lnd_devices_init(void) { return lnd_cfg_u32(LND_CFG_RUN_MODE) == LND_MODE_REALTIME ? lnd_context_ensure_backend() : LND_OK; }

int32_t lnd_devices_start(void) {
    if (lnd_cfg_u32(LND_CFG_RUN_MODE) != LND_MODE_REALTIME) return LND_OK;
    int32_t r = lnd_engine_start();
    if (r != LND_OK) return r;
    if (lnd_cfg_bool(LND_CFG_DEVICES_AUTO_OPEN) && !lnd_context_default_output()) {
        int32_t r = LND_ErrorGetLast();
        LND_LOG_W("auto open failed: %s", LND_ErrorGetString(r));
        return r;
    }
    return LND_OK;
}

void lnd_devices_stop(void) {
    lnd_engine_stop();
    lnd_instances_stop_all();
}

void lnd_devices_free(void) {
    lnd_instances_destroy_all();
    lnd_devices_free_all();
    if (lnd_device_ctx.backend_ready) {
        lnd_device_ctx.backend.vt->free(&lnd_device_ctx.backend);
        lnd_device_ctx.backend_ready = false;
        lnd_device_ctx.backend.vt = nullptr;
    }
    lnd_context_set_default_output(nullptr);
}

int32_t lnd_devices_config_set(LND_CONFIG_KEY *key, uint64_t value) {
    if (key == LND_CFG_DEVICES_OUTPUT_DEVICE || key == LND_CFG_DEVICES_INPUT_DEVICE) {
        lnd_device *d = (lnd_device *)(uintptr_t)value;
        if (lnd_device_is_default_handle(d) && d->type != (key == LND_CFG_DEVICES_INPUT_DEVICE ? LND_DEVICE_INPUT : LND_DEVICE_OUTPUT)) return LND_ERR_INVALID_ARG;
    }
    if (key == LND_CFG_DEVICES_FOLLOW_DEFAULT && value) lnd_engine_wake();
    if (key == LND_CFG_DEVICES_OUTPUT_INSTANCE) {
        lnd_instance *i = (lnd_instance *)(uintptr_t)value;
        if (i && !lnd_context_has_instance(i)) return LND_ERR_INVALID_ARG;
        lnd_context_set_default_output(i);
    }
    return LND_OK;
}

void lnd_context_set_default_output(lnd_instance *instance) {
    lnd_device_ctx.default_output = instance;
    lnd_store(&LND_CFG_DEVICES_OUTPUT_INSTANCE->value, (uint64_t)(uintptr_t)instance);
}

int32_t LND_DeviceSetPreferred(int32_t type, LND_DEVICE *device) {
    if (type != LND_DEVICE_INPUT && type != LND_DEVICE_OUTPUT) return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_config_set(type == LND_DEVICE_INPUT ? LND_CFG_DEVICES_INPUT_DEVICE : LND_CFG_DEVICES_OUTPUT_DEVICE, (uintptr_t)device);
}

LND_DEVICE *LND_DeviceGetPreferred(int32_t type) {
    if (type != LND_DEVICE_INPUT && type != LND_DEVICE_OUTPUT) return lnd_error_null(LND_ERR_INVALID_ARG);
    return lnd_cfg_ptr(type == LND_DEVICE_INPUT ? LND_CFG_DEVICES_INPUT_DEVICE : LND_CFG_DEVICES_OUTPUT_DEVICE);
}

int32_t LND_DeviceSetOutputInstance(LND_DEVICE_INSTANCE *instance) { return lnd_config_set(LND_CFG_DEVICES_OUTPUT_INSTANCE, (uintptr_t)instance); }

int32_t LND_DeviceSetEventCallback(LND_DEVICE_PROC proc, void *user) { return lnd_config_set_callback(LND_CFG_DEVICES_DEVICE_PROC, LND_CFG_DEVICES_DEVICE_USER, (uintptr_t)proc, user); }
