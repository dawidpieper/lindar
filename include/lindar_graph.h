#pragma once

#include "lindar.h"
#include "lindar_buffers.h"
#include "lindar_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Key "graph.sample_rate_hz": Default graph sample rate in Hz; zero selects automatically. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_SAMPLE_RATE_HZ;

/** Key "graph.channels": Default graph channel count; zero selects automatically. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_CHANNELS;

/** Key "graph.buffer_frames": Default buffered source block length in frames. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_BUFFER_FRAMES;

/** Key "graph.buffer_count": Number of blocks in default source buffering. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_BUFFER_COUNT;

/** Key "graph.mix_block_frames": Maximum PCM block length for graph mixing. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_MIX_BLOCK_FRAMES;

/** Key "graph.command_queue_capacity": Per-node control queue capacity allocated on first command.
 * Submit initial controls before entering an allocation-sensitive render loop.
 */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_COMMAND_QUEUE_CAPACITY;

/** Key "graph.gain_ramp_frames": Default gain smoothing length in frames; zero disables smoothing. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_GAIN_RAMP_FRAMES;

/** Key "graph.clip_mode": Default LND_CLIP policy for mixed PCM. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_CLIP_MODE;

/** Key "graph.mixer_buffer_frames": Default frame capacity for buffered mixer/splitter input. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_MIXER_BUFFER_FRAMES;

/** Key "graph.drain_timeout_ms": Maximum wait for playback draining, in milliseconds. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_DRAIN_TIMEOUT_MS;

/** Key "graph.position_compensate": Compensate reported playback position for buffered latency (0/1). */
LND_API extern LND_CONFIG_KEY *const LND_CFG_GRAPH_POSITION_COMPENSATE;

enum {
    LND_GRAPH_SOURCE_DIRECT = 1u << 0 /**< Pull source callbacks directly rather than through a worker buffer. */
};

/** Graph node or borrowed adapter; release owned nodes with NodeFree and borrowed views through their
 * owner.
 */
typedef struct LND_NODE LND_NODE;

enum {
    LND_CLIP_NONE = 0, /**< Leave out-of-range floating-point samples unchanged. */
    LND_CLIP_HARD = 1, /**< Clamp mixed samples to nominal full scale. */
    LND_CLIP_SOFT = 2, /**< Apply smooth saturation to mixed samples. */
};

enum {
    LND_NODE_BUS = 0, /**< General mixing bus. */
    LND_NODE_DESTINATION = 1, /**< Device output endpoint. */
    LND_NODE_SOURCE = 2, /**< Graph-backed source reader. */
    LND_NODE_SPLITTER = 3, /**< Buffered parent of independent readers. */
    LND_NODE_SPLIT = 4, /**< One reader of a splitter's shared PCM. */
    LND_NODE_MIXER = 5, /**< Mixer with explicit input availability policy. */
    LND_NODE_PROCESSOR = 6, /**< Callback or module-defined PCM processor. */
    LND_NODE_CHANNEL_SPLITTER = 7, /**< Parent splitting channels into mono branches. */
    LND_NODE_CHANNEL = 8, /**< One mono channel branch. */
    LND_NODE_CHANNEL_MERGER = 9, /**< Node combining mono channel inputs. */
    LND_NODE_TERMINAL = 10, /**< Generic terminal sink; role does not identify its module. */
    LND_NODE_PCM_INPUT = 11, /**< Adapter pulling a core PCM source/sound. */
};

enum {
    LND_MIX_CONTINUOUS = 0, /**< Keep output time moving; unavailable inputs contribute silence. */
    LND_MIX_LOCKSTEP = 1, /**< Advance only as far as all required inputs permit. */
    LND_MIX_AVAILABLE = 2, /**< Mix whichever inputs currently have PCM. */
    LND_MIX_MODE_MASK = 3, /**< Mask selecting the mixer availability policy. */
    LND_MIX_END = 1u << 8, /**< Finish the mixer when every input has ended. */
};

enum {
    LND_PARAM_GAIN = 0, /**< Linear node gain; 1 is unity. */
    LND_PARAM_COUNT, /**< Number of built-in node parameters. */
    LND_PARAM_USER = 256, /**< First processor/module parameter ID. */
    LND_PARAM_USER_COUNT = 64, /**< Number of slots reserved for processor/module parameters. */
};

