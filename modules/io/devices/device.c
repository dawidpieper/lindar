#include "context.h"
#include "device.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/context.h"
#include "src/error.h"
#include "engine.h"

#include <string.h>

typedef struct lnd_device_name {
    struct lnd_device_name *next;
    char text[];
} lnd_device_name;

static lnd_device lnd_default_input = {.type = LND_DEVICE_INPUT, .name = "Default input", .id = "lindar:default-input", .flags = LND_DEVICE_FLAG_DEFAULT};
static lnd_device lnd_default_output = {.type = LND_DEVICE_OUTPUT, .name = "Default output", .id = "lindar:default-output", .flags = LND_DEVICE_FLAG_DEFAULT};

LND_DEVICE *const LND_DEVICE_DEFAULT_INPUT = &lnd_default_input;
LND_DEVICE *const LND_DEVICE_DEFAULT_OUTPUT = &lnd_default_output;

LND_DEVICE *LND_DeviceGetDefaultHandle(int32_t type) {
    if (type == LND_DEVICE_INPUT) return LND_DEVICE_DEFAULT_INPUT;
    if (type == LND_DEVICE_OUTPUT) return LND_DEVICE_DEFAULT_OUTPUT;
    return lnd_error_null(LND_ERR_INVALID_ARG);
}

bool lnd_device_is_default_handle(const lnd_device *device) {
    return device == &lnd_default_input || device == &lnd_default_output;
}

lnd_device *lnd_device_resolve(lnd_device *device) {
    return lnd_device_is_default_handle(device) ? lnd_devices_default(device->type) : device;
}

LND_DEVICE *LND_DeviceResolve(LND_DEVICE *device) {
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_device *resolved = lnd_device_resolve(device);
    lnd_context_unlock();
    return resolved ? resolved : lnd_error_null(LND_ERR_NO_DEVICE);
}

lnd_device *lnd_device_new(int32_t type, const char *name, const char *id) {
    lnd_device *d = lnd_alloc_zero(sizeof *d);
    if (!d) return nullptr;
    d->type = type;
    if (!name) name = "";
    size_t length = strlen(name);
    if (length < SIZE_MAX - sizeof *d->names) d->names = lnd_alloc(sizeof *d->names + length + 1);
    if (d->names) {
        d->names->next = nullptr;
        memcpy(d->names->text, name, length + 1);
        d->name = d->names->text;
    }
    d->id = lnd_strdup(id ? id : "");
    if (!d->name || !d->id) {
        lnd_device_free(d);
        return nullptr;
    }
    return d;
}

void lnd_device_free(lnd_device *d) {
    if (!d) return;
    if (d->backend_data && d->backend_data_free) d->backend_data_free(d->backend_data);
    while (d->names) {
        lnd_device_name *next = d->names->next;
        lnd_free(d->names);
        d->names = next;
    }
    lnd_free(d->id);
    lnd_free(d->modes);
    lnd_free(d->instances);
    lnd_free(d);
}

int32_t lnd_device_add_mode(lnd_device *d, uint32_t sample_rate_hz, uint32_t channels, int32_t format, uint32_t flags) {
    if (d->modes_count == d->modes_cap) {
        uint32_t limit = (uint32_t)LND_MIN((uint64_t)UINT32_MAX, SIZE_MAX / sizeof(LND_DEVICE_MODE));
        if (d->modes_cap >= limit) return LND_ERR_OUT_OF_MEMORY;
        uint32_t cap = d->modes_cap ? (d->modes_cap > limit / 2 ? limit : d->modes_cap * 2) : 8;
        LND_DEVICE_MODE *m = lnd_realloc(d->modes, sizeof *m * cap);
        if (!m) return LND_ERR_OUT_OF_MEMORY;
        d->modes = m;
        d->modes_cap = cap;
    }
    d->modes[d->modes_count++] = (LND_DEVICE_MODE){.sample_rate_hz = sample_rate_hz, .channels = channels, .format = format, .flags = flags};
    return LND_OK;
}

int32_t lnd_device_list_push(lnd_device_list *l, lnd_device *d) {
    if (l->count == l->cap) {
        uint32_t limit = (uint32_t)LND_MIN((uint64_t)UINT32_MAX, SIZE_MAX / sizeof(lnd_device *));
        if (l->cap >= limit) return LND_ERR_OUT_OF_MEMORY;
        uint32_t cap = l->cap ? (l->cap > limit / 2 ? limit : l->cap * 2) : 8;
        lnd_device **items = lnd_realloc(l->items, sizeof *items * cap);
        if (!items) return LND_ERR_OUT_OF_MEMORY;
        l->items = items;
        l->cap = cap;
    }
    l->items[l->count++] = d;
    return LND_OK;
}

void lnd_device_list_free(lnd_device_list *l) {
    for (uint32_t i = 0; i < l->count; i++) lnd_device_free(l->items[i]);
    lnd_free(l->items);
    memset(l, 0, sizeof *l);
}

