#pragma once

#include "atomic.h"
#include "callback.h"
#include "platform.h"
#include "thread.h"

typedef struct lnd_context {
    bool initialized;
    bool closing;
    lnd_mutex mutex;
} lnd_context;

extern lnd_context lnd_ctx;

void lnd_context_lock(void);
void lnd_context_unlock(void);

bool lnd_context_enter(void);
