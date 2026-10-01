#pragma once

#include "lindar_io.h"
#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Owned text, chapter and binary metadata; Get views are borrowed and may be invalidated by mutation. */
typedef struct LND_METADATA LND_METADATA;

/** Encoder implementation descriptor; callbacks and strings remain valid while registered or used. */
typedef struct LND_ENCODER LND_ENCODER;

/** Encoder and its destination; OutputFinish finalises, OutputFree closes and releases it. */
typedef struct LND_OUTPUT LND_OUTPUT;

enum {
    LND_ENCODER_MODE_DEFAULT = 0, /**< Use the encoder's default rate control. */
    LND_ENCODER_MODE_CBR = 1, /**< Request constant bitrate encoding. */
    LND_ENCODER_MODE_VBR = 2, /**< Request variable bitrate encoding. */
    LND_ENCODER_MODE_ABR = 3, /**< Request average bitrate encoding. */
};

enum {
    LND_OUTPUT_STREAMING = 1u << 0, /**< Request an output suitable for a non-seekable stream. */
};

enum {
    LND_ENCODER_FLAG_SYSTEM = 1u << 0, /**< Encoder uses a platform-provided implementation. */
    LND_ENCODER_FLAG_SEEK = 1u << 1, /**< Requires a seekable destination, e.g. ADPCM header finalisation. */
    LND_ENCODER_FLAG_FALLBACK = 1u << 2, /**< Try this encoder after non-fallback candidates. */
    LND_ENCODER_FLAG_FRAME_SIZE = 1u << 3, /**< Encoder accepts an explicit frame_size_frames setting. */
    LND_ENCODER_FLAG_METADATA = 1u << 4, /**< Encoder supports embedded metadata. */
};

/** Encoder selection and PCM settings; format NONE lets the encoder choose its output representation. */
typedef struct LND_ENCODER_PARAMS {
    const char *encoder_name; /**< Encoder implementation name; NULL selects by output extension/format. */
    uint32_t channels; /**< Number of PCM channels. */
    uint32_t sample_rate_hz; /**< Input PCM rate; Opus resamples rates other than 8/12/16/24/48 kHz to 48 kHz. */
    int32_t format; /**< LND_FORMAT sample representation. */
    uint32_t bitrate_kbps; /**< Target encoded bitrate in kilobits per second; zero uses encoder defaults. */
    uint32_t quality; /**< Quality from 1 to 100 mapped by the encoder; zero uses its default. */
    int32_t mode; /**< LND_ENCODER_MODE_DEFAULT, CBR, VBR or ABR. */
    uint32_t flags; /**< LND_OUTPUT_STREAMING or zero. */
    const char *options; /**< Optional encoder-specific key=value entries separated by semicolons or commas. */
    uint32_t frame_size_frames; /**< Codec frame size at the encoder rate, after any resampling; zero uses its default. */
    const LND_METADATA *metadata; /**< Optional metadata copied into the output. */
    uint32_t metadata_flags; /**< LND_METADATA_DROP_UNSUPPORTED or zero. */
} LND_ENCODER_PARAMS;

/** Encoder implementation descriptor; callbacks and strings remain valid while registered or used. */
struct LND_ENCODER {
    const char *name; /**< Unique encoder implementation name. */
    const char *extensions; /**< Semicolon-separated file extensions without dots. */
    uint32_t flags; /**< LND_ENCODER capability/selection bits. */
    /** Open borrowed io using params/extension and write owned state.
     *
     * @param io IO handle to operate on.
     * @param params Encoder settings; referenced metadata is copied on creation.
     * @param extension Format extension without a leading dot.
     * @param state Receives newly allocated callback state.
     * @return LND_OK or a negative error.
     */
    int32_t (*open)(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state);
    /** Encode frames of interleaved F32 pcm with state.
     *
     * @param state State owned by this callback implementation.
     * @param pcm Borrowed interleaved F32 samples for frames.
     * @param frames Number of PCM frames to process.
     * @return LND_OK or a negative error.
     */
    int32_t (*write)(void *state, const float *pcm, uint64_t frames);
    /** Flush state without ending input.
     *
     * @param state State owned by this callback implementation.
     * @return LND_OK or a negative error. Optional.
     */
    int32_t (*flush)(void *state);
    /** Finalise and release state.
     *
     * @param state State owned by this callback implementation.
     * @return LND_OK or a negative error, leaving io to its owner.
     */
    int32_t (*close)(void *state);
};

/** Register caller-owned encoder; retain its descriptor while registered and in use.
 *
 * @param encoder Borrowed encoder implementation descriptor.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_EncoderRegister(const LND_ENCODER *encoder);

/** Remove encoder from future selection.
 *
 * @param encoder Borrowed encoder implementation descriptor.
 * @return LND_OK or a negative error; existing outputs still need the descriptor.
 */
LND_API int32_t LND_EncoderUnregister(const LND_ENCODER *encoder);

/** Get the number of registered encoder implementations.
 *
 * @return The number of registered encoder implementations.
 */
LND_API uint32_t LND_EncoderGetCount(void);

/** Get a encoder at zero-based index.
 *
 * @param index Zero-based entry index.
 * @return A borrowed encoder at zero-based index, or NULL if out of range.
 */
