#pragma once

#include "lindar_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LND_EFFECT_ECHO = 0, /**< Feedback delay with dry/wet mixing. */
    LND_EFFECT_REVERB, /**< Reverberation with room and damping controls. */
    LND_EFFECT_CHORUS, /**< Modulated delays producing a chorus. */
    LND_EFFECT_FLANGER, /**< Short modulated feedback delay. */
    LND_EFFECT_PHASER, /**< Modulated all-pass filter cascade. */
    LND_EFFECT_DISTORTION, /**< Nonlinear saturation/distortion. */
    LND_EFFECT_COMPRESSOR, /**< Level-dependent dynamic range compression. */
    LND_EFFECT_AUTOWAH, /**< Envelope/modulation-controlled filtering. */
    LND_EFFECT_PEAK_EQ, /**< Peaking equaliser. */
    LND_EFFECT_BIQUAD, /**< Configurable second-order filter. */
    LND_EFFECT_DYNAMIC_GAIN, /**< Automatic gain adjustment towards a target peak. */
    LND_EFFECT_ROTATION, /**< Periodic stereo rotation. */
    LND_EFFECT_COUNT /**< Number of effect algorithms; not an effect type. */
};

enum {
    LND_EFFECT_PARAM_DRY = LND_PARAM_USER, /**< Dry gain: [-2, 2]; distortion [-5, 5], reverb [0, 1]. */
    LND_EFFECT_PARAM_WET, /**< Processed gain: [-2, 2]; distortion [-5, 5], reverb [0, 3]. */
    LND_EFFECT_PARAM_FEEDBACK, /**< Feedback coefficient in [-1, 1]. */
    LND_EFFECT_PARAM_DELAY_MS, /**< Delay in [0.001, 3600000] ms, within reserved history. */
    LND_EFFECT_PARAM_STEREO, /**< Stereo processing: exactly 0 or 1; not interpolated. */
    LND_EFFECT_PARAM_MIN_DELAY_MS, /**< Minimum delay in [0.001, 6000] ms, within reserved history. */
    LND_EFFECT_PARAM_MAX_DELAY_MS, /**< Maximum delay in [0.001, 6000] ms, within reserved history. */
    LND_EFFECT_PARAM_SWEEP_MS_PER_SECOND, /**< Delay sweep in [0, 1000] ms/s; zero stops modulation. */
    LND_EFFECT_PARAM_RATE_HZ, /**< Phaser/AutoWah rate in [0, 10] Hz; rotation [-1000, 1000] Hz. */
    LND_EFFECT_PARAM_ROOM_SIZE, /**< Reverb room size in [0, 1]. */
    LND_EFFECT_PARAM_DAMPING, /**< Reverb high-frequency damping in [0, 1]. */
    LND_EFFECT_PARAM_WIDTH, /**< Stereo spread in [0, 1]. */
    LND_EFFECT_PARAM_FREEZE, /**< Freeze reverb history: exactly 0 or 1; not interpolated. */
    LND_EFFECT_PARAM_DRIVE, /**< Distortion drive in [0, 5]. */
    LND_EFFECT_PARAM_OUTPUT_GAIN, /**< Processed signal gain in [0, 2]. */
    LND_EFFECT_PARAM_GAIN_DB, /**< Gain in [-120, 120] dB. */
    LND_EFFECT_PARAM_THRESHOLD_DB, /**< Compressor threshold in [-120, 0] dBFS. */
    LND_EFFECT_PARAM_RATIO, /**< Compression ratio in [1, FLT_MAX]; 1 leaves only makeup gain. */
    LND_EFFECT_PARAM_ATTACK_MS, /**< Envelope attack in [0.01, 1000] ms. */
    LND_EFFECT_PARAM_RELEASE_MS, /**< Envelope release in [0.01, 5000] ms. */
    LND_EFFECT_PARAM_RANGE_OCTAVES, /**< Filter sweep range in [0, 10] octaves. */
    LND_EFFECT_PARAM_FREQUENCY_HZ, /**< Frequency in [0.001, 384000] Hz; capped at 0.49 of rate for filters, 0.45 for Phaser/AutoWah. */
    LND_EFFECT_PARAM_BANDWIDTH_OCTAVES, /**< Bandwidth in [0, 10] octaves; positive values override Q. */
    LND_EFFECT_PARAM_Q, /**< Filter Q in [0, 1000]; zero selects 1/sqrt(2). */
    LND_EFFECT_PARAM_FILTER_TYPE, /**< LND_EFFECT_FILTER selector; not interpolated. */
    LND_EFFECT_PARAM_SLOPE, /**< Shelf slope in [0, 1], with an effective minimum of 0.0001. */
    LND_EFFECT_PARAM_TARGET_PEAK, /**< DynamicGain target amplitude in [0.001, 1]. */
    LND_EFFECT_PARAM_QUIET_THRESHOLD, /**< Amplitude in [0, 1] below which DynamicGain does not increase gain. */
    LND_EFFECT_PARAM_ADJUST_SPEED, /**< Dimensionless DynamicGain adjustment speed in [0, 1]; zero holds gain. */
    LND_EFFECT_PARAM_GAIN, /**< DynamicGain starting/current gain in [0, 1000000]; GetParam returns the requested value. */
    LND_EFFECT_PARAM_GAIN_DELAY_MS, /**< Delay before gain growth in [0, 3600000] ms; does not delay PCM. */
    LND_EFFECT_PARAM_BYPASS, /**< Bypass: exactly 0 or 1; changes clear history and are not interpolated. */
    LND_EFFECT_PARAM_END /**< End of the effect parameter ID range; not a settable parameter. */
};

