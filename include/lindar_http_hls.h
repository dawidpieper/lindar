#pragma once

#include <stdbool.h>
#include <stdint.h>

/** HLS variant, alternate-audio language and latency preferences; language is copied when opening HTTP.
 * Supports live playlists, low-latency parts/delta updates and AES-128; SAMPLE-AES and DRM are unsupported.
 */
typedef struct LND_HTTP_HLS_OPTIONS {
    const char *preferred_language; /**< Optional preferred audio rendition language. */
    uint64_t max_bitrate_bps; /**< Variant bitrate ceiling in bits per second; zero removes it. */
    uint32_t live_delay_ms; /**< Target delay behind the live edge in milliseconds. */
    bool adaptive; /**< Allow automatic variant changes based on throughput. */
    bool low_latency; /**< Enable low-latency HLS behaviour where supported. */
} LND_HTTP_HLS_OPTIONS;
