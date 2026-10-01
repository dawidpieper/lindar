#pragma once

#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Key "log.level": Highest emitted LND_LOG level, from ERROR to DEBUG. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_LOG_LEVEL;

enum {
    LND_LOG_ERROR = 0, /**< Operation failure diagnostic. */
    LND_LOG_WARN = 1, /**< Recoverable or unexpected condition. */
    LND_LOG_INFO = 2, /**< General operation/state information. */
    LND_LOG_DEBUG = 3, /**< Detailed implementation diagnostics. */
};

/** Receive level and a message valid for this call with borrowed user; may run on library threads.
 *
 * @param user Borrowed callback context.
 * @param level LND_LOG severity level.
 * @param msg Log message borrowed for this call.
 */
typedef void (*LND_LOG_PROC)(void *user, int32_t level, const char *msg);

/** Set proc and borrowed user together; NULL proc disables callback delivery.
 *
 * @param proc Callback to install; user supplies its context.
 * @param user Borrowed callback context.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_LogSetCallback(LND_LOG_PROC proc, void *user);

#ifdef __cplusplus
}
#endif
