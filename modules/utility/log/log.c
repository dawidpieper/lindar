#include "log.h"
#include "src/callback.h"
#include "src/config.h"
#include "lindar_log.h"

#include <stdarg.h>
#include <stdio.h>

void lnd_log(int32_t level, const char *fmt, ...) {
    if (!lnd_cfg_u64(LND_CFG_LOG_PROC) || level > (int32_t)lnd_cfg_u32(LND_CFG_LOG_LEVEL)) return;
    lnd_callback_config c = lnd_config_get_callback(LND_CFG_LOG_PROC, LND_CFG_LOG_USER);
    LND_LOG_PROC proc = (LND_LOG_PROC)c.proc;
    if (!proc) return;
    char msg[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof msg, fmt, args);
    va_end(args);
    lnd_callback_enter();
    proc(c.user, level, msg);
    lnd_callback_leave();
}

int32_t LND_LogSetCallback(LND_LOG_PROC proc, void *user) { return lnd_config_set_callback(LND_CFG_LOG_PROC, LND_CFG_LOG_USER, (uintptr_t)proc, user); }
