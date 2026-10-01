#include "src/thread.h"
#include "src/atomic.h"
#include <stdio.h>

static unsigned checks, failures;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

typedef struct state {
    lnd_mutex mutex;
    lnd_event ready;
    lnd_event proceed;
    unsigned count;
} state;

static void worker(void *user) {
    state *s = user;
    lnd_event_signal(&s->ready);
    lnd_event_wait(&s->proceed, UINT32_MAX);
    for (unsigned i = 0; i < 10000; i++) {
        lnd_mutex_lock(&s->mutex);
        s->count++;
        lnd_mutex_unlock(&s->mutex);
    }
}

int main(void) {
    state s = {0};
    lnd_mutex_init(&s.mutex);
    CHECK(lnd_event_init(&s.ready) == LND_OK);
    CHECK(lnd_event_init(&s.proceed) == LND_OK);
    CHECK(!lnd_event_wait(&s.ready, 0));
    lnd_event_signal(&s.ready);
    lnd_event_signal(&s.ready);
    CHECK(lnd_event_wait(&s.ready, 0));
    CHECK(!lnd_event_wait(&s.ready, 0));
    uint64_t start = lnd_time_ns();
    CHECK(!lnd_event_wait(&s.ready, 10));
    CHECK(lnd_time_ns() >= start + 5000000);
    for (unsigned pass = 0; pass < 3; pass++) {
        lnd_thread thread = {0};
        CHECK(lnd_thread_create(&thread, worker, &s) == LND_OK);
        CHECK(lnd_event_wait(&s.ready, 2000));
        lnd_event_signal(&s.proceed);
        for (unsigned i = 0; i < 10000; i++) {
            lnd_mutex_lock(&s.mutex);
            s.count++;
            lnd_mutex_unlock(&s.mutex);
        }
        lnd_thread_join(&thread);
        CHECK(thread.handle == nullptr);
        lnd_thread_join(&thread);
        CHECK(s.count == (pass + 1) * 20000);
    }
    start = lnd_time_ns();
    lnd_sleep_ms(5);
    CHECK(lnd_time_ns() >= start + 1000000);
    lnd_event_free(&s.ready);
    lnd_event_free(&s.ready);
    lnd_event_free(&s.proceed);
    lnd_mutex_free(&s.mutex);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
