#pragma once
#include "lindar_metadata.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Parse size bytes of ID3v2 data into metadata and optionally write consumed bytes.
 * Reads v2.2, v2.3 and v2.4; CRCs are not verified. Chapter times have millisecond precision.
 *
 * @param metadata Metadata object to read or update.
 * @param data Input bytes borrowed for the operation.
 * @param size Buffer length in bytes.
 * @param consumed Optional output for the number of input bytes consumed; may be NULL.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataId3v2Read(LND_METADATA *metadata, const void *data, size_t size, size_t *consumed);

/** Serialise metadata as ID3v2 version with flags; write owned data and byte size.
 * Does not emit CRCs or edit CTOC; chapter times have millisecond precision.
 *
 * @param metadata Metadata object to read or update.
 * @param version ID3v2 major version, 3 or 4; 0 preserves v2.3 input or selects v2.4.
 * @param flags LND_METADATA_DROP_UNSUPPORTED or 0.
 * @param data Receives an owned buffer; release it with LND_MetadataBufferFree.
 * @param size Receives the buffer length in bytes.
 * @return LND_OK or a negative error; use LND_MetadataBufferFree.
 */
LND_API int32_t LND_MetadataId3v2CreateBuffer(const LND_METADATA *metadata, uint32_t version, uint32_t flags, void **data, size_t *size);
#ifdef __cplusplus
}
#endif
