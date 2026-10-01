#pragma once

#include <stdbool.h>
#include <stdint.h>
#if LND_THREADS
#include <stdatomic.h>

#define lnd_atomic(T) _Atomic(T)

typedef _Atomic(uint32_t) lnd_atomic_u32;
typedef _Atomic(uint64_t) lnd_atomic_u64;
typedef _Atomic(int32_t) lnd_atomic_i32;
typedef _Atomic(void *) lnd_atomic_ptr;

#define lnd_load(p) atomic_load_explicit((p), memory_order_acquire)
#define lnd_store(p, v) atomic_store_explicit((p), (v), memory_order_release)
#define lnd_load_relaxed(p) atomic_load_explicit((p), memory_order_relaxed)
#define lnd_store_relaxed(p, v) atomic_store_explicit((p), (v), memory_order_relaxed)
#define lnd_load_seq(p) atomic_load_explicit((p), memory_order_seq_cst)
#define lnd_store_seq(p, v) atomic_store_explicit((p), (v), memory_order_seq_cst)
#define lnd_add(p, v) atomic_fetch_add_explicit((p), (v), memory_order_acq_rel)
#define lnd_sub(p, v) atomic_fetch_sub_explicit((p), (v), memory_order_acq_rel)
#define lnd_exchange(p, v) atomic_exchange_explicit((p), (v), memory_order_acq_rel)
#define lnd_cas(p, expected, desired) atomic_compare_exchange_strong_explicit((p), (expected), (desired), memory_order_acq_rel, memory_order_acquire)

#else

#define lnd_atomic(T) T

typedef uint32_t lnd_atomic_u32;
typedef uint64_t lnd_atomic_u64;
typedef int32_t lnd_atomic_i32;
typedef void *lnd_atomic_ptr;

#define LND_SINGLE_ARITHMETIC(T, suffix)                                                                                                                       \
    static inline T lnd_single_add_##suffix(T *p, T v) {                                                                                                       \
        T old = *p;                                                                                                                                            \
        *p += v;                                                                                                                                               \
        return old;                                                                                                                                            \
    }                                                                                                                                                          \
    static inline T lnd_single_sub_##suffix(T *p, T v) {                                                                                                       \
        T old = *p;                                                                                                                                            \
        *p -= v;                                                                                                                                               \
        return old;                                                                                                                                            \
    }

LND_SINGLE_ARITHMETIC(uint32_t, u32)
LND_SINGLE_ARITHMETIC(uint64_t, u64)

static inline uint32_t lnd_single_exchange(uint32_t *p, uint32_t v) {
    uint32_t old = *p;
    *p = v;
    return old;
}
static inline bool lnd_single_cas(uint32_t *p, uint32_t *expected, uint32_t desired) {
    if (*p == *expected) {
        *p = desired;
        return true;
    }
    *expected = *p;
    return false;
}

#define lnd_load(p) (*(p))
#define lnd_store(p, v) (*(p) = (v))
#define lnd_load_relaxed(p) lnd_load(p)
#define lnd_store_relaxed(p, v) lnd_store(p, v)
#define lnd_load_seq(p) lnd_load(p)
#define lnd_store_seq(p, v) lnd_store(p, v)
#define lnd_add(p, v) _Generic((p), uint32_t *: lnd_single_add_u32, uint64_t *: lnd_single_add_u64)((p), (v))
#define lnd_sub(p, v) _Generic((p), uint32_t *: lnd_single_sub_u32, uint64_t *: lnd_single_sub_u64)((p), (v))
#define lnd_exchange(p, v) lnd_single_exchange((p), (v))
#define lnd_cas(p, expected, desired) lnd_single_cas((p), (expected), (desired))

#endif
