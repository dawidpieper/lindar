#include "callback.h"

#if LND_THREADS
thread_local uint32_t lnd_callback_depth;
#else
uint32_t lnd_callback_depth;
#endif
