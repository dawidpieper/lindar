#pragma once

#include "lindar_stretch.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LND_SOUNDTOUCH_PARAM_TEMPO_RATIO = LND_STRETCH_PARAM_TEMPO_RATIO, /**< Tempo multiplier without pitch change; 1 is unchanged. */
    LND_SOUNDTOUCH_PARAM_PITCH_RATIO = LND_STRETCH_PARAM_PITCH_RATIO, /**< Pitch multiplier without duration change; 1 is unchanged. */
    LND_SOUNDTOUCH_PARAM_RATE_RATIO = LND_STRETCH_PARAM_RATE_RATIO, /**< Speed and pitch multiplier; 1 is unchanged. */
};

enum {
    LND_SOUNDTOUCH_AA_FILTER, /**< Setting: enable anti-alias filtering (0/1). */
    LND_SOUNDTOUCH_AA_FILTER_LENGTH, /**< Filter length: 8 to 128 taps, in steps of 8. */
    LND_SOUNDTOUCH_QUICK_SEEK, /**< Setting: use a faster overlap search (0/1). */
    LND_SOUNDTOUCH_SEQUENCE_MS, /**< Sequence length: 8 to 200 ms; zero selects automatically. */
    LND_SOUNDTOUCH_SEEKWINDOW_MS, /**< Search window: 1 to 100 ms; zero selects automatically. */
    LND_SOUNDTOUCH_OVERLAP_MS, /**< Overlap length: 1 to 64 ms. */
};

enum {
    LND_SOUNDTOUCH_QUICK = 1u << 0, /**< Enable the faster overlap search at creation. */
    LND_SOUNDTOUCH_NO_AA = 1u << 1, /**< Disable anti-alias filtering at creation. */
};

/** SoundTouch settings copied at creation; zero ratios select unity and zero tuning sizes use defaults. */
typedef struct LND_SOUNDTOUCH_CONFIG {
    float tempo_ratio; /**< Tempo multiplier without pitch change; 0 defaults to 1. */
    float pitch_ratio; /**< Pitch multiplier without tempo change; 0 defaults to 1. */
    float rate_ratio; /**< Speed and pitch multiplier; 0 defaults to 1. */
    uint32_t sequence_ms; /**< Processing sequence length in milliseconds; zero uses the algorithm default. */
    uint32_t seekwindow_ms; /**< Overlap search window in milliseconds; zero uses the algorithm default. */
    uint32_t overlap_ms; /**< Overlap length in milliseconds; zero uses the algorithm default. */
    uint32_t aa_filter_length; /**< Anti-alias filter length in taps; zero uses the default. */
    uint32_t flags; /**< LND_SOUNDTOUCH_QUICK and NO_AA bits. */
} LND_SOUNDTOUCH_CONFIG;

/** Copied SoundTouch processing counters and buffering state. */
typedef struct LND_SOUNDTOUCH_INFO {
    uint64_t input_frames; /**< Total input frames consumed. */
    uint64_t output_frames; /**< Total output frames produced. */
    double duration_ratio; /**< Output duration divided by input duration. */
    uint32_t initial_latency_frames; /**< Initial processing latency in frames. */
    uint32_t input_sequence_frames; /**< Algorithm input sequence length in frames. */
    uint32_t output_sequence_frames; /**< Algorithm output sequence length in frames. */
    uint32_t buffered_output_frames; /**< Processed frames waiting to be read. */
    uint32_t unprocessed_frames; /**< Input frames still held by the algorithm. */
    bool input_ended; /**< No more input is expected; output may still drain. */
    int32_t error; /**< LND_OK or the recorded negative processing error. */
} LND_SOUNDTOUCH_INFO;

/** Create a SoundTouch node at channels and rate with optional config.
 * Processing, setting changes and reset may allocate through SoundTouch.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateSoundTouch(uint32_t channels, uint32_t sample_rate_hz, const LND_SOUNDTOUCH_CONFIG *config);

/** Set node's SoundTouch pitch shift in semitones; zero is unchanged.
 *
 * @param node Graph node to operate on.
 * @param semitones Pitch shift in semitones; 0 is unchanged.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetSoundTouchPitchSemitones(LND_NODE *node, float semitones);

/** Get node's SoundTouch pitch shift in semitones.
 *
 * @param node Graph node to operate on.
 * @return Node's SoundTouch pitch shift in semitones; zero is unchanged.
 */
LND_API float LND_NodeGetSoundTouchPitchSemitones(const LND_NODE *node);

/** Set node's SoundTouch tempo change; 0 is unchanged and 100 doubles tempo.
 *
 * @param node Graph node to operate on.
 * @param percent Tempo change in percent; 0 is unchanged and 100 doubles tempo.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetSoundTouchTempoChangePercent(LND_NODE *node, float percent);

/** Set node's SoundTouch rate change; 0 is unchanged and 100 doubles speed and pitch.
 *
 * @param node Graph node to operate on.
 * @param percent Speed/pitch change in percent; 0 is unchanged and 100 doubles both.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetSoundTouchRateChangePercent(LND_NODE *node, float percent);

/** Set node's SoundTouch setting to value in that setting's units.
 * May allocate through SoundTouch.
 *
 * @param node Graph node to operate on.
 * @param setting SoundTouch setting selector, such as LND_SOUNDTOUCH_AA_FILTER.
 * @param value New value in the selected parameter's units.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetSoundTouchSetting(LND_NODE *node, int32_t setting, int32_t value);

/** Get node's SoundTouch setting value.
 *
 * @param node Graph node to operate on.
 * @param setting SoundTouch setting selector, such as LND_SOUNDTOUCH_AA_FILTER.
 * @return Node's SoundTouch setting value, or a negative error.
 */
LND_API int32_t LND_NodeGetSoundTouchSetting(const LND_NODE *node, int32_t setting);

/** Copy node's SoundTouch counters, buffering and state into info.
 *
 * @param node Graph node to operate on.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetSoundTouchInfo(const LND_NODE *node, LND_SOUNDTOUCH_INFO *info);

/** Clear node's SoundTouch history and end state for new input.
 * May allocate through SoundTouch.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeResetSoundTouch(LND_NODE *node);

/** Mark node's SoundTouch input complete and drain its tail; Reset is required for reuse.
 * Only already-supplied input drains; see LND_NodeEndStretchInput for upstream completion.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeEndSoundTouchInput(LND_NODE *node);

/** Get the SoundTouch version string.
 *
 * @return The static SoundTouch version string; do not free it.
 */
LND_API const char *LND_SoundTouchGetVersion(void);

#ifdef __cplusplus
}
#endif