enum {
    LND_SOUND_RESAMPLE_DEFAULT = 0, /**< Use the configured default resampling quality. */
    LND_SOUND_RESAMPLE_LINEAR = 1, /**< Use linear interpolation for this sound. */
    LND_SOUND_RESAMPLE_SINC8 = 2, /**< Use 8-tap sinc interpolation for this sound. */
    LND_SOUND_RESAMPLE_SINC16 = 3, /**< Use 16-tap sinc interpolation for this sound. */
    LND_SOUND_RESAMPLE_SINC32 = 4, /**< Use 32-tap sinc interpolation for this sound. */
    LND_SOUND_RESAMPLE_MASK = 7, /**< Mask for the sound-specific resampling selection. */
};

/** Read up to frames into interleaved dst for user.
 *
 * @param user Borrowed callback context.
 * @param dst Writable interleaved PCM buffer for frames times channels samples.
 * @param frames Number of PCM frames to process.
 * @return Frames, LND_READ_EOF or a negative error; a short read ends finite input, while live
 * input may wait.
 */
typedef int64_t (*LND_SOURCE_READ_PROC)(void *user, void *dst, uint64_t frames);

/** Interleaved source callbacks copied at creation; callback user data remains borrowed until close. */
typedef struct LND_SOURCE_PROCS {
    LND_SOURCE_READ_PROC read; /**< Required interleaved PCM reader. */
    LND_SOURCE_SEEK_PROC seek; /**< Optional absolute-frame seek callback. */
    uint64_t length_frames; /**< Stream length in frames; check length validity separately. */
    LND_RENDER_CLOSE_PROC close; /**< Optional cleanup for the borrowed callback user. */
    bool length_known; /**< True when length_frames is available, including an empty stream. */
} LND_SOURCE_PROCS;

/** Create a source retaining b; the caller may release its own buffer reference.
 *
 * @param b PCM buffer to retain; the caller keeps its own reference.
 * @return Owned source or NULL; release with LND_SourceFree.
 */
LND_API LND_SOURCE *LND_SourceCreateBuffer(LND_BUFFER *b);

/** Create a source from copied procs and borrowed user, using format, channels, rate and flags.
 *
 * @param procs Callback table copied during creation.
 * @param user Borrowed callback context.
 * @param format LND_FORMAT sample representation.
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param flags LND_SOURCE option bits.
 * @return Owned source or NULL; release with LND_SourceFree.
 */
LND_API LND_SOURCE *LND_SourceCreateProc(const LND_SOURCE_PROCS *procs, void *user, int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint32_t flags);

/** Get source's existing graph adapter.
 *
 * @param source Source to operate on.
 * @return Source's existing borrowed graph adapter, or NULL; does not create one.
 */
LND_API LND_NODE *LND_SourceGetNode(const LND_SOURCE *source);

/** Get sound's existing graph adapter.
 *
 * @param sound Sound to operate on.
 * @return Sound's existing borrowed graph adapter, or NULL; does not create one.
 */
LND_API LND_NODE *LND_SoundGetNode(const LND_SOUND *sound);

/** Get node's existing source view.
 *
 * @param node Graph node to operate on.
 * @return Node's existing borrowed source view, or NULL; does not create one.
 */
LND_API LND_SOURCE *LND_NodeGetSource(const LND_NODE *node);

/** Get node's existing sound view.
 *
 * @param node Graph node to operate on.
 * @return Node's existing borrowed sound view, or NULL; does not create one.
 */
LND_API LND_SOUND *LND_NodeGetSound(const LND_NODE *node);

/** Get or create the graph adapter of a source.
 *
 * @param s Source to operate on.
 * @return The borrowed graph adapter, or NULL on failure. The source owns it.
 */
LND_API LND_NODE *LND_SourceEnsureNode(LND_SOURCE *s);

/** Get or create the graph adapter of a sound.
 *
 * @param s Sound to operate on.
 * @return The borrowed graph adapter, or NULL on failure. The sound owns it.
 */
LND_API LND_NODE *LND_SoundEnsureNode(LND_SOUND *s);

/** Route sound to dst; NULL disconnects its output.
 * Seeking the sound does not reset downstream processing history. Reset affected processors when
 * old history must be discarded; pausing the sound can leave buffered output audible.
 *
 * @param s Sound to operate on.
 * @param dst Destination node; NULL disconnects the sound.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SoundSetOutput(LND_SOUND *s, LND_NODE *dst);

/** Get sound's output node.
 *
 * @param s Sound to operate on.
 * @return Sound's borrowed output node, or NULL if none is assigned.
 */
LND_API LND_NODE *LND_SoundGetOutput(const LND_SOUND *s);

/** Process interleaved F32 pcm in place: frames, channels and sample_rate_hz describe the block.
 * user is borrowed.
 *
 * @param user Borrowed callback context.
 * @param pcm Writable interleaved F32 samples for frames.
 * @param frames Number of PCM frames to process.
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 */
typedef void (*LND_PROCESS_PROC)(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz);

