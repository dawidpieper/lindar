/** @file
 * @details Names follow LND_ObjectOperationProperty.
 *
 * | Operation | Resource ownership |
 * | --- | --- |
 * | Get / Find | Value, caller output or borrowed view; no transfer. |
 * | Create / Clone | Owned object; release with its matching Free. |
 * | Init | Caller-owned storage; Free releases internals only. |
 * | Ensure | View owned by the source/node. |
 * | Take | Ownership transfers on success only. |
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Public symbol visibility; LND_SHARED selects DLL/shared-library linkage. */
#if defined(LND_SHARED) && defined(_WIN32)
#if defined(LND_BUILD)
#define LND_API __declspec(dllexport)
#else
#define LND_API __declspec(dllimport)
#endif
#elif defined(LND_SHARED) && defined(LND_BUILD)
#define LND_API __attribute__((visibility("default")))
#else
#define LND_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** PCM reader with format, position and end state; release owned sources with SourceFree. */
typedef struct LND_SOURCE LND_SOURCE;

/** Playback state and conversion view over a source; borrowed views belong to their source or node. */
typedef struct LND_SOUND LND_SOUND;

/** Playback conversion settings; zero dimensions use source values. Explicit conversion requires graph
 * support.
 */
typedef struct LND_SOUND_CONFIG LND_SOUND_CONFIG;

enum {
    LND_LENGTH_UNKNOWN, /**< Length has not been determined. */
    LND_LENGTH_EXACT, /**< Length is exact; zero denotes an empty stream. */
    LND_LENGTH_ESTIMATED, /**< Length is approximate and may change. */
};

enum {
    LND_MODE_REALTIME = 0, /**< Device/worker threads drive playback and updates. */
    LND_MODE_MANUAL = 1, /**< The application pulls PCM; worker support remains available. */
    LND_MODE_SINGLE_THREADED = 2, /**< The application drives work through LibraryUpdate without library worker threads. */
};

enum {
    LND_OK = 0, /**< Operation completed successfully. */
    LND_ERR_INVALID_ARG = -1, /**< Argument, range or handle is invalid. */
    LND_ERR_STATE = -2, /**< Operation is not valid in the current lifecycle state. */
    LND_ERR_EXTERNAL = -4, /**< A backend or external component failed. */
    LND_ERR_OUT_OF_MEMORY = -5, /**< Allocation failed or the requested size cannot be represented. */
    LND_ERR_UNSUPPORTED = -6, /**< The selected module/backend does not support this operation. */
    LND_ERR_FORMAT = -7, /**< Input representation or encoded data is invalid/unsupported. */
    LND_ERR_BUSY = -8, /**< Operation cannot run now, including disallowed callback re-entry. */
    LND_ERR_IO = -9, /**< Input/output operation failed. */
    LND_ERR_CYCLE = -10, /**< A graph connection would introduce a cycle. */
};

enum {
    LND_FORMAT_NONE = 0, /**< No PCM format selected; meaning depends on the accepting API. */
    LND_FORMAT_U8 = 1, /**< Unsigned 8-bit PCM; silence is 128. */
    LND_FORMAT_S16 = 2, /**< Signed 16-bit little-endian PCM. */
    LND_FORMAT_S24 = 3, /**< Packed signed 24-bit little-endian PCM. */
    LND_FORMAT_S32 = 4, /**< Signed 32-bit little-endian PCM. */
    LND_FORMAT_F32 = 5, /**< 32-bit floating-point PCM; nominal full scale is [-1, 1]. */
    LND_FORMAT_F64 = 6, /**< 64-bit floating-point PCM; nominal full scale is [-1, 1]. */
};

enum {
    LND_SOUND_STOPPED = 0, /**< Playback is stopped. */
    LND_SOUND_PLAYING = 1, /**< Playback is advancing. */
    LND_SOUND_PAUSED = 2, /**< Playback is paused without rewinding. */
    LND_SOUND_STALLED = 3, /**< Playback is waiting for more source data. */
};

enum {
    LND_READ_EOF = -11, /**< Signed read callback sentinel for permanent end of input. */
    LND_SOURCE_READY = 0, /**< Source can produce PCM. */
    LND_SOURCE_WAITING = 1, /**< Input is temporarily unavailable; this is not EOF. */
    LND_SOURCE_EOF = 2, /**< Source is exhausted after buffered input drains. */
};

