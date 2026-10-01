#include "callback.h"
#include "alloc.h"
#include "config.h"
#include "lindar.h"

#if LND_USE_LIBC_ALLOC
#include <stdlib.h>
#endif
#include <string.h>

void *lnd_alloc(size_t size) {
    LND_ALLOC_PROC proc = lnd_cfg_ptr(LND_CFG_ALLOC);
    if (proc) {
        lnd_callback_enter();
        void *memory = proc(lnd_cfg_ptr(LND_CFG_ALLOC_USER), size);
        lnd_callback_leave();
        return memory;
    }
#if LND_USE_LIBC_ALLOC
    return malloc(size);
#else
    return nullptr;
#endif
}

void *lnd_alloc_zero(size_t size) {
    void *p = lnd_alloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void *lnd_realloc(void *ptr, size_t size) {
    LND_REALLOC_PROC proc = lnd_cfg_ptr(LND_CFG_REALLOC);
    if (proc) {
        lnd_callback_enter();
        void *memory = proc(lnd_cfg_ptr(LND_CFG_ALLOC_USER), ptr, size);
        lnd_callback_leave();
        return memory;
    }
    if (lnd_cfg_ptr(LND_CFG_ALLOC)) return ptr ? nullptr : lnd_alloc(size);
#if LND_USE_LIBC_ALLOC
    return realloc(ptr, size);
#else
    return nullptr;
#endif
}

void lnd_free(void *ptr) {
    if (!ptr) return;
    LND_FREE_PROC proc = lnd_cfg_ptr(LND_CFG_FREE);
    if (proc) {
        lnd_callback_enter();
        proc(lnd_cfg_ptr(LND_CFG_ALLOC_USER), ptr);
        lnd_callback_leave();
    }
#if LND_USE_LIBC_ALLOC
    else
        free(ptr);
#endif
}

void *lnd_alloc_aligned(size_t size, size_t align) {
    if (align < sizeof(void *)) align = sizeof(void *);
    if ((align & (align - 1)) || size > SIZE_MAX - (align - 1) - sizeof(void *)) return nullptr;
    unsigned char *raw = lnd_alloc(size + align - 1 + sizeof(void *));
    if (!raw) return nullptr;
    uintptr_t base = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (base + align - 1) & ~(uintptr_t)(align - 1);
    ((void **)aligned)[-1] = raw;
    return (void *)aligned;
}

void lnd_free_aligned(void *ptr) {
    if (!ptr) return;
    lnd_free(((void **)ptr)[-1]);
}

char *lnd_strdup(const char *s) {
    if (!s) return nullptr;
    size_t n = strlen(s) + 1;
    char *d = lnd_alloc(n);
    if (d) memcpy(d, s, n);
    return d;
}
