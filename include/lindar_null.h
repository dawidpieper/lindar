#pragma once

#include "lindar_devices.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Key "null.realtime": Pace the null device against elapsed time (0/1). */
LND_API extern LND_CONFIG_KEY *const LND_CFG_NULL_REALTIME;

/** Fill frames in data for user's null input using its negotiated format/channels.
 *
 * @param user Borrowed callback context.
 * @param data Writable interleaved buffer for frames in the negotiated format.
 * @param frames Number of PCM frames to process.
 */
typedef void (*LND_NULL_SOURCE_PROC)(void *user, void *data, uint64_t frames);

/** Consume frames from data for user's null output using its negotiated format/channels; data is
 * borrowed for this call.
 *
 * @param user Borrowed callback context.
 * @param data Borrowed interleaved PCM in the negotiated format.
 * @param frames Number of PCM frames to process.
 */
typedef void (*LND_NULL_SINK_PROC)(void *user, const void *data, uint64_t frames);

/** Set proc and borrowed user for future null output streams; NULL disables it.
 *
 * @param proc Callback to install; user supplies its context.
 * @param user Borrowed callback context.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NullSetOutputCallback(LND_NULL_SINK_PROC proc, void *user);

/** Set proc and borrowed user for future null input streams; NULL supplies silence.
 *
 * @param proc Callback to install; user supplies its context.
 * @param user Borrowed callback context.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NullSetInputCallback(LND_NULL_SOURCE_PROC proc, void *user);

#ifdef __cplusplus
}
#endif
