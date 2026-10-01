#pragma once

#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Get sound's playback position in seconds, derived from source frames.
 *
 * @param s Sound to operate on.
 * @return Sound's playback position in seconds, derived from source frames.
 */
LND_API double LND_SoundGetPositionSeconds(const LND_SOUND *s);

/** Seek sound to absolute sec.
 *
 * @param s Sound to operate on.
 * @param sec Absolute source position in seconds.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundSeekSeconds(LND_SOUND *s, double sec);

/** Get sound's length in seconds.
 *
 * @param s Sound to operate on.
 * @return Sound's length in seconds; inspect LND_SourceGetInfo for length validity.
 */
LND_API double LND_SoundGetLengthSeconds(const LND_SOUND *s);

/** Set sound's linear gain; 1 is unity and 0 is silence.
 *
 * @param s Sound to operate on.
 * @param gain Linear gain; 1 is unity and 0 is silence.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundSetGain(LND_SOUND *s, float gain);

/** Get sound's linear gain.
 *
 * @param s Sound to operate on.
 * @return Sound's linear gain; 1 is unity.
 */
LND_API float LND_SoundGetGain(const LND_SOUND *s);

/** Render up to frames from sound to interleaved dst.
 *
 * @param s Sound to operate on.
 * @param dst Writable interleaved buffer for frames times output channels floats.
 * @param frames Number of PCM frames to process.
 * @return Frames read or a negative error; dst holds frames times output channels floats.
 */
LND_API int64_t LND_SoundReadF32(LND_SOUND *s, float *dst, uint64_t frames);

#ifdef __cplusplus
}
#endif