int32_t lnd_device_attach_instance(lnd_device *d, lnd_instance *i) {
    if (d->instances_count == d->instances_cap) {
        uint32_t limit = (uint32_t)LND_MIN((uint64_t)UINT32_MAX, SIZE_MAX / sizeof(lnd_instance *));
        if (d->instances_cap >= limit) return LND_ERR_OUT_OF_MEMORY;
        uint32_t cap = d->instances_cap ? (d->instances_cap > limit / 2 ? limit : d->instances_cap * 2) : 4;
        lnd_instance **items = lnd_realloc(d->instances, sizeof *items * cap);
        if (!items) return LND_ERR_OUT_OF_MEMORY;
        d->instances = items;
        d->instances_cap = cap;
    }
    d->instances[d->instances_count++] = i;
    return LND_OK;
}

void lnd_device_detach_instance(lnd_device *d, lnd_instance *i) {
    for (uint32_t k = 0; k < d->instances_count; k++) {
        if (d->instances[k] == i) {
            d->instances[k] = d->instances[--d->instances_count];
            return;
        }
    }
}

static void lnd_device_update_name(lnd_device *old, lnd_device *fresh) {
    if (!strcmp(old->name, fresh->name)) return;
    for (lnd_device_name *name = old->names; name; name = name->next) {
        if (strcmp(name->text, fresh->name)) continue;
        old->name = name->text;
        return;
    }
    fresh->names->next = old->names;
    old->names = fresh->names;
    old->name = fresh->name;
    fresh->names = nullptr;
}

static void lnd_device_update(lnd_device *old, lnd_device *fresh) {
    lnd_device_update_name(old, fresh);
    LND_DEVICE_MODE *modes = old->modes;
    old->modes = fresh->modes;
    fresh->modes = modes;
    uint32_t mc = old->modes_count, mcap = old->modes_cap;
    old->modes_count = fresh->modes_count;
    old->modes_cap = fresh->modes_cap;
    fresh->modes_count = mc;
    fresh->modes_cap = mcap;
    void *bd = old->backend_data;
    void (*bdf)(void *) = old->backend_data_free;
    old->backend_data = fresh->backend_data;
    old->backend_data_free = fresh->backend_data_free;
    fresh->backend_data = bd;
    fresh->backend_data_free = bdf;
    old->flags = fresh->flags;
    old->sample_rate_hz = fresh->sample_rate_hz;
    old->channels = fresh->channels;
    old->format = fresh->format;
    old->default_period = fresh->default_period;
    old->min_period = fresh->min_period;
    old->max_instances = fresh->max_instances;
}

static int32_t lnd_devices_scan(int32_t type) {
    int32_t r = lnd_context_ensure_backend();
    if (r != LND_OK) return r;
    lnd_device_list fresh = {0};
    r = lnd_device_ctx.backend.vt->enumerate(&lnd_device_ctx.backend, type, &fresh);
    if (r != LND_OK) {
        lnd_device_list_free(&fresh);
        return r;
    }
    lnd_device_list *old = &lnd_device_ctx.devices[type];
    for (uint32_t i = 0; i < old->count; i++) {
        lnd_device *od = old->items[i];
        if (od->instances_count == 0) {
            if (lnd_cfg_ptr(LND_CFG_DEVICES_OUTPUT_DEVICE) == od) lnd_store(&LND_CFG_DEVICES_OUTPUT_DEVICE->value, 0);
            if (lnd_cfg_ptr(LND_CFG_DEVICES_INPUT_DEVICE) == od) lnd_store(&LND_CFG_DEVICES_INPUT_DEVICE->value, 0);
            lnd_device_free(od);
            continue;
        }
        bool found = false;
        for (uint32_t j = 0; j < fresh.count; j++) {
            lnd_device *nd = fresh.items[j];
            if (strcmp(nd->id, od->id) == 0) {
                lnd_device_update(od, nd);
                lnd_device_free(nd);
                fresh.items[j] = od;
                found = true;
                break;
            }
        }
        if (!found) {
            od->flags = (od->flags | LND_DEVICE_FLAG_STALE) & ~LND_DEVICE_FLAG_DEFAULT;
            lnd_device_list_push(&fresh, od);
        }
    }
    lnd_free(old->items);
    *old = fresh;
    lnd_device_ctx.devices_valid[type] = true;
    return LND_OK;
}

int32_t lnd_devices_ensure(int32_t type) {
    if (type != LND_DEVICE_OUTPUT && type != LND_DEVICE_INPUT) return LND_ERR_INVALID_ARG;
    if (lnd_device_ctx.devices_valid[type]) return LND_OK;
    return lnd_devices_scan(type);
}

int32_t lnd_devices_rescan_all(void) {
    if (!lnd_device_ctx.backend_ready) return LND_OK;
    int32_t r = lnd_devices_scan(LND_DEVICE_OUTPUT);
    if (r == LND_OK) r = lnd_devices_scan(LND_DEVICE_INPUT);
    return r;
}

