#pragma once

#include "lindar_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Borrowed stretch algorithm descriptor; selection uses identity and priority, not numeric IDs. */
typedef struct LND_STRETCH_BACKEND LND_STRETCH_BACKEND;

enum {
    LND_STRETCH_PARAM_TEMPO_RATIO = LND_PARAM_USER, /**< Tempo multiplier without pitch change; 1 is unchanged. */
    LND_STRETCH_PARAM_PITCH_RATIO, /**< Pitch multiplier without duration change; 1 is unchanged. */
    LND_STRETCH_PARAM_RATE_RATIO, /**< Speed and pitch multiplier; 1 is unchanged. */
};

/** Stretch ratios and optional backend; zero ratios select unity and NULL backend selects automatically. */
typedef struct LND_STRETCH_CONFIG {
    float tempo_ratio; /**< Tempo multiplier without pitch change; 0 defaults to 1. */
    float pitch_ratio; /**< Pitch multiplier without tempo change; 0 defaults to 1. */
    float rate_ratio; /**< Speed and pitch multiplier; 0 defaults to 1. */
    const LND_STRETCH_BACKEND *backend; /**< Borrowed algorithm descriptor; NULL selects by priority. */
} LND_STRETCH_CONFIG;

/** Copied common stretch state with a borrowed backend identity. */
typedef struct LND_STRETCH_INFO {
    uint64_t input_frames; /**< Total input frames consumed. */
    uint64_t output_frames; /**< Total output frames produced. */
    double duration_ratio; /**< Output duration divided by input duration. */
    uint32_t initial_latency_frames; /**< Initial processing latency in frames. */
    uint32_t buffered_output_frames; /**< Processed frames waiting to be read. */
    uint32_t unprocessed_frames; /**< Input frames still held by the algorithm. */
    const LND_STRETCH_BACKEND *backend; /**< Borrowed descriptor of the selected algorithm. */
    int32_t error; /**< LND_OK or the recorded negative processing error. */
    bool input_ended; /**< No more input is expected; output may still drain. */
} LND_STRETCH_INFO;

/** Get the number of available stretch implementations.
 *
 * @return The number of available stretch implementations.
 */
LND_API uint32_t LND_StretchBackendGetCount(void);

/** Get a stretch backend at zero-based index.
 *
 * @param index Zero-based entry index.
 * @return A borrowed stretch backend at zero-based index, or NULL if out of range.
 */
LND_API const LND_STRETCH_BACKEND *LND_StretchBackendGet(uint32_t index);

/** Get the stretch backend matching name.
 *
 * @param name Implementation name.
 * @return The borrowed stretch backend matching name, or NULL if absent.
 */
LND_API const LND_STRETCH_BACKEND *LND_StretchBackendFind(const char *name);

/** Get backend's name.
 *
 * @param backend Borrowed stretch backend descriptor.
 * @return Backend's borrowed name, or NULL for an invalid descriptor.
 */
LND_API const char *LND_StretchBackendGetName(const LND_STRETCH_BACKEND *backend);

/** Set backend's nonnegative selection priority; higher wins and zero disables automatic selection.
 *
 * @param backend Borrowed stretch backend descriptor.
 * @param priority Nonnegative selection priority; higher wins, zero disables automatic selection.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_StretchBackendSetPriority(const LND_STRETCH_BACKEND *backend, int32_t priority);

/** Get backend's selection priority.
 *
 * @param backend Borrowed stretch backend descriptor.
 * @return Backend's selection priority.
 */
LND_API int32_t LND_StretchBackendGetPriority(const LND_STRETCH_BACKEND *backend);

/** Create a stretch node at channels and rate using optional config and backend selection.
 * Pausing upstream can leave buffered output audible. Allocation behaviour depends on the backend.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateStretch(uint32_t channels, uint32_t sample_rate_hz, const LND_STRETCH_CONFIG *config);

/** Create a pitch shifter at channels and rate; pitch_ratio 1 is unchanged.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param pitch_ratio Pitch multiplier; 1 is unchanged.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreatePitch(uint32_t channels, uint32_t sample_rate_hz, float pitch_ratio);

/** Create a tempo changer at channels and rate; tempo_ratio 1 is unchanged.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param tempo_ratio Tempo multiplier; 1 is unchanged.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateTempo(uint32_t channels, uint32_t sample_rate_hz, float tempo_ratio);

/** Get node's stretch implementation descriptor.
 *
 * @param node Graph node to operate on.
 * @return Node's borrowed stretch implementation descriptor, or NULL for another node type.
 */
LND_API const LND_STRETCH_BACKEND *LND_NodeGetStretchBackend(const LND_NODE *node);

/** Copy node's stretch counters, buffering and state into info.
 *
 * @param node Graph node to operate on.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetStretchInfo(const LND_NODE *node, LND_STRETCH_INFO *info);

/** Clear node's stretch history and end state for new input.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeResetStretch(LND_NODE *node);

/** Mark node's stretch input complete and drain its tail; Reset is required for reuse.
 * Drains only input already supplied to this processor. To retain remaining upstream audio, end
 * the producer instead and keep rendering the chain.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeEndStretchInput(LND_NODE *node);

/** Set node's pitch shift in semitones; zero is unchanged.
 *
 * @param node Graph node to operate on.
 * @param semitones Pitch shift in semitones; 0 is unchanged.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetStretchPitchSemitones(LND_NODE *node, float semitones);

/** Get node's pitch shift in semitones.
 *
 * @param node Graph node to operate on.
 * @return Node's pitch shift in semitones; zero is unchanged.
 */
LND_API float LND_NodeGetStretchPitchSemitones(const LND_NODE *node);

/** Set node's tempo change; 0 is unchanged and 100 doubles tempo.
 *
 * @param node Graph node to operate on.
 * @param percent Tempo change in percent; 0 is unchanged and 100 doubles tempo.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetStretchTempoChangePercent(LND_NODE *node, float percent);

/** Set node's rate change; 0 is unchanged and 100 doubles speed and pitch.
 *
 * @param node Graph node to operate on.
 * @param percent Speed/pitch change in percent; 0 is unchanged and 100 doubles both.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetStretchRateChangePercent(LND_NODE *node, float percent);

#ifdef __cplusplus
}
#endif
