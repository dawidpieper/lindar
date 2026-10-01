#pragma once

#include "lindar_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Key "codecs.system": Allow platform-provided codecs during automatic selection (0/1).
 * Platform codecs may allocate and start threads independently of Lindar's execution mode. Format
 * support depends on the OS and installed plugins; seeking may decode forward.
 */
LND_API extern LND_CONFIG_KEY *const LND_CFG_CODECS_SYSTEM;

/** Owned text, chapter and binary metadata; Get views are borrowed and may be invalidated by mutation. */
typedef struct LND_METADATA LND_METADATA;

/** Decoder implementation descriptor; callbacks and strings must remain valid while registered or used. */
typedef struct LND_CODEC LND_CODEC;

/** Get source's decoder descriptor.
 *
 * @param source Source to operate on.
 * @return Source's borrowed decoder descriptor, or NULL if it has no codec.
 */
LND_API const LND_CODEC *LND_SourceGetCodec(const LND_SOURCE *source);

enum {
    LND_CODEC_FLAG_SYSTEM = 1u << 0, /**< Codec uses a platform-provided decoder. */
    LND_CODEC_FLAG_FALLBACK = 1u << 1, /**< Try this decoder after non-fallback candidates. */
};

/** Unsigned codec read sentinel for a decoding failure. */
#define LND_CODEC_READ_ERROR UINT64_MAX

/** Metadata allocation/count ceilings; zero fields select library defaults. */
struct LND_METADATA_LIMITS;

enum {
    LND_ENCODED_SOURCE_DIRECT = 1u << 0, /**< Decode on the pulling thread instead of a buffered worker path. */
    LND_ENCODED_SOURCE_PRELOAD = 1u << 1, /**< Decode the entire input into PCM before returning. */
    LND_ENCODED_SOURCE_LIGHTWEIGHT = 1u << 2, /**< Use the minimal source path without a graph-backed streaming adapter. */
};

enum {
    LND_ENCODED_SOURCE_READ_METADATA = 1u << 0, /**< Load metadata when creating the encoded source. */
    LND_ENCODED_SOURCE_REQUIRE_METADATA = 1u << 1, /**< Treat metadata load failure as source creation failure. */
};

/** Optional decoder selection, byte range and metadata policy; zero range length uses the remaining input.
 * Automatic selection stops on LND_ERR_OUT_OF_MEMORY or LND_ERR_BUSY.
 */
typedef struct LND_ENCODED_SOURCE_OPTIONS {
    const char *codec_name; /**< Force one decoder without fallback; NULL selects automatically. */
    uint64_t offset_bytes; /**< First encoded byte to expose to the decoder. */
    uint64_t length_bytes; /**< Byte range length; zero uses the rest of the input. */
    uint32_t block_frames; /**< Decoder block length; zero uses the module default. */
    uint32_t metadata_flags; /**< LND_ENCODED_SOURCE_READ_METADATA/REQUIRE_METADATA policy bits. */
    const struct LND_METADATA_LIMITS *metadata_limits; /**< Optional metadata allocation limits copied during creation. */
} LND_ENCODED_SOURCE_OPTIONS;

/** PCM format and length supplied by a decoder; length_known distinguishes exact empty input from unknown
 * length.
 */
typedef struct LND_CODEC_INFO {
    int32_t format; /**< LND_FORMAT sample representation. */
    uint32_t channels; /**< Number of PCM channels. */
    uint32_t sample_rate_hz; /**< PCM sample rate in Hz. */
    uint64_t length_frames; /**< Stream length in frames; check length validity separately. */
    bool seekable; /**< True if seeking is supported. */
    bool length_known; /**< True when length_frames is available, including an empty stream. */
    bool length_estimated; /**< True when the known length is approximate. */
} LND_CODEC_INFO;

/** Incremental decoder callbacks; returned PCM is borrowed until the next decoder operation. */
struct LND_CODEC_STREAM;

/** Decoder implementation descriptor; callbacks and strings must remain valid while registered or used.
 * Built-in ADPCM allocates at open, then decodes without further allocation. Opus uses libc allocation.
 * Speex seeks from the beginning, requires a stable format across Ogg chains and trims codec delay
 * as the reference decoder does. Bundled FFmpeg 9 does not provide Sonic/SonicLS.
 */