enum {
    LND_SOURCE_LIVE = 1u << 3, /**< Treat an empty read as temporary waiting until End or explicit EOF. */
};

/** Opaque configuration identity, stable until library unload; never free or fabricate a key. */
typedef struct LND_CONFIG_KEY LND_CONFIG_KEY;

/** Key "core.internal_format": Internal LND_FORMAT sample representation; set before LibraryInit. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_INTERNAL_FORMAT;

/** Key "core.internal_layout": Internal LND_LAYOUT selection; set before LibraryInit. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_INTERNAL_LAYOUT;

/** Key "core.run_mode": LND_MODE execution model; set before LibraryInit. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_RUN_MODE;

/** Allocate size bytes for user, suitably aligned for C objects.
 *
 * @param user Borrowed callback context.
 * @param size Requested allocation size in bytes.
 * @return Memory or NULL; pair with the configured free callback.
 */
typedef void *(*LND_ALLOC_PROC)(void *user, size_t size);

/** Resize ptr to size bytes for user.
 *
 * @param user Borrowed callback context.
 * @param ptr Memory obtained from the same allocator.
 * @param size Requested allocation size in bytes.
 * @return Memory or NULL; failure preserves ptr. Optional, never mixed with the libc allocator.
 */
typedef void *(*LND_REALLOC_PROC)(void *user, void *ptr, size_t size);

/** Release ptr using the allocator identified by user.
 *
 * @param user Borrowed callback context.
 * @param ptr Memory obtained from the same allocator.
 */
typedef void (*LND_FREE_PROC)(void *user, void *ptr);

/** Allocator callbacks copied before LibraryInit; alloc/free are required, realloc is optional, user is
 * borrowed.
 */
typedef struct LND_ALLOCATOR_CONFIG {
    LND_ALLOC_PROC alloc; /**< Required allocation callback. */
    LND_REALLOC_PROC realloc; /**< Optional resize callback; without it, existing allocations cannot grow. */
    LND_FREE_PROC free; /**< Required deallocation callback, paired with alloc. */
    void *user; /**< Borrowed context passed to callbacks. */
} LND_ALLOCATOR_CONFIG;

/** Get the library's release name; no initialisation is required.
 *
 * @return Static, null-terminated string; do not free.
 */
LND_API const char *LND_GetVersionName(void);

/** Initialise the configured modules.
 *
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_LibraryInit(void);

/** Release library-owned playback resources and restore configuration defaults; configuration key
 * identities remain valid.
 */
LND_API void LND_LibraryFree(void);

/** Advance manual work and dispatch pending callbacks.
 *
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_LibraryUpdate(void);

/** Copy allocator callbacks before LND_LibraryInit; NULL restores defaults.
 *
 * @param config Allocator callbacks to copy; NULL restores defaults.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_AllocatorSetConfig(const LND_ALLOCATOR_CONFIG *config);

/** Copy the current callbacks and borrowed user pointer into config.
 *
 * @param config Receives the current settings.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_AllocatorGetConfig(LND_ALLOCATOR_CONFIG *config);

/** Set key to value within its range and permitted lifecycle phase.
 *
 * @param key Borrowed configuration key from this library.
 * @param value New configuration value within the key's supported range.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_ConfigSet(LND_CONFIG_KEY *key, uint64_t value);

/** Get the current value for key.
 *
 * @param key Borrowed configuration key from this library.
 * @return The current value for key; zero on error, with details in LND_ErrorGetLast.
 */
LND_API uint64_t LND_ConfigGet(const LND_CONFIG_KEY *key);

/** Get the number of public configuration keys in this build.
 *
 * @return The number of public configuration keys in this build.
 */
LND_API uint32_t LND_ConfigGetKeyCount(void);

/** Get a key at zero-based index.
 *
 * @param index Zero-based entry index.
 * @return A borrowed key at zero-based index, or NULL if out of range; indices are not persistent
 * IDs.
 */
LND_API LND_CONFIG_KEY *LND_ConfigGetKey(uint32_t index);

