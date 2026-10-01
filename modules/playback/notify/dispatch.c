#include "notify.h"
#include "src/context.h"
#include "src/thread.h"

#if LND_THREADS
static struct {
    lnd_thread thread;
    lnd_event event;
    lnd_atomic_u32 pending;
    lnd_atomic_u32 stop;
    bool running;
} lnd_dispatcher;

static void lnd_notify_thread(void *user) {
    LND_UNUSED(user);
    while (!lnd_load(&lnd_dispatcher.stop)) {
        lnd_event_wait(&lnd_dispatcher.event, UINT32_MAX);
        lnd_context_lock();
        if (!lnd_load(&lnd_dispatcher.stop) && lnd_ctx.initialized && !lnd_ctx.closing) {
            lnd_exchange(&lnd_dispatcher.pending, 0);
            lnd_notify_dispatch(true);
        }
        lnd_context_unlock();
    }
}

int32_t lnd_notify_dispatch_start(void) {
    if (lnd_dispatcher.running)
        return LND_OK;
    lnd_store(&lnd_dispatcher.stop, 0);
    lnd_store(&lnd_dispatcher.pending, 0);
    int32_t result = lnd_event_init(&lnd_dispatcher.event);
    if (result != LND_OK)
        return result;
    result = lnd_thread_create(&lnd_dispatcher.thread, lnd_notify_thread, nullptr);
    if (result != LND_OK) {
        lnd_event_free(&lnd_dispatcher.event);
        return result;
    }
    lnd_dispatcher.running = true;
    return LND_OK;
}

void lnd_notify_dispatch_wake(void) {
    if (!lnd_exchange(&lnd_dispatcher.pending, 1))
        lnd_event_signal(&lnd_dispatcher.event);
}

void lnd_notify_dispatch_stop(void) {
    if (!lnd_dispatcher.running)
        return;
    lnd_store(&lnd_dispatcher.stop, 1);
    lnd_event_signal(&lnd_dispatcher.event);
    lnd_context_unlock();
    lnd_thread_join(&lnd_dispatcher.thread);
    lnd_context_lock();
    lnd_event_free(&lnd_dispatcher.event);
    lnd_dispatcher.running = false;
}
#else
int32_t lnd_notify_dispatch_start(void) { return LND_ERR_UNSUPPORTED; }
void lnd_notify_dispatch_wake(void) {}
void lnd_notify_dispatch_stop(void) {}
#endif
