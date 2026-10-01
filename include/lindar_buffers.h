#pragma once

#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Key "buffers.pool_max_bytes": Maximum bytes retained in the reusable PCM buffer pool. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_BUFFERS_POOL_MAX_BYTES;

/** Key "buffers.align_bytes": PCM allocation alignment in bytes (16-4096); set before LibraryInit. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_BUFFERS_ALIGN_BYTES;

/** Reference-counted interleaved PCM storage; release the caller's reference with BufferFree. */
typedef struct LND_BUFFER LND_BUFFER;

/** Allocate frames of interleaved PCM with format, channels and rate.
 *
 * @param format LND_FORMAT sample representation.
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param frames Logical buffer length in PCM frames.
 * @return An owned buffer or NULL; release with LND_BufferFree.
 */
LND_API LND_BUFFER *LND_BufferCreate(int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint64_t frames);

/** Resize/reconfigure b, allocating a replacement if shared; NULL b creates one.
 *
 * @param b Buffer reference consumed on success; NULL creates a new buffer.
 * @param format LND_FORMAT sample representation.
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param frames Logical buffer length in PCM frames.
 * @return The owned result or NULL, leaving b owned by the caller on failure.
 */
LND_API LND_BUFFER *LND_BufferReuse(LND_BUFFER *b, int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint64_t frames);

/** Release the caller's reference to b; retained source references remain valid. NULL is accepted.
 *
 * @param b PCM buffer to operate on.
 */
LND_API void LND_BufferFree(LND_BUFFER *b);

/** Get b's writable PCM data.
 *
 * @param b PCM buffer to operate on.
 * @return B's borrowed writable PCM data, or NULL; do not free it separately.
 */
LND_API void *LND_BufferGetData(const LND_BUFFER *b);

/** Get b's logical frame count.
 *
 * @param b PCM buffer to operate on.
 * @return B's logical frame count, or zero for NULL.
 */
LND_API uint64_t LND_BufferGetFrames(const LND_BUFFER *b);

/** Get b's LND_FORMAT value.
 *
 * @param b PCM buffer to operate on.
 * @return B's LND_FORMAT value, or NONE for NULL.
 */
LND_API int32_t LND_BufferGetFormat(const LND_BUFFER *b);

/** Get b's channel count.
 *
 * @param b PCM buffer to operate on.
 * @return B's channel count, or zero for NULL.
 */
LND_API uint32_t LND_BufferGetChannels(const LND_BUFFER *b);

/** Get b's sample rate in Hz.
 *
 * @param b PCM buffer to operate on.
 * @return B's sample rate in Hz, or zero for NULL.
 */
LND_API uint32_t LND_BufferGetSampleRateHz(const LND_BUFFER *b);

/** Get b's logical PCM size in bytes, excluding spare capacity.
 *
 * @param b PCM buffer to operate on.
 * @return B's logical PCM size in bytes, excluding spare capacity; zero for NULL.
 */
LND_API size_t LND_BufferGetBytes(const LND_BUFFER *b);

#ifdef __cplusplus
}
#endif
