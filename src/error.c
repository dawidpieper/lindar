#include "error.h"
#include "lindar.h"
#include "module.h"

#if LND_THREADS
static thread_local int32_t lnd_last_error;
#else
static int32_t lnd_last_error;
#endif

int32_t lnd_error(int32_t code) {
    if (code != LND_OK) lnd_last_error = code;
    return code;
}

void *lnd_error_null(int32_t code) {
    lnd_last_error = code;
    return nullptr;
}

int32_t LND_ErrorGetLast(void) { return lnd_last_error; }

const char *LND_ErrorGetString(int32_t code) {
    switch (code) {
    case LND_OK:
        return "ok";
    case LND_ERR_INVALID_ARG:
        return "invalid argument";
    case LND_ERR_STATE:
        return "invalid state";
    case LND_ERR_EXTERNAL:
        return "external operation failed";
    case LND_ERR_OUT_OF_MEMORY:
        return "out of memory";
    case LND_ERR_UNSUPPORTED:
        return "unsupported";
    case LND_ERR_FORMAT:
        return "unsupported format";
    case LND_ERR_BUSY:
        return "busy";
    case LND_ERR_IO:
        return "I/O error";
    case LND_ERR_CYCLE:
        return "cycle detected";
    default:
        return lnd_modules_error_string(code);
    }
}