/** Get a key matching the case-sensitive name.
 *
 * @param name Case-sensitive configuration key name.
 * @return A borrowed key matching the case-sensitive name, or NULL if absent.
 */
LND_API LND_CONFIG_KEY *LND_ConfigFindKey(const char *name);

/** Get the borrowed, case-sensitive name of key.
 *
 * @param key Borrowed configuration key from this library.
 * @return The borrowed, case-sensitive name of key, or NULL for an invalid key.
 */
LND_API const char *LND_ConfigKeyGetName(const LND_CONFIG_KEY *key);

/** Get the last recorded error, thread-local in threaded builds.
 *
 * @return The last recorded error, thread-local in threaded builds; successful calls need not
 * clear it.
 */
LND_API int32_t LND_ErrorGetLast(void);

/** Get a description of code.
 *
 * @param code LND_OK or an LND_ERR error code.
 * @return A static description of code; the caller must not free it.
 */
LND_API const char *LND_ErrorGetString(int32_t code);

enum {
    LND_LAYOUT_INTERLEAVED = 0, /**< Each frame stores all channel samples consecutively. */
    LND_LAYOUT_PLANAR = 1, /**< Each channel has a separate sample plane. */
};

enum {
    LND_FORMAT_S16LE = LND_FORMAT_S16, /**< Explicit little-endian alias for S16. */
    LND_FORMAT_S24LE = LND_FORMAT_S24, /**< Explicit little-endian alias for S24. */
    LND_FORMAT_S32LE = LND_FORMAT_S32, /**< Explicit little-endian alias for S32. */
};

enum {
    LND_LAYOUT_MASK_INTERLEAVED = 1u << LND_LAYOUT_INTERLEAVED, /**< Accept interleaved PCM. */
    LND_LAYOUT_MASK_PLANAR = 1u << LND_LAYOUT_PLANAR, /**< Accept planar PCM. */
    LND_LAYOUT_MASK_ALL = LND_LAYOUT_MASK_INTERLEAVED | LND_LAYOUT_MASK_PLANAR /**< Accept both PCM layouts. */
};

/** Borrowed PCM buffer descriptor; const protects the descriptor, not the samples. A frame contains one
 * sample per channel.
 */
typedef struct LND_PCM {
    void *data; /**< Interleaved sample base; unused for planar PCM. */
    void *const *planes; /**< Array of channels sample bases for planar PCM. */
    size_t frames; /**< Accessible frames in each channel. */
    size_t stride_bytes; /**< Bytes between frames in data/each plane; 0 selects tightly packed storage. */
    uint32_t channels; /**< Number of PCM channels. */
    int32_t format; /**< LND_FORMAT sample representation. */
    int32_t layout; /**< LND_LAYOUT_INTERLEAVED or LND_LAYOUT_PLANAR. */
} LND_PCM;

/** Get bytes per sample for format.
 *
 * @param format LND_FORMAT sample representation.
 * @return Bytes per sample for format, or zero for an unsupported format.
 */
LND_API size_t LND_PcmGetSampleBytes(int32_t format);

/** Check pcm layout, dimensions and buffer ranges.
 *
 * @param pcm PCM descriptor; sample storage remains borrowed.
 * @return LND_OK or a negative error; buffer contents are not inspected.
 */
LND_API int32_t LND_PcmValidate(const LND_PCM *pcm);

/** Convert frames from src_offset_frames to dst_offset_frames with equal channel counts.
 *
 * @param dst Destination PCM descriptor with writable sample storage.
 * @param dst_offset_frames Zero-based starting frame in dst.
 * @param src Source PCM descriptor; sample storage remains borrowed.
 * @param src_offset_frames Zero-based starting frame in src.
 * @param frames Number of PCM frames to process.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_PcmConvert(const LND_PCM *dst, size_t dst_offset_frames, const LND_PCM *src, size_t src_offset_frames, size_t frames);

/** Fill frames at offset_frames with silence in pcm's format.
 *
 * @param pcm Destination PCM descriptor with writable sample storage.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_PcmSilence(const LND_PCM *pcm, size_t offset_frames, size_t frames);

/** Manual PCM pull renderer; release with RendererFree, including instances initialised in caller storage. */
typedef struct LND_RENDERER LND_RENDERER;

