#pragma once

#include "src/platform.h"

#if LND_MODULE_LOG
#include "lindar_log.h"
void lnd_log(int32_t level, const char *fmt, ...);

#define LND_LOG_E(...) lnd_log(LND_LOG_ERROR, __VA_ARGS__)
#define LND_LOG_W(...) lnd_log(LND_LOG_WARN, __VA_ARGS__)
#define LND_LOG_I(...) lnd_log(LND_LOG_INFO, __VA_ARGS__)
#define LND_LOG_D(...) lnd_log(LND_LOG_DEBUG, __VA_ARGS__)

#else
#define LND_LOG_E(...) ((void)0)
#define LND_LOG_W(...) ((void)0)
#define LND_LOG_I(...) ((void)0)
#define LND_LOG_D(...) ((void)0)
#endif