LND_DEVICE *LND_DeviceFind(int32_t type, const char *id) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!id || (type != LND_DEVICE_OUTPUT && type != LND_DEVICE_INPUT)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_device *found = nullptr;
    if (lnd_devices_ensure(type) == LND_OK) {
        lnd_device_list *l = &lnd_device_ctx.devices[type];
        for (uint32_t i = 0; i < l->count; i++) {
            if (strcmp(l->items[i]->id, id) == 0) {
                found = l->items[i];
                break;
            }
        }
    }
    lnd_context_unlock();
    if (!found) lnd_error(LND_ERR_NO_DEVICE);
    return found;
}

lnd_device *lnd_devices_default(int32_t type) {
    if (lnd_devices_ensure(type) != LND_OK) return nullptr;
    lnd_device_list *l = &lnd_device_ctx.devices[type];
    for (uint32_t i = 0; i < l->count; i++) {
        if (l->items[i]->flags & LND_DEVICE_FLAG_DEFAULT) return l->items[i];
    }
    for (uint32_t i = 0; i < l->count; i++)
        if (!(l->items[i]->flags & LND_DEVICE_FLAG_STALE)) return l->items[i];
    return nullptr;
}

void lnd_devices_free_all(void) {
    for (int32_t t = 0; t < 2; t++) {
        lnd_device_list_free(&lnd_device_ctx.devices[t]);
        lnd_device_ctx.devices_valid[t] = false;
    }
}

int32_t LND_DeviceRefresh(void) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t r = lnd_devices_scan(LND_DEVICE_OUTPUT);
    if (r == LND_OK) r = lnd_devices_scan(LND_DEVICE_INPUT);
    lnd_engine_wake();
    lnd_context_unlock();
    return lnd_error(r);
}

uint32_t LND_DeviceGetCount(int32_t type) {
    if (lnd_callback_active()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }

    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }
    uint32_t n = 0;
    int32_t r = lnd_devices_ensure(type);
    if (r == LND_OK) n = lnd_device_ctx.devices[type].count;
    lnd_context_unlock();
    lnd_error(r);
    return n;
}

LND_DEVICE *LND_DeviceGet(int32_t type, uint32_t index) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_device *d = nullptr;
    int32_t r = lnd_devices_ensure(type);
    if (r == LND_OK) {
        if (index < lnd_device_ctx.devices[type].count) d = lnd_device_ctx.devices[type].items[index];
        else r = LND_ERR_INVALID_ARG;
    }
    lnd_context_unlock();
    lnd_error(r);
    return d;
}

LND_DEVICE *LND_DeviceGetDefault(int32_t type) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_device *d = (type == LND_DEVICE_OUTPUT || type == LND_DEVICE_INPUT) ? lnd_devices_default(type) : nullptr;
    lnd_context_unlock();
    if (!d) lnd_error(LND_ERR_NO_DEVICE);
    return d;
}

const char *LND_DeviceGetName(const LND_DEVICE *d) {
    if (!d) return "";
    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return "";
    }
    const char *name = d->name;
    lnd_context_unlock();
    return name;
}

const char *LND_DeviceGetId(const LND_DEVICE *d) { return d ? d->id : ""; }

int32_t LND_DeviceGetType(const LND_DEVICE *d) { return d ? d->type : LND_DEVICE_OUTPUT; }

bool LND_DeviceIsDefault(const LND_DEVICE *d) { return d ? (d->flags & LND_DEVICE_FLAG_DEFAULT) != 0 : false; }

uint32_t LND_DeviceGetFlags(const LND_DEVICE *d) { return d ? d->flags : 0; }

uint32_t LND_DeviceGetSampleRateHz(const LND_DEVICE *d) { return d ? d->sample_rate_hz : 0; }

uint32_t LND_DeviceGetChannels(const LND_DEVICE *d) { return d ? d->channels : 0; }

int32_t LND_DeviceGetFormat(const LND_DEVICE *d) { return d ? d->format : LND_FORMAT_NONE; }

uint32_t LND_DeviceGetDefaultPeriodFrames(const LND_DEVICE *d) { return d ? d->default_period : 0; }

uint32_t LND_DeviceGetMinPeriodFrames(const LND_DEVICE *d) { return d ? d->min_period : 0; }

uint32_t LND_DeviceGetModeCount(const LND_DEVICE *d) { return d ? d->modes_count : 0; }

int32_t LND_DeviceGetMode(const LND_DEVICE *d, uint32_t index, LND_DEVICE_MODE *out) {
    if (!d || !out) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = index < d->modes_count ? LND_OK : LND_ERR_INVALID_ARG;
    if (result == LND_OK) *out = d->modes[index];
    lnd_context_unlock();
    return lnd_error(result);
}

uint32_t LND_DeviceGetMaxInstances(const LND_DEVICE *d) { return d ? d->max_instances : 0; }

uint32_t LND_DeviceGetInstanceCount(const LND_DEVICE *d) { return d ? d->instances_count : 0; }

LND_DEVICE_INSTANCE *LND_DeviceGetInstance(const LND_DEVICE *d, uint32_t index) {
    if (!d) return nullptr;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_instance *instance = index < d->instances_count ? d->instances[index] : nullptr;
    lnd_context_unlock();
    return instance;
}

LND_DEVICE_INSTANCE *LND_NodeGetDeviceInstance(const LND_NODE *node) { return node ? node->instance : nullptr; }