/** Fill frames of pcm at offset_frames for user.
 *
 * @param user Borrowed callback context.
 * @param pcm Destination PCM descriptor with writable sample storage.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @return Frames, LND_READ_EOF or a negative error; a short read ends finite sources, while live
 * sources may wait.
 */
typedef int64_t (*LND_RENDER_PROC)(void *user, const LND_PCM *pcm, size_t offset_frames, size_t frames);

/** Release callback state identified by user when its owner closes.
 *
 * @param user Borrowed callback context.
 */
typedef void (*LND_RENDER_CLOSE_PROC)(void *user);

/** Callback renderer configuration, copied at creation; user remains valid until close. */
typedef struct LND_RENDERER_CONFIG {
    LND_RENDER_PROC render; /**< Required render callback writing the requested PCM range. */
    LND_RENDER_CLOSE_PROC close; /**< Optional cleanup callback, called when the renderer is freed. */
    void *user; /**< Borrowed context passed to callbacks. */
    uint32_t channels; /**< Number of PCM channels. */
    uint32_t sample_rate_hz; /**< PCM sample rate in Hz. */
    uint32_t block_frames; /**< Nonzero maximum internal render block length. */
} LND_RENDERER_CONFIG;

/** Get storage bytes required by config and the current PCM settings.
 *
 * @param config Required settings, borrowed during the call.
 * @return Storage bytes required by config and the current PCM settings, or zero for invalid
 * dimensions.
 */
LND_API size_t LND_RendererGetMemoryBytes(const LND_RENDERER_CONFIG *config);

/** Initialise config in bytes of caller storage aligned to max_align_t.
 *
 * @param memory Caller-owned storage aligned to max_align_t.
 * @param bytes Available size of memory in bytes.
 * @param config Required settings, borrowed during the call.
 * @return A renderer or NULL; Free preserves the storage.
 */
LND_API LND_RENDERER *LND_RendererInit(void *memory, size_t bytes, const LND_RENDERER_CONFIG *config);

/** Copy config and allocate a renderer using its callbacks.
 *
 * @param config Required settings, borrowed during the call.
 * @return An owned renderer or NULL; release with LND_RendererFree.
 */
LND_API LND_RENDERER *LND_RendererCreateProc(const LND_RENDERER_CONFIG *config);

/** Close renderer and release its owned storage.
 *
 * @param renderer Renderer to operate on.
 * @return LND_OK or a negative error; caller storage from Init remains owned by the caller.
 */
LND_API int32_t LND_RendererFree(LND_RENDERER *renderer);

/** Get renderer's sample rate in Hz.
 *
 * @param renderer Renderer to operate on.
 * @return Renderer's sample rate in Hz, or zero for NULL.
 */
LND_API uint32_t LND_RendererGetSampleRateHz(const LND_RENDERER *renderer);

/** Get renderer's channel count.
 *
 * @param renderer Renderer to operate on.
 * @return Renderer's channel count, or zero for NULL.
 */
LND_API uint32_t LND_RendererGetChannels(const LND_RENDERER *renderer);

/** Get renderer's internal LND_FORMAT value.
 *
 * @param renderer Renderer to operate on.
 * @return Renderer's internal LND_FORMAT value, or LND_FORMAT_NONE for NULL.
 */
LND_API int32_t LND_RendererGetFormat(const LND_RENDERER *renderer);

/** Get renderer's internal LND_LAYOUT value.
 *
 * @param renderer Renderer to operate on.
 * @return Renderer's internal LND_LAYOUT value; NULL yields INTERLEAVED.
 */
LND_API int32_t LND_RendererGetLayout(const LND_RENDERER *renderer);

/** Render frames into pcm at offset_frames and silence any shortfall.
 *
 * @param renderer Renderer to operate on.
 * @param pcm Destination PCM descriptor with writable sample storage.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @return Actual frames before padding, or a negative error.
 */
LND_API int64_t LND_RendererReadPcm(LND_RENDERER *renderer, const LND_PCM *pcm, size_t offset_frames, size_t frames);