enum {
    LND_EFFECT_FILTER_LOWPASS = 0, /**< Pass frequencies below the cut-off. */
    LND_EFFECT_FILTER_HIGHPASS, /**< Pass frequencies above the cut-off. */
    LND_EFFECT_FILTER_BANDPASS, /**< Pass a band around the centre frequency. */
    LND_EFFECT_FILTER_NOTCH, /**< Reject a band around the centre frequency. */
    LND_EFFECT_FILTER_PEAKING, /**< Boost/cut a band around the centre frequency. */
    LND_EFFECT_FILTER_LOWSHELF, /**< Boost/cut frequencies below the shelf. */
    LND_EFFECT_FILTER_HIGHSHELF, /**< Boost/cut frequencies above the shelf. */
    LND_EFFECT_FILTER_ALLPASS, /**< Change phase while preserving amplitude. */
    LND_EFFECT_FILTER_BANDPASS_Q /**< Band-pass response with Q-dependent peak gain. */
};

enum {
    LND_EFFECT_SELECT_CHANNELS = 1u << 0, /**< Use config.channel_mask instead of processing every channel. */
    LND_EFFECT_NO_TAIL = 1u << 1 /**< Stop output at input EOF without rendering an effect tail. */
};

/** One initial effect parameter/value pair, in the units defined by its ID. */
typedef struct LND_EFFECT_PARAM {
    int32_t id; /**< LND_EFFECT_PARAM identifier. */
    float value; /**< Parameter value in its defined units. */
} LND_EFFECT_PARAM;

/** Effect settings; copied parameter entries override defaults, including explicit zero.
 * Duplicate, unsupported, non-finite or out-of-range values are rejected.
 */
typedef struct LND_EFFECT_CONFIG {
    const LND_EFFECT_PARAM *params; /**< Array of initial parameter/value pairs. */
    uint32_t param_count; /**< Number of entries in params. */
    uint32_t flags; /**< LND_EFFECT_SELECT_CHANNELS and NO_TAIL bits. */
    uint32_t channel_mask; /**< Active channel bits when SELECT_CHANNELS is set; otherwise all channels. */
    float max_delay_ms; /**< Reserved delay in [0, 3600000] ms; zero selects 1000 for echo, 100 otherwise. */
    float tail_ms; /**< Tail cap in [0, 3600000] ms; zero selects 10000. Set at EOF; parameter changes do not extend it. */
} LND_EFFECT_CONFIG;

/** Copied effect counters and tail state; output may continue after input ends. */
typedef struct LND_EFFECT_INFO {
    uint64_t input_frames; /**< Total input frames consumed. */
    uint64_t output_frames; /**< Total output frames produced. */
    uint64_t tail_remaining_frames; /**< Frames still allowed after input EOF. */
    uint64_t tail_limit_frames; /**< Configured maximum tail length. */
    uint32_t channel_mask; /**< Bit i selects zero-based channel i. */
    int32_t type; /**< LND_EFFECT algorithm identifier. */
    int32_t error; /**< LND_OK or the recorded negative processing error. */
    bool input_ended; /**< No more input is expected; output may still drain. */
    bool bypassed; /**< Effect processing is bypassed. */
} LND_EFFECT_INFO;