LND_API const LND_ENCODER *LND_EncoderGet(uint32_t index);

/** Get the encoder implementation named name.
 *
 * @param name Implementation name.
 * @return The borrowed encoder implementation named name, or NULL if absent.
 */
LND_API const LND_ENCODER *LND_EncoderFind(const char *name);

/** Get encoder's implementation name.
 *
 * @param encoder Borrowed encoder implementation descriptor.
 * @return Encoder's borrowed implementation name, or NULL for NULL.
 */
LND_API const char *LND_EncoderGetName(const LND_ENCODER *encoder);

/** Get encoder's extension list.
 *
 * @param encoder Borrowed encoder implementation descriptor.
 * @return Encoder's borrowed extension list, or NULL if absent.
 */
LND_API const char *LND_EncoderGetExtensions(const LND_ENCODER *encoder);

/** Get encoder's LND_ENCODER capability flags.
 *
 * @param encoder Borrowed encoder implementation descriptor.
 * @return Encoder's LND_ENCODER capability flags, or zero for NULL.
 */
LND_API uint32_t LND_EncoderGetFlags(const LND_ENCODER *encoder);

/** Create an encoder using params and copied output procs with user.
 *
 * @param procs Callback table copied during creation.
 * @param user Callback context; the destination is adopted only on successful creation.
 * @param params Encoder settings; referenced metadata is copied on creation.
 * @return Owned output or NULL; LND_OutputFree closes a successfully adopted destination.
 */
LND_API LND_OUTPUT *LND_OutputCreateProc(const LND_IO_OUTPUT_PROCS *procs, void *user, const LND_ENCODER_PARAMS *params);

/** Encode frames of interleaved data in format using output's channel count.
 *
 * @param o Encoder output to operate on.
 * @param data Borrowed interleaved PCM for frames times output channels samples.
 * @param format LND_FORMAT sample representation.
 * @param frames Number of PCM frames to process.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_OutputWrite(LND_OUTPUT *o, const void *data, int32_t format, uint64_t frames);

/** Encode frames from pcm at offset_frames into output.
 *
 * @param o Encoder output to operate on.
 * @param pcm PCM descriptor; sample storage remains borrowed.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_OutputWritePcm(LND_OUTPUT *o, const LND_PCM *pcm, size_t offset_frames, size_t frames);

/** Pull and encode up to frames from source into output.
 *
 * @param o Encoder output to operate on.
 * @param s Source to operate on.
 * @param frames Number of PCM frames to process.
 * @return Frames written or a negative error; a waiting source may stop the read early.
 */
LND_API int64_t LND_OutputWriteSource(LND_OUTPUT *o, LND_SOURCE *s, uint64_t frames);

/** Copied output counters and finalisation result. */
typedef struct LND_OUTPUT_INFO {
    uint64_t frames; /**< PCM frames accepted by the encoder. */
    uint64_t bytes; /**< Encoded bytes written. */
    bool finished; /**< Finalisation has been attempted; no further PCM is accepted. */
    int32_t result; /**< Recorded finalisation/write result, LND_OK or a negative error. */
} LND_OUTPUT_INFO;

/** Finalise output's encoder and container once; further writes are rejected.
 *
 * @param output Encoder output to operate on.
 * @return LND_OK or the final error.
 */
LND_API int32_t LND_OutputFinish(LND_OUTPUT *output);

/** Copy output's frame/byte counters and final result into info.
 *
 * @param output Encoder output to operate on.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_OutputGetInfo(const LND_OUTPUT *output, LND_OUTPUT_INFO *info);

/** Flush buffered encoder data without ending output.
 *
 * @param o Encoder output to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_OutputFlush(LND_OUTPUT *o);

/** Finish output, close its destination and release resources.
 *
 * @param o Encoder output to operate on.
 * @return LND_OK or a finalisation/close error.
 */
LND_API int32_t LND_OutputFree(LND_OUTPUT *o);

/** Get output's encoder descriptor.
 *
 * @param o Encoder output to operate on.
 * @return Output's borrowed encoder descriptor, or NULL if unavailable.
 */
LND_API const LND_ENCODER *LND_OutputGetEncoder(const LND_OUTPUT *o);

/** Get output's input channel count.
 *
 * @param o Encoder output to operate on.
 * @return Output's input channel count, or zero for NULL.
 */
LND_API uint32_t LND_OutputGetChannels(const LND_OUTPUT *o);

/** Get output's input sample rate in Hz.
 *
 * @param o Encoder output to operate on.
 * @return Output's input sample rate in Hz, or zero for NULL.
 */
LND_API uint32_t LND_OutputGetSampleRateHz(const LND_OUTPUT *o);

/** Get PCM frames accepted by output.
 *
 * @param o Encoder output to operate on.
 * @return PCM frames accepted by output, or zero for NULL.
 */
LND_API uint64_t LND_OutputGetFrames(const LND_OUTPUT *o);

/** Get encoded bytes reported by output.
 *
 * @param o Encoder output to operate on.
 * @return Encoded bytes reported by output, or zero for NULL.
 */
LND_API uint64_t LND_OutputGetBytes(const LND_OUTPUT *o);

#ifdef __cplusplus
}
#endif