/** Process frames in pcm at offset_frames in place, at sample_rate_hz for user.
 *
 * @param user Borrowed callback context.
 * @param pcm Destination PCM descriptor with writable sample storage.
 * @param offset_frames Zero-based starting frame in pcm.
 * @param frames Number of PCM frames to process.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @return LND_OK or a negative error.
 */
typedef int32_t (*LND_PROCESS_PCM_PROC)(void *user, const LND_PCM *pcm, size_t offset_frames, uint32_t frames, uint32_t sample_rate_hz);

/** Apply parameter param's value to processor user state.
 *
 * @param user Borrowed callback context.
 * @param param Parameter identifier defined by the node type.
 * @param value New value in the selected parameter's units.
 */
typedef void (*LND_PARAM_PROC)(void *user, int32_t param, float value);

/** Release processor user state when its node is destroyed.
 *
 * @param user Borrowed callback context.
 */
typedef void (*LND_RELEASE_PROC)(void *user);

enum {
    LND_PROCESSOR_BOUNDED = 1u << 0 /**< Process only frames supplied by inputs; do not extend processing through padded silence. */
};

/** Processor callbacks copied at creation; supply exactly one of process or process_pcm. */
typedef struct LND_PROCESSOR_PROCS {
    LND_PROCESS_PROC process; /**< Optional interleaved F32 processing callback. */
    LND_PARAM_PROC param; /**< Optional parameter update callback. */
    LND_RELEASE_PROC release; /**< Optional callback releasing processor user state. */
    uint32_t flags; /**< LND_PROCESSOR_BOUNDED or zero. */
    /** Check whether a processor parameter value is accepted.
     *
     * @param user Borrowed callback context.
     * @param param Parameter identifier defined by the node type.
     * @param value New value in the selected parameter's units.
     * @return True if param/value is accepted for user; optional.
     */
    bool (*validate)(void *user, int32_t param, float value);
    /** Check whether a processor parameter can slide.
     *
     * @param user Borrowed callback context.
     * @param param Parameter identifier defined by the node type.
     * @return True if param permits continuous slides for user; optional.
     */
    bool (*can_slide)(void *user, int32_t param);
    LND_PROCESS_PCM_PROC process_pcm; /**< Typed/layout-aware processing callback, mutually exclusive with process. */
    int32_t process_format; /**< Required PCM format for process_pcm; NONE uses the internal format. */
    uint32_t process_layouts; /**< Accepted LND_LAYOUT_MASK bits; zero accepts both layouts. */
} LND_PROCESSOR_PROCS;

/** Create a manual renderer borrowing node; freeing node detaches it.
 *
 * @param node Borrowed graph node; freeing it detaches this renderer.
 * @return Owned renderer or NULL; release with LND_RendererFree.
 */
LND_API LND_RENDERER *LND_RendererCreateNode(LND_NODE *node);

/** Create a mixing bus for channels and sample_rate_hz.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @return An owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateBus(uint32_t channels, uint32_t sample_rate_hz);

/** Create a buffered splitter with outputs independent readers at channels and rate.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param outputs Number of output branches.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateSplitter(uint32_t channels, uint32_t sample_rate_hz, uint32_t outputs);

/** Get splitter's branch at zero-based index.
 *
 * @param splitter Splitter node.
 * @param index Zero-based entry index.
 * @return Splitter's borrowed branch at zero-based index, or NULL if invalid.
 */
LND_API LND_NODE *LND_NodeGetSplitterOutput(const LND_NODE *splitter, uint32_t index);

/** Move branch to the splitter's current position and clear its dropped count.
 *
 * @param branch Borrowed splitter output branch.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeResetSplit(LND_NODE *branch);

/** Get frames lost when branch lagged behind the splitter's buffer.
 *
 * @param branch Borrowed splitter output branch.
 * @return Frames lost when branch lagged behind the splitter's buffer, or zero if unavailable.
 */
LND_API uint64_t LND_NodeGetSplitDroppedFrames(const LND_NODE *branch);

/** Get splitter's branch count.
 *
 * @param splitter Splitter node.
 * @return Splitter's branch count, or zero for a different node type.
 */
LND_API uint32_t LND_NodeGetSplitterOutputCount(const LND_NODE *splitter);

