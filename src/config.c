#include "config.h"
#include "context.h"
#include "error.h"
#include "lnd_config_handlers.h"
#include "spinlock.h"

#include <string.h>

static lnd_spinlock lnd_config_callback_lock;

static bool lnd_config_known(const LND_CONFIG_KEY *key, size_t count) {
    uintptr_t offset = (uintptr_t)key - (uintptr_t)lnd_config_keys;
    return offset < count * sizeof *key && offset % sizeof *key == 0;
}

static bool lnd_config_public(const LND_CONFIG_KEY *key) { return lnd_config_known(key, LND_CONFIG_PUBLIC_KEY_COUNT); }

void lnd_config_reset(void) {
    lnd_spinlock_lock(&lnd_config_callback_lock);
    for (size_t i = 0; i < LND_CONFIG_KEY_COUNT; i++)
        lnd_store(&lnd_config_keys[i].value, lnd_config_keys[i].desc->def);
    lnd_spinlock_unlock(&lnd_config_callback_lock);
}

static bool lnd_config_validate(const LND_CONFIG_KEY *key, uint64_t value) {
    const lnd_cfg_desc *d = key->desc;
    switch (d->type) {
    case LND_CFG_TYPE_U32:
        return value <= UINT32_MAX && value >= d->min && value <= d->max;
    case LND_CFG_TYPE_U64:
    case LND_CFG_TYPE_ENUM:
        return value >= d->min && value <= d->max;
    case LND_CFG_TYPE_BOOL:
        return value <= 1;
    default:
        return true;
    }
}

static int32_t lnd_config_assign(LND_CONFIG_KEY *key, uint64_t value) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_config_validate(key, value)) return lnd_error(LND_ERR_INVALID_ARG);
    if (LND_INTERNAL_FORMAT_MAX < LND_FORMAT_F64 && key == LND_CFG_INTERNAL_FORMAT && value > LND_INTERNAL_FORMAT_MAX) return lnd_error(LND_ERR_UNSUPPORTED);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    if (key->desc->phase == LND_CFG_PHASE_PRE && lnd_ctx.initialized) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_STATE);
    }
    int32_t result = lnd_config_notify_set(key, value);
    if (result == LND_OK) lnd_store(&key->value, value);
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t lnd_config_set(LND_CONFIG_KEY *key, uint64_t value) {
    if (!lnd_config_known(key, LND_CONFIG_KEY_COUNT)) return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_config_assign(key, value);
}

uint64_t LND_ConfigGet(const LND_CONFIG_KEY *key) {
    if (!lnd_config_public(key)) {
        lnd_error(LND_ERR_INVALID_ARG);
        return 0;
    }
    if (lnd_callback_active()) return lnd_cfg_u64(key);
    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }
    uint64_t value = lnd_cfg_u64(key);
    lnd_context_unlock();
    return value;
}

int32_t LND_ConfigSet(LND_CONFIG_KEY *key, uint64_t value) {
    if (!lnd_config_public(key)) return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_config_assign(key, value);
}

uint32_t LND_ConfigGetKeyCount(void) { return LND_CONFIG_PUBLIC_KEY_COUNT; }

LND_CONFIG_KEY *LND_ConfigGetKey(uint32_t index) { return index < LND_CONFIG_PUBLIC_KEY_COUNT ? &lnd_config_keys[index] : nullptr; }

LND_CONFIG_KEY *LND_ConfigFindKey(const char *name) {
    if (!name) return nullptr;
    for (size_t i = 0; i < LND_CONFIG_PUBLIC_KEY_COUNT; i++) {
        const char *candidate = lnd_config_keys[i].desc->name;
        if (!strcmp(candidate, name)) return &lnd_config_keys[i];
    }
    return nullptr;
}

const char *LND_ConfigKeyGetName(const LND_CONFIG_KEY *key) { return lnd_config_public(key) ? key->desc->name : nullptr; }

int32_t LND_AllocatorSetConfig(const LND_ALLOCATOR_CONFIG *config) {
    LND_ALLOCATOR_CONFIG c = config ? *config : (LND_ALLOCATOR_CONFIG){0};
    if ((!c.alloc != !c.free) || (c.realloc && !c.alloc)) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = lnd_ctx.initialized ? LND_ERR_STATE : LND_OK;
    if (result == LND_OK) {
        lnd_store(&LND_CFG_ALLOC->value, (uintptr_t)c.alloc);
        lnd_store(&LND_CFG_REALLOC->value, (uintptr_t)c.realloc);
        lnd_store(&LND_CFG_FREE->value, (uintptr_t)c.free);
        lnd_store(&LND_CFG_ALLOC_USER->value, (uintptr_t)c.user);
    }
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_AllocatorGetConfig(LND_ALLOCATOR_CONFIG *config) {
    if (!config) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    *config = (LND_ALLOCATOR_CONFIG){.alloc = (LND_ALLOC_PROC)(uintptr_t)lnd_cfg_u64(LND_CFG_ALLOC),
                                   .realloc = (LND_REALLOC_PROC)(uintptr_t)lnd_cfg_u64(LND_CFG_REALLOC),
                                   .free = (LND_FREE_PROC)(uintptr_t)lnd_cfg_u64(LND_CFG_FREE),
                                   .user = lnd_cfg_ptr(LND_CFG_ALLOC_USER)};
    lnd_context_unlock();
    return LND_OK;
}

int32_t lnd_config_set_callback(LND_CONFIG_KEY *proc_key, LND_CONFIG_KEY *user_key, uintptr_t proc, void *user) {
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_spinlock_lock(&lnd_config_callback_lock);
    lnd_store(&proc_key->value, proc);
    lnd_store(&user_key->value, (uintptr_t)user);
    lnd_spinlock_unlock(&lnd_config_callback_lock);
    lnd_context_unlock();
    return LND_OK;
}

lnd_callback_config lnd_config_get_callback(const LND_CONFIG_KEY *proc_key, const LND_CONFIG_KEY *user_key) {
    lnd_spinlock_lock(&lnd_config_callback_lock);
    lnd_callback_config c = {.proc = (uintptr_t)lnd_cfg_u64(proc_key), .user = lnd_cfg_ptr(user_key)};
    lnd_spinlock_unlock(&lnd_config_callback_lock);
    return c;
}
