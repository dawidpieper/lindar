#pragma once

#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Get storage bytes for channels, rate and capacity_frames.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param capacity_frames Queue capacity in frames.
 * @return Storage bytes for channels, rate and capacity_frames, or zero if invalid.
 */
LND_API size_t LND_QueueGetMemoryBytes(uint32_t channels, uint32_t sample_rate_hz, uint32_t capacity_frames);

/** Initialise a PCM queue in bytes of caller storage aligned to max_align_t.
 *
 * @param memory Caller-owned storage aligned to max_align_t.
 * @param bytes Available size of memory in bytes.
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param capacity_frames Queue capacity in frames.
 * @return Its source or NULL; LND_SourceFree preserves the storage.
 */
LND_API LND_SOURCE *LND_QueueInit(void *memory, size_t bytes, uint32_t channels, uint32_t sample_rate_hz, uint32_t capacity_frames);

/** Allocate a live PCM queue with channels, rate and capacity_frames.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param capacity_frames Queue capacity in frames.
 * @return An owned source or NULL; release with LND_SourceFree.
 */
LND_API LND_SOURCE *LND_SourceCreateQueue(uint32_t channels, uint32_t sample_rate_hz, uint32_t capacity_frames);

/** Append up to frames from pcm at offset_frames to source's queue.
 *
 * @param source Source to operate on.
 * @param pcm PCM descriptor; sample storage remains borrowed.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @return Accepted frames or a negative error; retry any remainder.
 */
LND_API int64_t LND_QueueWritePcm(LND_SOURCE *source, const LND_PCM *pcm, size_t offset_frames, size_t frames);

/** Get unread frames in source's queue.
 *
 * @param source Source to operate on.
 * @return Unread frames in source's queue, or zero if unavailable.
 */
LND_API uint32_t LND_QueueGetBufferedFrames(const LND_SOURCE *source);

/** Get source's queue capacity in frames.
 *
 * @param source Source to operate on.
 * @return Source's queue capacity in frames, or zero if unavailable.
 */
LND_API uint32_t LND_QueueGetCapacityFrames(const LND_SOURCE *source);

/** Drop up to frames from source's queue.
 *
 * @param source Source to operate on.
 * @param frames Number of PCM frames to process.
 * @return Frames discarded or a negative error.
 */
LND_API int64_t LND_QueueDiscardFrames(LND_SOURCE *source, size_t frames);

/** Empty source's queue and clear its end state for reuse.
 *
 * @param source Source to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_QueueReset(LND_SOURCE *source);

#ifdef __cplusplus
}
#endif