struct LND_CODEC {
    const char *name; /**< Unique decoder implementation name. */
    const char *extensions; /**< Semicolon-separated file extensions without dots. */
    uint32_t flags; /**< LND_CODEC_FLAG_SYSTEM/FALLBACK selection flags. */
    /** Inspect borrowed io.
     *
     * @param io IO handle to operate on.
     * @return Positive confidence if recognised, otherwise zero or a negative value.
     */
    int32_t (*probe)(LND_IO *io);
    /** Open borrowed io, fill info and owned state.
     *
     * @param io IO handle to operate on.
     * @param info Receives the requested snapshot.
     * @param state Receives newly allocated callback state.
     * @return LND_OK or a negative error.
     */
    int32_t (*open)(LND_IO *io, LND_CODEC_INFO *info, void **state);
    /** Read up to frames into interleaved dst.
     *
     * @param state State owned by this callback implementation.
     * @param dst Writable interleaved PCM buffer for frames times channels samples.
     * @param frames Number of PCM frames to process.
     * @return Frames, zero at EOF, or LND_CODEC_READ_ERROR.
     */
    uint64_t (*read)(void *state, void *dst, uint64_t frames);
    /** Seek state to absolute frame.
     *
     * @param state State owned by this callback implementation.
     * @param frame Absolute frame position.
     * @return LND_OK or a negative error. Optional.
     */
    int32_t (*seek)(void *state, uint64_t frame);
    /** Release decoder state; io remains owned by the caller of the codec.
     *
     * @param state State owned by this callback implementation.
     */
    void (*close)(void *state);
    const struct LND_CODEC_STREAM *stream; /**< Optional incremental decoder callback table. */
    const char *stream_identifiers; /**< Supported incremental format identifiers, distinct from name. */
};

/** Decode size bytes of borrowed data with flags and optional options; retain data until
 * LND_SourceFree.
 *
 * @param data Encoded bytes borrowed until SourceFree.
 * @param size Buffer length in bytes.
 * @param flags LND_ENCODED_SOURCE option bits.
 * @param options Settings to apply; NULL selects defaults.
 * @return Owned source or NULL.
 */
LND_API LND_SOURCE *LND_SourceCreateEncodedMemory(const void *data, size_t size, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options);

/** Decode callbacks procs with user and size using flags and optional options.
 *
 * @param procs Callback table copied during creation.
 * @param user Callback context; the input is adopted only on successful creation.
 * @param size Encoded input length in bytes, or LND_IO_SIZE_UNKNOWN.
 * @param flags LND_ENCODED_SOURCE option bits.
 * @param options Settings to apply; NULL selects defaults.
 * @return Owned source or NULL; LND_SourceFree closes a successfully adopted input.
 */
LND_API LND_SOURCE *LND_SourceCreateEncodedInput(const LND_IO_INPUT_PROCS *procs, void *user, uint64_t size, uint32_t flags, const LND_ENCODED_SOURCE_OPTIONS *options);

/** Register caller-owned codec; retain its descriptor while registered and in use.
 *
 * @param codec Borrowed decoder implementation descriptor.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_CodecRegister(const LND_CODEC *codec);

/** Remove codec from future selection.
 *
 * @param codec Borrowed decoder implementation descriptor.
 * @return LND_OK or a negative error; existing users still need the descriptor.
 */
LND_API int32_t LND_CodecUnregister(const LND_CODEC *codec);

/** Get the number of registered decoder implementations.
 *
 * @return The number of registered decoder implementations.
 */
LND_API uint32_t LND_CodecGetCount(void);

/** Get a codec at zero-based index.
 *
 * @param index Zero-based entry index.
 * @return A borrowed codec at zero-based index, or NULL if out of range.
 */
LND_API const LND_CODEC *LND_CodecGet(uint32_t index);

/** Get the decoder implementation named name.
 *
 * @param name Implementation name.
 * @return The borrowed decoder implementation named name, or NULL if absent.
 */
LND_API const LND_CODEC *LND_CodecFind(const char *name);

/** Get codec's implementation name.
 *
 * @param codec Borrowed decoder implementation descriptor.
 * @return Codec's borrowed implementation name, or NULL for NULL.
 */
