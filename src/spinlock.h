#pragma once

#include "atomic.h"
#include "platform.h"

typedef struct lnd_spinlock {
    lnd_atomic_u32 flag;
} lnd_spinlock;

LND_INLINE void lnd_spin_pause(void) {
#if defined(LND_ARCH_X64) || defined(LND_ARCH_X86)
    __builtin_ia32_pause();
#elif defined(LND_ARCH_ARM64) || defined(LND_ARCH_ARM)
    __asm__ volatile("yield");
#endif
}

LND_INLINE void lnd_spinlock_lock(lnd_spinlock *l) {
    for (;;) {
        uint32_t expected = 0;
        if (lnd_cas(&l->flag, &expected, 1)) return;
        while (lnd_load_relaxed(&l->flag)) lnd_spin_pause();
    }
}

LND_INLINE bool lnd_spinlock_try(lnd_spinlock *l) {
    uint32_t expected = 0;
    return lnd_cas(&l->flag, &expected, 1);
}

LND_INLINE void lnd_spinlock_unlock(lnd_spinlock *l) { lnd_store(&l->flag, 0); }
