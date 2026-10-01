#pragma once

#include "lindar_codecs.h"
#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Incremental decoder driven by Feed/Step/ReadPcm or adopted IO; release with DecoderFree. */
typedef struct LND_DECODER LND_DECODER;

/** Incremental decoder settings; NULL options or zero sizes select defaults. */
typedef struct LND_DECODER_OPTIONS {
    const char *codec_name; /**< Force one decoder without fallback; NULL selects automatically. */
    size_t input_bytes; /**< Maximum compressed input queue bytes; zero uses the default. */
    uint32_t block_frames; /**< Decoded PCM block capacity; zero uses the default. */
    bool allow_buffered; /**< Permit whole-input buffering for codecs without incremental support. */
} LND_DECODER_OPTIONS;

/** Create an incremental decoder using optional options.
 *
 * @param options Settings to apply; NULL selects defaults.
 * @return Owned decoder or NULL; release with LND_DecoderFree.
 */
LND_API LND_DECODER *LND_DecoderCreate(const LND_DECODER_OPTIONS *options);

/** Attach io to decoder, transferring ownership only on success.
 *
 * @param decoder Incremental decoder to operate on.
 * @param io IO handle whose ownership transfers only on success.
 * @return LND_OK or a negative error; do not feed attached input manually.
 */
LND_API int32_t LND_DecoderTakeIo(LND_DECODER *decoder, LND_IO *io);

/** Copy up to bytes from data into decoder's input queue.
 *
 * @param decoder Incremental decoder to operate on.
 * @param data Input bytes borrowed for the operation.
 * @param bytes Buffer length in bytes.
 * @return Bytes accepted or a negative error; drain PCM before retrying a short write.
 */
LND_API int64_t LND_DecoderFeed(LND_DECODER *decoder, const void *data, size_t bytes);

/** Mark decoder input complete and allow remaining PCM to drain.
 *
 * @param decoder Incremental decoder to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DecoderEnd(LND_DECODER *decoder);

/** Copy decoder's current PCM format and length into info.
 *
 * @param decoder Incremental decoder to operate on.
 * @param info Receives the requested snapshot.
 * @return LND_OK, LND_SOURCE_WAITING before detection, or a negative error.
 */
LND_API int32_t LND_DecoderGetInfo(const LND_DECODER *decoder, LND_CODEC_INFO *info);

/** Get decoder's selected codec.
 *
 * @param decoder Incremental decoder to operate on.
 * @return Decoder's borrowed selected codec, or NULL before detection.
 */
LND_API const LND_CODEC *LND_DecoderGetCodec(const LND_DECODER *decoder);

/** Get decoder's READY, WAITING or EOF state.
 *
 * @param decoder Incremental decoder to operate on.
 * @return Decoder's READY, WAITING or EOF state, or a negative error.
 */
LND_API int32_t LND_DecoderGetStatus(const LND_DECODER *decoder);

/** Get compressed bytes currently queued in decoder.
 *
 * @param decoder Incremental decoder to operate on.
 * @return Compressed bytes currently queued in decoder.
 */
LND_API size_t LND_DecoderGetBufferedBytes(const LND_DECODER *decoder);

/** Advance decoder probing/decoding without reading PCM.
 * The ffmpeg_stream implementation uses a demux worker for Matroska/WebM/ASF even when stepped manually.
 *
 * @param decoder Incremental decoder to operate on.
 * @return LND_SOURCE_READY, LND_SOURCE_WAITING, LND_SOURCE_EOF or a negative error.
 */
LND_API int32_t LND_DecoderStep(LND_DECODER *decoder);

/** Read up to frames into pcm at offset_frames.
 *
 * @param decoder Incremental decoder to operate on.
 * @param pcm Destination PCM descriptor with writable sample storage.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @return Decoded frames or a negative error; GetStatus distinguishes waiting from EOF.
 */
LND_API int64_t LND_DecoderReadPcm(LND_DECODER *decoder, const LND_PCM *pcm, size_t offset_frames, size_t frames);

/** Load or refresh decoder's cached metadata, possibly reading input.
 *
 * @param decoder Incremental decoder to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DecoderLoadMetadata(LND_DECODER *decoder);

/** Copy decoder's loaded metadata into an existing metadata object.
 *
 * @param decoder Incremental decoder to operate on.
 * @param metadata Existing object to receive a copy of the metadata.
 * @return LND_OK or a negative error; call LoadMetadata first.
 */
LND_API int32_t LND_DecoderCopyMetadata(const LND_DECODER *decoder, LND_METADATA *metadata);

/** Get decoder's cached metadata revision without I/O.
 *
 * @param decoder Incremental decoder to operate on.
 * @return Decoder's cached metadata revision without I/O; zero before a successful load.
 */
LND_API uint64_t LND_DecoderGetMetadataRevision(const LND_DECODER *decoder);

/** Close decoder, its adopted IO and its decode state; NULL is accepted.
 *
 * @param decoder Incremental decoder to operate on.
 */
LND_API void LND_DecoderFree(LND_DECODER *decoder);

#ifdef __cplusplus
}
#endif
