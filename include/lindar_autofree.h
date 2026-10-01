#pragma once

#include "lindar_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Nonzero playback ID owning a source until completion or cancellation; zero denotes failure. */
typedef uint64_t LND_AUTOFREE;

/** Copied playback snapshot for a currently valid autofree ID. */
typedef struct LND_AUTOFREE_INFO {
    int32_t state; /**< LND_SOUND playback state. */
    int32_t source_status; /**< Source READY, WAITING, EOF or negative error. */
    uint64_t position_frames; /**< Current position in PCM frames. */
    uint64_t length_frames; /**< Stream length in frames; check length validity separately. */
    bool draining; /**< Source ended; downstream audio is still draining. */
} LND_AUTOFREE_INFO;

/** Start source on output, or the default for NULL, and free it after playback drains.
 *
 * @param source Source whose ownership transfers only on success.
 * @param output Destination node; NULL selects the default output.
 * @return Nonzero ID and take ownership only on success; zero leaves source owned by caller.
 */
LND_API LND_AUTOFREE LND_AutofreeTakeSource(LND_SOURCE *source, LND_NODE *output);

/** Copy playback state for id into info.
 *
 * @param id Autofree playback identifier.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error if id has expired.
 */
LND_API int32_t LND_AutofreeGetInfo(LND_AUTOFREE id, LND_AUTOFREE_INFO *info);

/** Check whether id still identifies an active autofree entry.
 *
 * @param id Autofree playback identifier.
 * @return True if id still identifies an active autofree entry; false otherwise.
 */
LND_API bool LND_AutofreeIsValid(LND_AUTOFREE id);

/** Stop id and release its owned source.
 *
 * @param id Autofree playback identifier.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_AutofreeCancel(LND_AUTOFREE id);

/** Pause or resume playback for id.
 *
 * @param id Autofree playback identifier.
 * @param pause True to pause; false to resume.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_AutofreeSetPause(LND_AUTOFREE id, bool pause);

/** Set id's linear gain; 1 is unity.
 *
 * @param id Autofree playback identifier.
 * @param gain Linear gain; 1 is unity and 0 is silence.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_AutofreeSetGain(LND_AUTOFREE id, float gain);

/** Enable or disable looping for id.
 *
 * @param id Autofree playback identifier.
 * @param loop True to loop at end of input; false to stop.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_AutofreeSetLoop(LND_AUTOFREE id, bool loop);

/** Seek id's source to absolute frame.
 *
 * @param id Autofree playback identifier.
 * @param frame Absolute frame position.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_AutofreeSeekFrames(LND_AUTOFREE id, uint64_t frame);

/** Mark id's live input complete and allow buffered playback to drain.
 *
 * @param id Autofree playback identifier.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_AutofreeEnd(LND_AUTOFREE id);

/** Append frames from pcm at offset_frames to id's queue.
 *
 * @param id Autofree playback identifier.
 * @param pcm PCM descriptor; sample storage remains borrowed.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @return Accepted frames or a negative error.
 */
LND_API int64_t LND_AutofreeWritePcm(LND_AUTOFREE id, const LND_PCM *pcm, size_t offset_frames, size_t frames);

#ifdef __cplusplus
}
#endif
