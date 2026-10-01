#pragma once

#include "platform.h"

enum { LND_THREAD_PRIORITY_NORMAL, LND_THREAD_PRIORITY_REALTIME };

typedef void (*lnd_thread_proc)(void *user);

typedef struct lnd_thread {
    void *handle;
} lnd_thread;

typedef struct lnd_mutex {
    alignas(8) unsigned char storage[64];
} lnd_mutex;

typedef struct lnd_event {
    void *handle;
    alignas(8) unsigned char storage[128];
} lnd_event;

#if LND_THREADS
int32_t lnd_thread_create(lnd_thread *t, lnd_thread_proc proc, void *user);
void lnd_thread_join(lnd_thread *t);
void lnd_thread_set_priority(int32_t priority);
void lnd_thread_com_init(void);
void lnd_thread_com_free(void);

void lnd_mutex_init(lnd_mutex *m);
void lnd_mutex_free(lnd_mutex *m);
void lnd_mutex_lock(lnd_mutex *m);
void lnd_mutex_unlock(lnd_mutex *m);

int32_t lnd_event_init(lnd_event *e);
void lnd_event_free(lnd_event *e);
void lnd_event_signal(lnd_event *e);
bool lnd_event_wait(lnd_event *e, uint32_t timeout_ms);

void lnd_sleep_ms(uint32_t ms);
uint64_t lnd_time_ns(void);

#else

LND_INLINE int32_t lnd_thread_create(lnd_thread *t, lnd_thread_proc proc, void *user) { return LND_ERR_UNSUPPORTED; }
LND_INLINE void lnd_thread_join(lnd_thread *t) {}
LND_INLINE void lnd_thread_set_priority(int32_t priority) {}
LND_INLINE void lnd_thread_com_init(void) {}
LND_INLINE void lnd_thread_com_free(void) {}
LND_INLINE void lnd_mutex_init(lnd_mutex *m) {}
LND_INLINE void lnd_mutex_free(lnd_mutex *m) {}
LND_INLINE void lnd_mutex_lock(lnd_mutex *m) {}
LND_INLINE void lnd_mutex_unlock(lnd_mutex *m) {}
LND_INLINE int32_t lnd_event_init(lnd_event *e) { return LND_OK; }
LND_INLINE void lnd_event_free(lnd_event *e) {}
LND_INLINE void lnd_event_signal(lnd_event *e) {}
LND_INLINE bool lnd_event_wait(lnd_event *e, uint32_t timeout_ms) { return false; }
LND_INLINE void lnd_sleep_ms(uint32_t ms) {}
LND_INLINE uint64_t lnd_time_ns(void) { return 0; }

#endif
