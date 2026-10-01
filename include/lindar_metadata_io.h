#pragma once
#include "lindar_metadata.h"
#include "lindar_io.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Target tag format, version and conversion policy; zero version selects the writer default. */
typedef struct LND_METADATA_WRITE_OPTIONS {
    int32_t format; /**< Target LND_METADATA format; AUTO selects from input. */
    uint32_t version; /**< Target format version; zero uses the writer default. */
    uint32_t flags; /**< LND_METADATA_DROP_UNSUPPORTED or zero. */
} LND_METADATA_WRITE_OPTIONS;

/** Check metadata reader support.
 *
 * @param format LND_METADATA tag format selector.
 * @return True if this build can parse the specified metadata format; false otherwise.
 */
LND_API bool LND_MetadataFormatCanRead(int32_t format);

/** Check metadata writer support.
 *
 * @param format LND_METADATA tag format selector.
 * @return True if this build can write the specified metadata format; false otherwise.
 */
LND_API bool LND_MetadataFormatCanWrite(int32_t format);

/** Read format, or AUTO-detect, from borrowed input into metadata.
 *
 * @param metadata Metadata object to read or update.
 * @param input IO handle to operate on.
 * @param format LND_METADATA tag format; LND_METADATA_AUTO enables detection.
 * @return LND_OK or a negative error; caller retains input.
 */
LND_API int32_t LND_MetadataReadIo(LND_METADATA *metadata, LND_IO *input, int32_t format);

/** Parse size bytes of data in format, or AUTO-detect, into metadata.
 *
 * @param metadata Metadata object to read or update.
 * @param data Input bytes borrowed for the operation.
 * @param size Buffer length in bytes.
 * @param format LND_METADATA tag format; LND_METADATA_AUTO enables detection.
 * @return LND_OK or a negative error; data is only borrowed during the call.
 */
LND_API int32_t LND_MetadataReadMemory(LND_METADATA *metadata, const void *data, size_t size, int32_t format);

/** Rewrite borrowed input to borrowed output with metadata and optional options.
 * Supports ID3, Ogg Opus and WAVE/RF64; Vorbis/FLAC whole-file rewriting is unsupported.
 * WAVE requires a complete RIFF structure. RF64 ds64 table entries and automatic RIFF-to-RF64
 * conversion are unsupported. Unpreservable content fails unless LND_METADATA_DROP_UNSUPPORTED is set.
 *
 * @param metadata Metadata object to read or update.
 * @param input Seekable input of known size, borrowed during the call.
 * @param output Distinct empty output, borrowed during the call.
 * @param options Settings to apply; NULL selects defaults.
 * @return LND_OK or a negative error; caller closes both IO objects.
 */
LND_API int32_t LND_MetadataWriteIo(const LND_METADATA *metadata, LND_IO *input, LND_IO *output, const LND_METADATA_WRITE_OPTIONS *options);
#ifdef __cplusplus
}
#endif
