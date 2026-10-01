#pragma once
#include "lindar_metadata.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Parse size bytes of an ID3v1 tag into metadata.
 *
 * @param metadata Metadata object to read or update.
 * @param data Input bytes borrowed for the operation.
 * @param size Buffer length in bytes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataId3v1Read(LND_METADATA *metadata, const void *data, size_t size);

/** Serialise metadata as ID3v1 with flags; write owned data and byte size.
 *
 * @param metadata Metadata object to read or update.
 * @param flags LND_METADATA_DROP_UNSUPPORTED or 0.
 * @param data Receives an owned buffer; release it with LND_MetadataBufferFree.
 * @param size Receives the buffer length in bytes.
 * @return LND_OK or a negative error; use LND_MetadataBufferFree.
 */
LND_API int32_t LND_MetadataId3v1CreateBuffer(const LND_METADATA *metadata, uint32_t flags, void **data, size_t *size);
#ifdef __cplusplus
}
#endif
