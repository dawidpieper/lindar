#pragma once

#include "src/platform.h"
#include "src/atomic.h"
#include "lindar_devices.h"

typedef struct LND_DEVICE_INSTANCE lnd_instance;

struct LND_DEVICE {
    int32_t type;
    char *name;
    struct lnd_device_name *names;
    char *id;
    lnd_atomic_u32 flags;
    lnd_atomic_u32 sample_rate_hz;
    lnd_atomic_u32 channels;
    lnd_atomic_i32 format;
    lnd_atomic_u32 default_period;
    lnd_atomic_u32 min_period;
    lnd_atomic_u32 max_instances;
    LND_DEVICE_MODE *modes;
    lnd_atomic_u32 modes_count;
    uint32_t modes_cap;
    lnd_instance **instances;
    lnd_atomic_u32 instances_count;
    uint32_t instances_cap;
    bool exclusive_open;
    void *backend_data;
    void (*backend_data_free)(void *);
};

typedef struct LND_DEVICE lnd_device;

typedef struct lnd_device_list {
    lnd_device **items;
    uint32_t count;
    uint32_t cap;
} lnd_device_list;

lnd_device *lnd_device_new(int32_t type, const char *name, const char *id);
void lnd_device_free(lnd_device *d);
int32_t lnd_device_add_mode(lnd_device *d, uint32_t sample_rate_hz, uint32_t channels, int32_t format, uint32_t flags);
int32_t lnd_device_list_push(lnd_device_list *l, lnd_device *d);
void lnd_device_list_free(lnd_device_list *l);
int32_t lnd_device_attach_instance(lnd_device *d, lnd_instance *i);
void lnd_device_detach_instance(lnd_device *d, lnd_instance *i);
int32_t lnd_devices_ensure(int32_t type);
int32_t lnd_devices_rescan_all(void);
lnd_device *lnd_devices_default(int32_t type);
bool lnd_device_is_default_handle(const lnd_device *device);
lnd_device *lnd_device_resolve(lnd_device *device);
void lnd_devices_free_all(void);