/** Create an effect with reserved history; rendering, parameter changes and reset do not allocate.
 * Processes F32 without limiting output to [-1, 1]. WAITING freezes state; EOF feeds zeros until
 * the bounded tail ends. Tails accumulate through a chain.
 *
 * @param channels 1 to 32 channels; zero selects the context default.
 * @param sample_rate_hz 8000 to 384000 Hz; zero selects the context default.
 * @param type LND_EFFECT type selector.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateEffect(uint32_t channels, uint32_t sample_rate_hz, int32_t type, const LND_EFFECT_CONFIG *config);

/** Create an echo effect.
 * Stereo mode couples selected adjacent channel pairs; unpaired channels run as mono.
 * Default LND_EFFECT_PARAM values: DRY=1, WET=0.5, FEEDBACK=0.3, DELAY_MS=250, STEREO=1.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateEcho(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create a reverb effect.
 * Couples selected adjacent channel pairs; unpaired channels run as mono.
 * Default LND_EFFECT_PARAM values: DRY=1, WET=0.5, ROOM_SIZE=0.5, DAMPING=0.5, WIDTH=1, FREEZE=0.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateReverb(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create a chorus effect.
 * Default LND_EFFECT_PARAM values: DRY=1, WET=0.5, FEEDBACK=0, MIN_DELAY_MS=5, MAX_DELAY_MS=30, SWEEP_MS_PER_SECOND=20.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateChorus(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create a flanger effect.
 * Default LND_EFFECT_PARAM values: DRY=1, WET=0.35, FEEDBACK=0.35, MIN_DELAY_MS=0.1, MAX_DELAY_MS=5, SWEEP_MS_PER_SECOND=5.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateFlanger(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create a phaser effect.
 * Default LND_EFFECT_PARAM values: DRY=1, WET=0.5, FEEDBACK=0, RATE_HZ=1, RANGE_OCTAVES=4, FREQUENCY_HZ=500.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreatePhaser(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create a distortion effect.
 * No oversampling is applied.
 * Default LND_EFFECT_PARAM values: DRIVE=1, DRY=0.8, WET=0.2, FEEDBACK=0, OUTPUT_GAIN=1.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateDistortion(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create a compressor effect.
 * Uses a hard knee and peak detection linked across selected channels.
 * Default LND_EFFECT_PARAM values: GAIN_DB=0, THRESHOLD_DB=-12, RATIO=4, ATTACK_MS=10, RELEASE_MS=100.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateCompressor(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create an auto-wah effect.
 * Default LND_EFFECT_PARAM values: DRY=0.5, WET=0.5, FEEDBACK=0.2, RATE_HZ=2, RANGE_OCTAVES=4, FREQUENCY_HZ=200.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateAutoWah(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create a peak equaliser effect.
 * Default LND_EFFECT_PARAM values: BANDWIDTH_OCTAVES=1, Q=0, FREQUENCY_HZ=1000, GAIN_DB=0.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreatePeakEqualizer(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create a biquad effect.
 * Default LND_EFFECT_PARAM values: FILTER_TYPE=LOWPASS, FREQUENCY_HZ=1000, GAIN_DB=0, BANDWIDTH_OCTAVES=0, Q=0.707, SLOPE=1.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateBiquadEffect(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create a dynamic gain effect.
 * Detection links selected channels.
 * Default LND_EFFECT_PARAM values: TARGET_PEAK=0.95, QUIET_THRESHOLD=0.02, ADJUST_SPEED=0.5, GAIN=1, GAIN_DELAY_MS=0.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateDynamicGain(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Create a rotation effect.
 * Leaves mono and unpaired channels unchanged.
 * Default LND_EFFECT_PARAM values: RATE_HZ=0.2.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateRotation(uint32_t channels, uint32_t sample_rate_hz, const LND_EFFECT_CONFIG *config);

/** Copy node's effect counters, tail and state into info.
 *
 * @param node Graph node to operate on.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetEffectInfo(const LND_NODE *node, LND_EFFECT_INFO *info);

/** Clear effect history, modulation, errors, counters and EOF; retain parameters and channel mask.
 * Does not seek upstream or cancel slides.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeResetEffect(LND_NODE *node);

/** Apply the channel mask and clear history; unselected channels pass through and do not drive detectors.
 *
 * @param node Graph node to operate on.
 * @param channel_mask Bit mask of selected channels.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetEffectChannelMask(LND_NODE *node, uint32_t channel_mask);

#ifdef __cplusplus
}
#endif