/** Render frames into pcm at offset_frames and silence any shortfall.
 *
 * @param renderer Renderer to operate on.
 * @param pcm Destination PCM descriptor with writable sample storage.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_RendererFillPcm(LND_RENDERER *renderer, const LND_PCM *pcm, size_t offset_frames, size_t frames);

/** Copied source snapshot; length_kind distinguishes empty, unknown and estimated lengths. */
typedef struct LND_SOURCE_INFO {
    uint64_t length_frames; /**< Stream length in frames; check length validity separately. */
    uint64_t position_frames; /**< Current position in PCM frames. */
    uint32_t sample_rate_hz; /**< PCM sample rate in Hz. */
    uint32_t channels; /**< Number of PCM channels. */
    int32_t format; /**< LND_FORMAT sample representation. */
    int32_t status; /**< LND_SOURCE_READY, WAITING, EOF or a negative error. */
    int32_t length_kind; /**< LND_LENGTH_UNKNOWN, EXACT or ESTIMATED. */
    uint64_t bitrate_bps; /**< Encoded bitrate in bits per second. */
    bool bitrate_estimated; /**< True if bitrate_bps is an estimate. */
} LND_SOURCE_INFO;

/** Copy source's format, position, length validity and status into info.
 *
 * @param source Source to operate on.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SourceGetInfo(const LND_SOURCE *source, LND_SOURCE_INFO *info);

/** Seek user's source to absolute frame.
 *
 * @param user Borrowed callback context.
 * @param frame Absolute frame position.
 * @return LND_OK or a negative error.
 */
typedef int32_t (*LND_SOURCE_SEEK_PROC)(void *user, uint64_t frame);

/** Source configuration: supply exactly one of read or pcm. The descriptor is copied; sample data and user
 * are borrowed.
 */
typedef struct LND_SOURCE_CONFIG {
    LND_RENDER_PROC read; /**< PCM callback, required unless pcm is supplied. */
    LND_SOURCE_SEEK_PROC seek; /**< Optional absolute-frame seek callback. */
    LND_RENDER_CLOSE_PROC close; /**< Optional callback releasing user state at source destruction. */
    void *user; /**< Borrowed context passed to callbacks. */
    const LND_PCM *pcm; /**< Borrowed sample storage alternative to read; descriptor and plane pointers are copied. */
    uint64_t length_frames; /**< Callback length; leave zero with pcm, whose frames define length. */
    uint32_t channels; /**< Number of PCM channels. */
    uint32_t sample_rate_hz; /**< PCM sample rate in Hz. */
    uint32_t block_frames; /**< Conversion scratch capacity; zero avoids allocating scratch. */
    uint32_t flags; /**< LND_SOURCE_LIVE or zero. */
    bool length_known; /**< True when length_frames is available, including an empty stream. */
} LND_SOURCE_CONFIG;

/** Get storage bytes required by config and current PCM settings.
 *
 * @param config Required settings, borrowed during the call.
 * @return Storage bytes required by config and current PCM settings, or zero if invalid; does not
 * allocate.
 */
LND_API size_t LND_SourceGetMemoryBytes(const LND_SOURCE_CONFIG *config);

/** Initialise config in bytes of caller storage aligned to max_align_t.
 *
 * @param memory Caller-owned storage aligned to max_align_t.
 * @param bytes Available size of memory in bytes.
 * @param config Required settings, borrowed during the call.
 * @return A source or NULL; PCM samples remain borrowed.
 */
LND_API LND_SOURCE *LND_SourceInit(void *memory, size_t bytes, const LND_SOURCE_CONFIG *config);

/** Copy config and create a source borrowing its PCM samples or callback user.
 *
 * @param config Required settings, borrowed during the call.
 * @return An owned source or NULL; release with LND_SourceFree.
 */
LND_API LND_SOURCE *LND_SourceCreate(const LND_SOURCE_CONFIG *config);

/** Read up to frames into pcm at offset_frames.
 *
 * @param source Source to operate on.
 * @param pcm Destination PCM descriptor with writable sample storage.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @return Frames read or a negative error; use GetStatus to distinguish waiting from EOF.
 */
LND_API int64_t LND_SourceReadPcm(LND_SOURCE *source, const LND_PCM *pcm, size_t offset_frames, size_t frames);

