#pragma once

#include <stdint.h>

/** Bounded compressed-data cache for finite responses; endless streams are unsupported.
 * The directory is copied when opening HTTP.
 */
typedef struct LND_HTTP_FILE_OPTIONS {
    const char *directory; /**< Temporary cache directory; NULL uses the system default. Supply the app cache path on Android. */
    uint64_t max_bytes; /**< Cache size limit in bytes; zero disables file caching. */
} LND_HTTP_FILE_OPTIONS;