/** Create a mixer at channels and rate using LND_MIX flags.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param flags LND_MIX option bits.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateMixer(uint32_t channels, uint32_t sample_rate_hz, uint32_t flags);

/** Get node's READY, WAITING or EOF state.
 * Source EOF can precede resampler and processor tails. Drain the final processing node; continuous mixers do not end automatically.
 *
 * @param node Graph node to operate on.
 * @return Node's READY, WAITING or EOF state, or a negative error.
 */
LND_API int32_t LND_NodeGetStatus(const LND_NODE *node);

/** Pause or resume input's connection to node without disconnecting it.
 *
 * @param node Graph node to operate on.
 * @param input Connected input node.
 * @param pause True to pause; false to resume.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetInputPause(LND_NODE *node, LND_NODE *input, bool pause);

/** Get 1 if input's connection to node is paused, 0 if running.
 *
 * @param node Graph node to operate on.
 * @param input Connected input node.
 * @return 1 if input's connection to node is paused, 0 if running, or a negative error.
 */
LND_API int32_t LND_NodeGetInputPause(const LND_NODE *node, const LND_NODE *input);

/** Set input as mixer's timing reference; NULL clears it.
 *
 * @param mixer Mixer node.
 * @param input Connected input to use as the clock; NULL clears the clock.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetMixerClock(LND_NODE *mixer, LND_NODE *input);

/** Get mixer's clock input.
 *
 * @param mixer Mixer node.
 * @return Mixer's borrowed clock input, or NULL if unset.
 */
LND_API LND_NODE *LND_NodeGetMixerClock(const LND_NODE *mixer);

/** Copy matrix for input-to-node channel conversion; NULL restores default mixing. Rows are output
 * channels.
 *
 * @param node Graph node to operate on.
 * @param input Connected input node.
 * @param matrix Row-major output-by-input matrix to copy; NULL restores default mixing.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetInputMatrix(LND_NODE *node, LND_NODE *input, const float *matrix);

/** Set node's gain transition length to frames; zero changes gain immediately.
 *
 * @param node Graph node to operate on.
 * @param frames Gain transition length in frames; 0 changes gain immediately.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetGainRampFrames(LND_NODE *node, uint32_t frames);

/** Get node's LND_MIX mode bits.
 *
 * @param n Graph node to operate on.
 * @return Node's LND_MIX mode bits.
 */
LND_API uint32_t LND_NodeGetMixMode(const LND_NODE *n);

/** Create a processor at channels and rate from copied procs and borrowed user.
 *
 * @param procs Processor callbacks; supply exactly one of process and process_pcm.
 * @param user Borrowed callback context.
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateProcessor(const LND_PROCESSOR_PROCS *procs, void *user, uint32_t channels, uint32_t sample_rate_hz);

/** Create an interleaved F32 processor from process, user and flags at channels and rate.
 *
 * @param process Callback to install; user supplies its context.
 * @param user Borrowed callback context.
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param flags LND_PROCESSOR option bits.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateProcessorProc(LND_PROCESS_PROC process, void *user, uint32_t channels, uint32_t sample_rate_hz, uint32_t flags);

/** Get processor node's user pointer.
 *
 * @param n Graph node to operate on.
 * @return Processor node's borrowed user pointer, or NULL for a different node type.
 */
LND_API void *LND_NodeGetProcessorUser(const LND_NODE *n);

/** Get processor node's callback table.
 *
 * @param n Graph node to operate on.
 * @return Processor node's borrowed callback table, or NULL for a different node type.
 */
LND_API const LND_PROCESSOR_PROCS *LND_NodeGetProcessorProcs(const LND_NODE *n);

/** Split channels at sample_rate_hz into borrowed mono outputs.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @return Owned parent node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateChannelSplitter(uint32_t channels, uint32_t sample_rate_hz);

/** Create a node combining mono inputs into channels at sample_rate_hz.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateChannelMerger(uint32_t channels, uint32_t sample_rate_hz);

/** Connect src to merger's zero-based output channel.
 *
 * @param merger Channel merger node.
 * @param channel Zero-based destination channel in merger.
 * @param src Input node to connect; use LND_NodeDisconnect to remove it.
 * @return LND_OK or a negative error; use LND_NodeDisconnect to remove the connection.
 */
LND_API int32_t LND_NodeSetChannelMergerInput(LND_NODE *merger, uint32_t channel, LND_NODE *src);

/** Disconnect and release node; borrowed views belong to their owning object.
 *
 * @param n Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeFree(LND_NODE *n);

/** Connect src to dst, adapting rate and channels where supported.
 *
 * @param src Source node of the connection.
 * @param dst Destination node of the connection.
 * @return LND_OK or a negative error, including graph cycles.
 */