/** Get source's READY, WAITING or EOF state.
 *
 * @param source Source to operate on.
 * @return Source's READY, WAITING or EOF state, or a negative error.
 */
LND_API int32_t LND_SourceGetStatus(const LND_SOURCE *source);

/** Mark a live source's input complete while allowing buffered PCM to drain.
 *
 * @param source Source to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SourceEnd(LND_SOURCE *source);

/** Release source and its owned adapters; close its input.
 *
 * @param s Source to operate on.
 * @return LND_OK or a negative error; Init storage remains caller-owned.
 */
LND_API int32_t LND_SourceFree(LND_SOURCE *s);

/** Get source's PCM format.
 *
 * @param s Source to operate on.
 * @return Source's PCM format, or LND_FORMAT_NONE if unavailable.
 */
LND_API int32_t LND_SourceGetFormat(const LND_SOURCE *s);

/** Get source's sample rate in Hz.
 *
 * @param s Source to operate on.
 * @return Source's sample rate in Hz, or zero if unavailable.
 */
LND_API uint32_t LND_SourceGetSampleRateHz(const LND_SOURCE *s);

/** Get source's channel count.
 *
 * @param s Source to operate on.
 * @return Source's channel count, or zero if unavailable.
 */
LND_API uint32_t LND_SourceGetChannels(const LND_SOURCE *s);

/** Get source length in frames.
 *
 * @param s Source to operate on.
 * @return Source length in frames; consult LND_SourceGetInfo to distinguish unknown, estimated and
 * exact lengths.
 */
LND_API uint64_t LND_SourceGetLengthFrames(const LND_SOURCE *s);

/** Get source's current read position in frames.
 *
 * @param s Source to operate on.
 * @return Source's current read position in frames, or zero if unavailable.
 */
LND_API uint64_t LND_SourceGetPositionFrames(const LND_SOURCE *s);

/** Seek source to absolute frame.
 *
 * @param s Source to operate on.
 * @param frame Absolute frame position.
 * @return LND_OK or a negative error, including unsupported seeking.
 */
LND_API int32_t LND_SourceSeekFrames(LND_SOURCE *s, uint64_t frame);

/** Read up to frames into interleaved dst in format.
 *
 * @param s Source to operate on.
 * @param dst Writable interleaved PCM buffer for frames times channels samples.
 * @param format LND_FORMAT sample representation.
 * @param frames Number of PCM frames to process.
 * @return Frames read or a negative error; dst must hold frames times channels samples.
 */
LND_API int64_t LND_SourceRead(LND_SOURCE *s, void *dst, int32_t format, uint64_t frames);

/** Get source's existing sound view.
 *
 * @param source Source to operate on.
 * @return Source's existing borrowed sound view, or NULL; does not create a view.
 */
LND_API LND_SOUND *LND_SourceGetSound(const LND_SOURCE *source);

/** Get or create the sound view of a source.
 *
 * @param s Source to operate on.
 * @param config Optional sound settings; NULL reuses an existing view or selects defaults.
 * @return The borrowed sound view, or NULL on failure or incompatible settings. The source owns
 * the view.
 */
LND_API LND_SOUND *LND_SourceEnsureSound(LND_SOURCE *s, const LND_SOUND_CONFIG *config);

/** Playback conversion settings; zero dimensions use source values. Explicit conversion requires graph
 * support.
 */
typedef struct LND_SOUND_CONFIG {
    uint32_t channels; /**< Output channels; zero uses the source count. */
    uint32_t sample_rate_hz; /**< Output sample rate in Hz; zero uses the source rate. */
    const float *channel_matrix; /**< Optional copied row-major output-by-input gain matrix. */
    uint32_t flags; /**< LND_SOUND_RESAMPLE selection bits; zero uses the configured default. */
} LND_SOUND_CONFIG;

/** Get caller storage bytes needed by LND_SoundInit.
 *
 * @return Caller storage bytes needed by LND_SoundInit; does not allocate.
 */
LND_API size_t LND_SoundGetMemoryBytes(void);

/** Initialise a sound for source in bytes of storage aligned to max_align_t.
 *
 * @param memory Caller-owned storage aligned to max_align_t.
 * @param bytes Available size of memory in bytes.
 * @param source Borrowed source; retain it until the sound is freed.
 * @return A sound or NULL; both source and storage remain caller-owned.
 */
