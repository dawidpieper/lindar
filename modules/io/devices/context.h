#pragma once

#include "src/context.h"
#include "device.h"
#include "backend.h"

typedef struct lnd_device_context {
    lnd_backend backend;
    bool backend_ready;
    lnd_device_list devices[2];
    bool devices_valid[2];
    lnd_instance *instances;
    lnd_instance *default_output;
} lnd_device_context;

extern lnd_device_context lnd_device_ctx;

int32_t lnd_context_ensure_backend(void);
bool lnd_context_has_instance(const lnd_instance *i);
lnd_instance *lnd_context_default_output(void);

void lnd_context_set_default_output(struct LND_DEVICE_INSTANCE *instance);
