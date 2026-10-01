#pragma once
#include "lindar_metadata.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Parse metadata from size bytes of wave data.
 *
 * @param metadata Metadata object to read or update.
 * @param wave Input bytes borrowed during the call.
 * @param size Buffer length in bytes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataWaveRead(LND_METADATA *metadata, const void *wave, size_t size);

/** Serialise metadata to WAV chunks at sample_rate_hz with flags; write owned chunks and byte size.
 * Produces metadata chunks only, not a complete audio file.
 *
 * @param metadata Metadata object to read or update.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param flags LND_METADATA_DROP_UNSUPPORTED or 0.
 * @param chunks Receives an owned buffer; release it with LND_MetadataBufferFree.
 * @param size Receives the buffer length in bytes.
 * @return LND_OK or a negative error; use LND_MetadataBufferFree.
 */
LND_API int32_t LND_MetadataWaveCreateBuffer(const LND_METADATA *metadata, uint32_t sample_rate_hz, uint32_t flags, void **chunks, size_t *size);
#ifdef __cplusplus
}
#endif