LND_API const char *LND_CodecGetName(const LND_CODEC *codec);

/** Get codec's extension list.
 *
 * @param codec Borrowed decoder implementation descriptor.
 * @return Codec's borrowed extension list, or NULL if absent.
 */
LND_API const char *LND_CodecGetExtensions(const LND_CODEC *codec);

/** Get codec's LND_CODEC capability flags.
 *
 * @param codec Borrowed decoder implementation descriptor.
 * @return Codec's LND_CODEC capability flags, or zero for NULL.
 */
LND_API uint32_t LND_CodecGetFlags(const LND_CODEC *codec);

/** Get codec's incremental format identifier list.
 *
 * @param codec Borrowed decoder implementation descriptor.
 * @return Codec's borrowed incremental format identifier list, or NULL if absent.
 */
LND_API const char *LND_CodecGetStreamIdentifiers(const LND_CODEC *codec);

/** Check support for incremental decoding of a format.
 *
 * @param codec Borrowed decoder implementation descriptor.
 * @param identifier Incremental codec format identifier.
 * @return True if codec supports incremental decoding of identifier; false otherwise.
 */
LND_API bool LND_CodecSupportsStream(const LND_CODEC *codec, const char *identifier);

/** Incremental decoder configuration for a format identifier; configuration data is borrowed during the
 * call.
 */
typedef struct LND_CODEC_STREAM_CONFIG {
    const char *codec; /**< Format identifier, such as a container's codec name; not an implementation name. */
    const uint8_t *data; /**< Borrowed codec configuration bytes. */
    size_t bytes; /**< Length of data in bytes. */
    uint32_t sample_rate_hz; /**< PCM sample rate in Hz. */
    uint32_t channels; /**< Number of PCM channels. */
    bool elementary; /**< Input packets contain an elementary stream rather than a container. */
    bool trim_known; /**< trim_start_frames is explicitly known. */
    uint32_t trim_start_frames; /**< Leading decoded frames to discard. */
} LND_CODEC_STREAM_CONFIG;

enum {
    LND_CODEC_STREAM_ASYNC = 1u << 0 /**< Decoder may make progress asynchronously without new input bytes. */
};

/** Incremental decoder callbacks; returned PCM is borrowed until the next decoder operation. */
typedef struct LND_CODEC_STREAM {
    /** Inspect bytes of data.
     *
     * @param data Input bytes borrowed for the operation.
     * @param bytes Buffer length in bytes.
     * @return True if recognised.
     */
    bool (*probe)(const uint8_t *data, size_t bytes);
    /** Allocate decoder state.
     *
     * @return Owned state or NULL.
     */
    void *(*create)(void);
    /** Consume data, report used bytes and borrowed PCM/info; end marks final input.
     *
     * @param state State owned by this callback implementation.
     * @param data Input bytes borrowed for the operation.
     * @param bytes Buffer length in bytes.
     * @param end True when this is the final input.
     * @param used Receives the number of input bytes consumed.
     * @param pcm Receives a borrowed PCM view valid until the next decoder operation.
     * @param info Receives the current decoded format and length.
     * @return LND_SOURCE_READY, LND_SOURCE_WAITING, LND_SOURCE_EOF or a negative error.
     */
    int32_t (*step)(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info);
    /** Release state and its buffered PCM.
     *
     * @param state State owned by this callback implementation.
     */
    void (*close)(void *state);
    /** Apply borrowed config to state.
     *
     * @param state State owned by this callback implementation.
     * @param config Required settings, borrowed during the call.
     * @return LND_OK or a negative error. Optional.
     */
    int32_t (*configure)(void *state, const LND_CODEC_STREAM_CONFIG *config);
    /** Copy state metadata and revision into caller outputs.
     *
     * @param state State owned by this callback implementation.
     * @param metadata Existing object to receive a copy of the decoder metadata.
     * @param revision Receives the metadata revision.
     * @return LND_OK or a negative error. Optional.
     */
    int32_t (*metadata)(void *state, LND_METADATA *metadata, uint64_t *revision);
    uint32_t flags; /**< LND_CODEC_STREAM_ASYNC or zero. */
} LND_CODEC_STREAM;

#ifdef __cplusplus
}
#endif
