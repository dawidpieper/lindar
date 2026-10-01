#pragma once

#include "platform.h"

#if LND_THREADS
extern thread_local uint32_t lnd_callback_depth;
#else
extern uint32_t lnd_callback_depth;
#endif

LND_INLINE void lnd_callback_enter(void) { lnd_callback_depth++; }
LND_INLINE void lnd_callback_leave(void) { lnd_callback_depth--; }
LND_INLINE bool lnd_callback_active(void) { return lnd_callback_depth != 0; }
