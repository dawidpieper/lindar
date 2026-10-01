#pragma once
#include "lindar_metadata.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Parse size bytes of a Vorbis/FLAC-style comment packet in format into metadata.
 *
 * @param metadata Metadata object to read or update.
 * @param packet Input bytes borrowed during the call.
 * @param size Buffer length in bytes.
 * @param format LND_METADATA_VORBIS or LND_METADATA_FLAC.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataCommentsRead(LND_METADATA *metadata, const void *packet, size_t size, int32_t format);

/** Serialise metadata in comment format with flags; write owned packet and byte size.
 * Writes a comment packet only; whole-file Vorbis/FLAC rewriting is unsupported.
 *
 * @param metadata Metadata object to read or update.
 * @param format LND_METADATA_VORBIS or LND_METADATA_FLAC.
 * @param flags LND_METADATA_DROP_UNSUPPORTED or 0.
 * @param packet Receives an owned buffer; release it with LND_MetadataBufferFree.
 * @param size Receives the buffer length in bytes.
 * @return LND_OK or a negative error; use LND_MetadataBufferFree.
 */
LND_API int32_t LND_MetadataCommentsCreateBuffer(const LND_METADATA *metadata, int32_t format, uint32_t flags, void **packet, size_t *size);

/** Parse size bytes of an OpusTags packet into metadata.
 *
 * @param metadata Metadata object to read or update.
 * @param packet Input bytes borrowed during the call.
 * @param size Buffer length in bytes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataOpusRead(LND_METADATA *metadata, const void *packet, size_t size);

/** Serialise metadata as OpusTags with flags; write owned packet and byte size.
 *
 * @param metadata Metadata object to read or update.
 * @param flags LND_METADATA_DROP_UNSUPPORTED or 0.
 * @param packet Receives an owned buffer; release it with LND_MetadataBufferFree.
 * @param size Receives the buffer length in bytes.
 * @return LND_OK or a negative error; use LND_MetadataBufferFree.
 */
LND_API int32_t LND_MetadataOpusCreateBuffer(const LND_METADATA *metadata, uint32_t flags, void **packet, size_t *size);

#ifdef __cplusplus
}
#endif