LND_API LND_SOUND *LND_SoundInit(void *memory, size_t bytes, LND_SOURCE *source);

/** Release sound's resources without freeing its source or caller storage.
 *
 * @param sound Sound to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundFree(LND_SOUND *sound);

/** Render sound into pcm at offset_frames as an LND_RENDER_PROC.
 *
 * @param sound Borrowed LND_SOUND passed as callback context.
 * @param pcm Destination PCM descriptor with writable sample storage.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @return Frames produced or a negative error.
 */
LND_API int64_t LND_SoundRenderPcm(void *sound, const LND_PCM *pcm, size_t offset_frames, size_t frames);

/** Set unsigned Q16 gain on sound; 65536 is unity and zero is silence.
 *
 * @param sound Sound to operate on.
 * @param gain Unsigned Q16 gain; 65536 is unity and 0 is silence.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundSetGainQ16(LND_SOUND *sound, uint32_t gain);

/** Get sound's unsigned Q16 gain.
 *
 * @param sound Sound to operate on.
 * @return Sound's unsigned Q16 gain; 65536 is unity.
 */
LND_API uint32_t LND_SoundGetGainQ16(const LND_SOUND *sound);

/** Get sound's source.
 *
 * @param s Sound to operate on.
 * @return Sound's borrowed source, or NULL if unavailable.
 */
LND_API LND_SOURCE *LND_SoundGetSource(const LND_SOUND *s);

/** Start or resume sound playback.
 *
 * @param s Sound to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundPlay(LND_SOUND *s);

/** Pause sound when pause is true, or resume it; preserve position.
 *
 * @param s Sound to operate on.
 * @param pause True to pause; false to resume.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundSetPause(LND_SOUND *s, bool pause);

/** Stop sound and rewind its source where supported.
 *
 * @param s Sound to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundStop(LND_SOUND *s);

/** Get sound's STOPPED, PLAYING, PAUSED or STALLED state.
 *
 * @param s Sound to operate on.
 * @return Sound's STOPPED, PLAYING, PAUSED or STALLED state.
 */
LND_API int32_t LND_SoundGetState(const LND_SOUND *s);

/** Get playback position in source frames.
 *
 * @param s Sound to operate on.
 * @return Playback position in source frames; graph latency compensation follows configuration.
 */
LND_API uint64_t LND_SoundGetPositionFrames(const LND_SOUND *s);

/** Seek sound to absolute source frame.
 *
 * @param s Sound to operate on.
 * @param frame Absolute frame position.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundSeekFrames(LND_SOUND *s, uint64_t frame);

/** Get sound's source length in frames.
 *
 * @param s Sound to operate on.
 * @return Sound's source length in frames; inspect LND_SourceGetInfo for length validity.
 */
LND_API uint64_t LND_SoundGetLengthFrames(const LND_SOUND *s);

/** Get sound's output sample rate in Hz.
 *
 * @param s Sound to operate on.
 * @return Sound's output sample rate in Hz, or zero if unavailable.
 */
LND_API uint32_t LND_SoundGetSampleRateHz(const LND_SOUND *s);

/** Get sound's output channel count.
 *
 * @param s Sound to operate on.
 * @return Sound's output channel count, or zero if unavailable.
 */
LND_API uint32_t LND_SoundGetChannels(const LND_SOUND *s);

/** Enable or disable rewinding at EOF for sound.
 *
 * @param s Sound to operate on.
 * @param loop True to loop at end of input; false to stop.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundSetLoop(LND_SOUND *s, bool loop);

/** Check whether sound is configured to loop.
 *
 * @param s Sound to operate on.
 * @return True if sound is configured to loop; false otherwise.
 */
LND_API bool LND_SoundGetLoop(const LND_SOUND *s);

/** Apply config to sound and its borrowed views; NULL restores source defaults. Copy any matrix.
 *
 * @param sound Sound to operate on.
 * @param config Sound settings to copy; NULL restores source defaults.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundSetConfig(LND_SOUND *sound, const LND_SOUND_CONFIG *config);

#ifdef __cplusplus
}
#endif
