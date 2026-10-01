#include "thread.h"
#include "alloc.h"
#include "atomic.h"
#include "lindar.h"

#if LND_OS_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <objbase.h>

typedef struct lnd_thread_start {
    lnd_thread_proc proc;
    void *user;
} lnd_thread_start;

static DWORD WINAPI lnd_thread_trampoline(LPVOID p) {
    lnd_thread_start s = *(lnd_thread_start *)p;
    lnd_free(p);
    s.proc(s.user);
    return 0;
}

int32_t lnd_thread_create(lnd_thread *t, lnd_thread_proc proc, void *user) {
    lnd_thread_start *s = lnd_alloc(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->proc = proc;
    s->user = user;
    HANDLE h = CreateThread(nullptr, 0, lnd_thread_trampoline, s, 0, nullptr);
    if (!h) {
        lnd_free(s);
        return LND_ERR_EXTERNAL;
    }
    t->handle = h;
    return LND_OK;
}

void lnd_thread_join(lnd_thread *t) {
    if (!t->handle) return;
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    t->handle = nullptr;
}

void lnd_thread_set_priority(int32_t priority) {
    SetThreadPriority(GetCurrentThread(), priority == LND_THREAD_PRIORITY_REALTIME ? THREAD_PRIORITY_TIME_CRITICAL : THREAD_PRIORITY_NORMAL);
}

void lnd_thread_com_init(void) { CoInitializeEx(nullptr, COINIT_MULTITHREADED); }

void lnd_thread_com_free(void) { CoUninitialize(); }

void lnd_mutex_init(lnd_mutex *m) { InitializeSRWLock((PSRWLOCK)m->storage); }

void lnd_mutex_free(lnd_mutex *m) { LND_UNUSED(m); }

void lnd_mutex_lock(lnd_mutex *m) { AcquireSRWLockExclusive((PSRWLOCK)m->storage); }

void lnd_mutex_unlock(lnd_mutex *m) { ReleaseSRWLockExclusive((PSRWLOCK)m->storage); }

int32_t lnd_event_init(lnd_event *e) {
    e->handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return e->handle ? LND_OK : LND_ERR_EXTERNAL;
}

void lnd_event_free(lnd_event *e) {
    if (e->handle) CloseHandle(e->handle);
    e->handle = nullptr;
}

void lnd_event_signal(lnd_event *e) { SetEvent(e->handle); }

bool lnd_event_wait(lnd_event *e, uint32_t timeout_ms) {
    return WaitForSingleObject(e->handle, timeout_ms == UINT32_MAX ? INFINITE : timeout_ms) == WAIT_OBJECT_0;
}

void lnd_sleep_ms(uint32_t ms) { Sleep(ms); }

uint64_t lnd_time_ns(void) {
    static lnd_atomic_u64 frequency;
    uint64_t freq = lnd_load(&frequency);
    if (!freq) {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        freq = (uint64_t)value.QuadPart;
        lnd_store(&frequency, freq);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)((double)now.QuadPart * 1e9 / (double)freq);
}

#else

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>

typedef struct lnd_thread_start {
    lnd_thread_proc proc;
    void *user;
} lnd_thread_start;

typedef struct lnd_event_posix {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool signaled;
} lnd_event_posix;

static_assert(sizeof(pthread_mutex_t) <= sizeof(((lnd_mutex *)0)->storage));
static_assert(sizeof(lnd_event_posix) <= sizeof(((lnd_event *)0)->storage));

static void *lnd_thread_trampoline(void *p) {
    lnd_thread_start s = *(lnd_thread_start *)p;
    lnd_free(p);
    s.proc(s.user);
    return nullptr;
}

int32_t lnd_thread_create(lnd_thread *t, lnd_thread_proc proc, void *user) {
    lnd_thread_start *s = lnd_alloc(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->proc = proc;
    s->user = user;
    pthread_t *h = lnd_alloc(sizeof *h);
    if (!h || pthread_create(h, nullptr, lnd_thread_trampoline, s) != 0) {
        lnd_free(h);
        lnd_free(s);
        return LND_ERR_EXTERNAL;
    }
    t->handle = h;
    return LND_OK;
}

void lnd_thread_join(lnd_thread *t) {
    if (!t->handle) return;
    pthread_join(*(pthread_t *)t->handle, nullptr);
    lnd_free(t->handle);
    t->handle = nullptr;
}

void lnd_thread_set_priority(int32_t priority) {
    struct sched_param sp = {0};
    if (priority == LND_THREAD_PRIORITY_REALTIME) {
        sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    } else {
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
    }
}

void lnd_thread_com_init(void) {}

void lnd_thread_com_free(void) {}

void lnd_mutex_init(lnd_mutex *m) { pthread_mutex_init((pthread_mutex_t *)m->storage, nullptr); }

void lnd_mutex_free(lnd_mutex *m) { pthread_mutex_destroy((pthread_mutex_t *)m->storage); }

void lnd_mutex_lock(lnd_mutex *m) { pthread_mutex_lock((pthread_mutex_t *)m->storage); }

void lnd_mutex_unlock(lnd_mutex *m) { pthread_mutex_unlock((pthread_mutex_t *)m->storage); }

int32_t lnd_event_init(lnd_event *e) {
    lnd_event_posix *p = (lnd_event_posix *)e->storage;
    if (pthread_mutex_init(&p->mutex, nullptr) != 0) return LND_ERR_EXTERNAL;
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) {
        pthread_mutex_destroy(&p->mutex);
        return LND_ERR_EXTERNAL;
    }
#if !LND_OS_MACOS && !LND_OS_IOS
    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) != 0) {
        pthread_condattr_destroy(&attr);
        pthread_mutex_destroy(&p->mutex);
        return LND_ERR_EXTERNAL;
    }
#endif
    int r = pthread_cond_init(&p->cond, &attr);
    pthread_condattr_destroy(&attr);
    if (r != 0) {
        pthread_mutex_destroy(&p->mutex);
        return LND_ERR_EXTERNAL;
    }
    p->signaled = false;
    e->handle = p;
    return LND_OK;
}

void lnd_event_free(lnd_event *e) {
    lnd_event_posix *p = (lnd_event_posix *)e->storage;
    if (!e->handle) return;
    pthread_cond_destroy(&p->cond);
    pthread_mutex_destroy(&p->mutex);
    e->handle = nullptr;
}

void lnd_event_signal(lnd_event *e) {
    lnd_event_posix *p = (lnd_event_posix *)e->storage;
    pthread_mutex_lock(&p->mutex);
    p->signaled = true;
    pthread_cond_signal(&p->cond);
    pthread_mutex_unlock(&p->mutex);
}

bool lnd_event_wait(lnd_event *e, uint32_t timeout_ms) {
    lnd_event_posix *p = (lnd_event_posix *)e->storage;
    bool got = false;
    pthread_mutex_lock(&p->mutex);
    if (timeout_ms == UINT32_MAX) {
        while (!p->signaled)
            pthread_cond_wait(&p->cond, &p->mutex);
        got = true;
    } else {
        struct timespec ts;
#if LND_OS_MACOS || LND_OS_IOS
        clock_gettime(CLOCK_REALTIME, &ts);
#else
        clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        while (!p->signaled) {
            if (pthread_cond_timedwait(&p->cond, &p->mutex, &ts) != 0) break;
        }
        got = p->signaled;
    }
    p->signaled = false;
    pthread_mutex_unlock(&p->mutex);
    return got;
}

void lnd_sleep_ms(uint32_t ms) {
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

uint64_t lnd_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#endif
