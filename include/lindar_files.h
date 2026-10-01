#pragma once

#include "lindar.h"
#include "lindar_codecs.h"
#include "lindar_output.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Open wide-character path with source flags and optional decoder options.
 *
 * @param path Wide-character file path.
 * @param flags LND_ENCODED_SOURCE option bits.
 * @param options Settings to apply; NULL selects defaults.
 * @return An owned source or NULL; release with LND_SourceFree.
 */
LND_API LND_SOURCE *LND_SourceCreateFileWide(const wchar_t *path, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options);

/** Open UTF-8 path with source flags and optional decoder options.
 *
 * @param path UTF-8 file path.
 * @param flags LND_ENCODED_SOURCE option bits.
 * @param options Settings to apply; NULL selects defaults.
 * @return An owned source or NULL; release with LND_SourceFree.
 */
LND_API LND_SOURCE *LND_SourceCreateFile(const char *path, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options);

/** Create/truncate wide-character path using params.
 *
 * @param path Wide-character file path.
 * @param params Encoder settings; referenced metadata is copied on creation.
 * @return Owned output or NULL; release with LND_OutputFree.
 */
LND_API LND_OUTPUT *LND_OutputCreateFileWide(const wchar_t *path, const LND_ENCODER_PARAMS *params);

/** Create/truncate UTF-8 path using params; extension helps select the encoder.
 *
 * @param path UTF-8 file path.
 * @param params Encoder settings; referenced metadata is copied on creation.
 * @return Owned output or NULL; release with LND_OutputFree.
 */
LND_API LND_OUTPUT *LND_OutputCreateFile(const char *path, const LND_ENCODER_PARAMS *params);

/** Render s to UTF-8 path in format, up to max_frames; zero uses known length.
 *
 * @param s Source to operate on.
 * @param path UTF-8 file path.
 * @param format LND_FORMAT sample representation.
 * @param max_frames Maximum number of PCM frames to process.
 * @return Frames written or a negative error; live input needs a limit.
 */
LND_API int64_t LND_SourceRenderFile(LND_SOURCE *s, const char *path, int32_t format, uint64_t max_frames);

#ifdef __cplusplus
}
#endif