LND_API int32_t LND_NodeConnect(LND_NODE *src, LND_NODE *dst);

/** Remove the connection from src to dst.
 *
 * @param src Source node of the connection.
 * @param dst Destination node of the connection.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeDisconnect(LND_NODE *src, LND_NODE *dst);

/** Get node's LND_NODE role.
 *
 * @param n Graph node to operate on.
 * @return Node's LND_NODE role, or BUS for NULL; roles do not identify module implementations.
 */
LND_API int32_t LND_NodeGetType(const LND_NODE *n);

/** Get node's output channel count.
 *
 * @param n Graph node to operate on.
 * @return Node's output channel count, or zero if unavailable.
 */
LND_API uint32_t LND_NodeGetChannels(const LND_NODE *n);

/** Get node's output sample rate in Hz.
 *
 * @param n Graph node to operate on.
 * @return Node's output sample rate in Hz, or zero if unavailable.
 */
LND_API uint32_t LND_NodeGetSampleRateHz(const LND_NODE *n);

/** Get node's connected input count.
 *
 * @param n Graph node to operate on.
 * @return Node's connected input count, or zero if unavailable.
 */
LND_API uint32_t LND_NodeGetInputCount(const LND_NODE *n);

/** Get node's connected output count.
 *
 * @param n Graph node to operate on.
 * @return Node's connected output count, or zero if unavailable.
 */
LND_API uint32_t LND_NodeGetOutputCount(const LND_NODE *n);

/** Get node's input at zero-based index.
 *
 * @param n Graph node to operate on.
 * @param index Zero-based entry index.
 * @return Node's borrowed input at zero-based index, or NULL if out of range.
 */
LND_API LND_NODE *LND_NodeGetInput(const LND_NODE *n, uint32_t index);

/** Get node's destination at zero-based index.
 *
 * @param n Graph node to operate on.
 * @param index Zero-based entry index.
 * @return Node's borrowed destination at zero-based index, or NULL if out of range.
 */
LND_API LND_NODE *LND_NodeGetOutput(const LND_NODE *n, uint32_t index);

/** Set node parameter param to value in that parameter's units.
 *
 * @param n Graph node to operate on.
 * @param param Parameter identifier defined by the node type.
 * @param value New value in the selected parameter's units.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetParam(LND_NODE *n, int32_t param, float value);

/** Get node parameter param in its defined units.
 *
 * @param n Graph node to operate on.
 * @param param Parameter identifier defined by the node type.
 * @return Node parameter param in its defined units, or zero for an unavailable parameter/node.
 */
LND_API float LND_NodeGetParam(const LND_NODE *n, int32_t param);

/** Set node's linear gain; 1 is unity and 0 is silence.
 *
 * @param n Graph node to operate on.
 * @param gain Linear gain; 1 is unity and 0 is silence.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetGain(LND_NODE *n, float gain);

/** Get node's linear gain.
 *
 * @param n Graph node to operate on.
 * @return Node's linear gain; 1 is unity.
 */
LND_API float LND_NodeGetGain(const LND_NODE *n);

/** Set node's LND_CLIP mode.
 *
 * @param n Graph node to operate on.
 * @param mode LND_CLIP processing mode.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetClipMode(LND_NODE *n, int32_t mode);

/** Get node's LND_CLIP mode.
 *
 * @param n Graph node to operate on.
 * @return Node's LND_CLIP mode.
 */
LND_API int32_t LND_NodeGetClipMode(const LND_NODE *n);

/** Get node's render position in frames at its output rate.
 *
 * @param n Graph node to operate on.
 * @return Node's render position in frames at its output rate.
 */
LND_API uint64_t LND_NodeGetPositionFrames(const LND_NODE *n);

/** Check whether node currently contributes active playback to the graph.
 *
 * @param n Graph node to operate on.
 * @return True if node currently contributes active playback to the graph; false otherwise.
 */
LND_API bool LND_NodeIsActive(const LND_NODE *n);

/** Get or create the source view of a node.
 *
 * @param n Graph node to operate on.
 * @return The borrowed source view, or NULL on failure. The node owns it.
 */
LND_API LND_SOURCE *LND_NodeEnsureSource(LND_NODE *n);

/** Get or create the sound view of a node.
 *
 * @param n Graph node to operate on.
 * @param config Optional sound settings; NULL reuses an existing view or selects defaults.
 * @return The borrowed sound view, or NULL on failure or incompatible settings. The node owns it.
 */
LND_API LND_SOUND *LND_NodeEnsureSound(LND_NODE *n, const LND_SOUND_CONFIG *config);

#ifdef __cplusplus
}
#endif
