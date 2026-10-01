#pragma once

#include "lindar.h"
#include "atomic.h"
#include "platform.h"

enum {
    LND_CFG_TYPE_U32,
    LND_CFG_TYPE_U64,
    LND_CFG_TYPE_BOOL,
    LND_CFG_TYPE_ENUM,
    LND_CFG_TYPE_PTR,
};

enum { LND_CFG_PHASE_PRE, LND_CFG_PHASE_ANY };

typedef struct lnd_cfg_desc {
    const char *name;
    uint8_t type;
    uint8_t phase;
    uint64_t min;
    uint64_t max;
    uint64_t def;
} lnd_cfg_desc;

struct LND_CONFIG_KEY {
    lnd_atomic_u64 value;
    const lnd_cfg_desc *desc;
};

extern LND_CONFIG_KEY lnd_config_keys[];

extern LND_CONFIG_KEY *const LND_CFG_ALLOC;
extern LND_CONFIG_KEY *const LND_CFG_REALLOC;
extern LND_CONFIG_KEY *const LND_CFG_FREE;
extern LND_CONFIG_KEY *const LND_CFG_ALLOC_USER;

#include "lnd_config_keys.h"

#ifndef LND_INTERNAL_FORMAT_MAX
#define LND_INTERNAL_FORMAT_MAX LND_FORMAT_S32
#endif

#ifndef LND_DEFAULT_INTERNAL_FORMAT
#define LND_DEFAULT_INTERNAL_FORMAT LND_FORMAT_S16
#endif

#ifndef LND_DEFAULT_RUN_MODE
#define LND_DEFAULT_RUN_MODE LND_MODE_SINGLE_THREADED
#endif

typedef struct lnd_callback_config {
    uintptr_t proc;
    void *user;
} lnd_callback_config;

int32_t lnd_config_set(LND_CONFIG_KEY *key, uint64_t value);
int32_t lnd_config_set_callback(LND_CONFIG_KEY *proc_key, LND_CONFIG_KEY *user_key, uintptr_t proc, void *user);
lnd_callback_config lnd_config_get_callback(const LND_CONFIG_KEY *proc_key, const LND_CONFIG_KEY *user_key);

LND_INLINE uint32_t lnd_cfg_u32(const LND_CONFIG_KEY *key) { return (uint32_t)lnd_load_relaxed(&key->value); }
LND_INLINE uint64_t lnd_cfg_u64(const LND_CONFIG_KEY *key) { return lnd_load_relaxed(&key->value); }
LND_INLINE bool lnd_cfg_bool(const LND_CONFIG_KEY *key) { return lnd_load_relaxed(&key->value) != 0; }
LND_INLINE void *lnd_cfg_ptr(const LND_CONFIG_KEY *key) { return (void *)(uintptr_t)lnd_load_relaxed(&key->value); }

void lnd_config_reset(void);

LND_INLINE bool lnd_threads_enabled(void) { return LND_THREADS && lnd_cfg_u32(LND_CFG_RUN_MODE) != LND_MODE_SINGLE_THREADED; }
