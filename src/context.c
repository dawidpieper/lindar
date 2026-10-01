#include "context.h"
#include "config.h"
#include "error.h"
#include "module.h"
#include "render.h"

lnd_context lnd_ctx;

static lnd_atomic_u32 lnd_ctx_once;
#if LND_THREADS
static thread_local uint32_t lnd_context_depth;
#endif

void lnd_context_lock(void) {
#if LND_THREADS
    if (lnd_context_depth++) return;
#endif
    uint32_t state = lnd_load(&lnd_ctx_once);
    if (state != 2) {
        uint32_t expected = 0;
        if (lnd_cas(&lnd_ctx_once, &expected, 1)) {
            lnd_mutex_init(&lnd_ctx.mutex);
            lnd_store(&lnd_ctx_once, 2);
        } else {
            while (lnd_load(&lnd_ctx_once) != 2) {
            }
        }
    }
    lnd_mutex_lock(&lnd_ctx.mutex);
}

void lnd_context_unlock(void) {
#if LND_THREADS
    if (--lnd_context_depth) return;
#endif
    lnd_mutex_unlock(&lnd_ctx.mutex);
}

bool lnd_context_enter(void) {
    if (lnd_callback_active()) return false;
    lnd_context_lock();
    if (!lnd_ctx.closing) return true;
    lnd_context_unlock();
    return false;
}

static void lnd_context_cleanup(void) {
    lnd_ctx.closing = true;
    lnd_ctx.initialized = false;
    lnd_modules_stop();
    lnd_renderers_free_all();
    lnd_modules_free();
    lnd_ctx.closing = false;
}

const char *LND_GetVersionName(void) {
    return "LINDAR Preview 1";
}

int32_t LND_LibraryInit(void) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    if (lnd_ctx.initialized) {
        lnd_context_unlock();
        return LND_OK;
    }
    int32_t r = lnd_modules_init();
    if (r == LND_OK) {
        lnd_ctx.initialized = true;
        r = lnd_modules_start();
    }
    if (r != LND_OK) lnd_context_cleanup();
    lnd_context_unlock();
    return lnd_error(r);
}

void LND_LibraryFree(void) {
    if (lnd_callback_active()) {
        lnd_error(LND_ERR_BUSY);
        return;
    }

    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return;
    }
    lnd_context_cleanup();
    lnd_config_reset();
    lnd_context_unlock();
}

int32_t LND_LibraryUpdate(void) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_STATE);
    }
    lnd_modules_update();
    lnd_context_unlock();
    return LND_OK;
}
