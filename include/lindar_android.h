#pragma once

#include "lindar_aaudio.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LND_ANDROID_RECORDING_DEFAULT,             /**< Keep the AAudio input default. */
    LND_ANDROID_RECORDING_MICROPHONE,          /**< General microphone recording. */
    LND_ANDROID_RECORDING_CAMCORDER,           /**< Recording alongside video. */
    LND_ANDROID_RECORDING_VOICE_RECOGNITION,   /**< Speech recognition with minimal processing. */
    LND_ANDROID_RECORDING_VOICE_COMMUNICATION, /**< Voice calls with platform speech processing. */
    LND_ANDROID_RECORDING_UNPROCESSED,         /**< Request input without platform processing; hardware support varies. */
    LND_ANDROID_RECORDING_VOICE_PERFORMANCE    /**< Live vocal performance; requires Android API 29. */
};

/** Key "android.recording_profile": LND_ANDROID_RECORDING profile for subsequently opened inputs.
 * Zero keeps the system default. Explicit profiles require API 28, or API 29 for voice performance.
 * Existing streams keep their profile until reopened; output streams are unaffected. Unsupported
 * profiles fail when opening input rather than falling back. The application handles RECORD_AUDIO permission.
 */
LND_API extern LND_CONFIG_KEY *const LND_CFG_ANDROID_RECORDING_PROFILE;

#ifdef __cplusplus
}
#endif
