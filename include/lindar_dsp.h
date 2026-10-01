#pragma once

#include "lindar_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LND_BIQUAD_LOWPASS = 0, /**< Pass frequencies below the cut-off. */
    LND_BIQUAD_HIGHPASS = 1, /**< Pass frequencies above the cut-off. */
    LND_BIQUAD_BANDPASS = 2, /**< Pass a band around the centre frequency. */
    LND_BIQUAD_NOTCH = 3, /**< Reject a band around the centre frequency. */
    LND_BIQUAD_PEAKING = 4, /**< Boost/cut a band around the centre frequency. */
    LND_BIQUAD_LOWSHELF = 5, /**< Boost/cut frequencies below the shelf. */
    LND_BIQUAD_HIGHSHELF = 6, /**< Boost/cut frequencies above the shelf. */
    LND_BIQUAD_ALLPASS = 7, /**< Change phase while preserving amplitude. */
};

enum {
    LND_PAN_STEREO = 0, /**< Pan stereo channels with crossfeed. */
    LND_PAN_MONO = 1, /**< Downmix to mono, then pan into stereo. */
};

enum {
    LND_DSP_PARAM_TYPE = LND_PARAM_USER, /**< Biquad response selector. */
    LND_DSP_PARAM_FREQUENCY_HZ, /**< Filter centre/cut-off frequency in Hz. */
    LND_DSP_PARAM_Q, /**< Dimensionless filter Q. */
    LND_DSP_PARAM_GAIN_DB, /**< Filter gain in decibels. */
    LND_DSP_PARAM_DELAY_MS, /**< Delay time in milliseconds. */
    LND_DSP_PARAM_FEEDBACK, /**< Linear delay feedback coefficient. */
    LND_DSP_PARAM_MIX, /**< Wet proportion in [0, 1]. */
    LND_DSP_PARAM_PAN, /**< Pan in [-1, 1]; -1 is left and +1 is right. */
    LND_DSP_PARAM_PAN_MODE, /**< LND_PAN_STEREO or LND_PAN_MONO. */
    LND_DSP_PARAM_DECAY_MS, /**< Meter decay time in milliseconds. */
    LND_DSP_PARAM_RATE_RATIO, /**< Varispeed multiplier; 1 is unchanged. */
};

/** Required biquad parameters; frequency is in Hz, Q is dimensionless and gain is in dB. */
typedef struct LND_BIQUAD_CONFIG {
    int32_t type; /**< LND_BIQUAD filter response. */
    float frequency_hz; /**< Filter centre/cut-off frequency in Hz. */
    float q; /**< Dimensionless filter Q (resonance). */
    float gain_db; /**< Gain in decibels; 0 is unity. */
} LND_BIQUAD_CONFIG;

/** Required delay parameters; max_delay_ms fixes allocated history capacity. */
typedef struct LND_DELAY_CONFIG {
    float max_delay_ms; /**< Maximum allocated delay history in milliseconds. */
    float delay_ms; /**< Delay time in milliseconds. */
    float feedback; /**< Linear feedback coefficient. */
    float mix; /**< Wet proportion in [0, 1]; zero is dry. */
} LND_DELAY_CONFIG;

/** Create a filter for channels and rate using required config.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Required settings, borrowed during the call.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateBiquad(uint32_t channels, uint32_t sample_rate_hz, const LND_BIQUAD_CONFIG *config);

/** Create a delay for channels and rate using required config and its maximum delay.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Required settings, borrowed during the call.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateDelay(uint32_t channels, uint32_t sample_rate_hz, const LND_DELAY_CONFIG *config);

/** Create a stereo panner at sample_rate_hz using pan (-1 left, +1 right) and mode.
 *
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param pan Pan in [-1, 1]; -1 is left and +1 is right.
 * @param mode LND_PAN_STEREO or LND_PAN_MONO.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreatePanner(uint32_t sample_rate_hz, float pan, int32_t mode);

/** Create a rate/pitch-changing node for channels and rate_ratio at sample_rate_hz with
 * resample_quality.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param rate_ratio Rate and pitch multiplier; 1 is unchanged.
 * @param resample_quality LND_RESAMPLE quality selector.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateVarispeed(uint32_t channels, uint32_t sample_rate_hz, float rate_ratio, uint32_t resample_quality);

/** Get node's original sample rate before varispeed adjustment.
 *
 * @param node Graph node to operate on.
 * @return Node's original sample rate before varispeed adjustment, or zero for another node type.
 */
LND_API uint32_t LND_NodeGetVarispeedBaseSampleRateHz(const LND_NODE *node);

/** Clear node's resampling history without changing its rate ratio.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeResetVarispeed(LND_NODE *node);

/** Create a pass-through level meter for channels and rate with decay_ms.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param decay_ms Level decay time in milliseconds.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateMeter(uint32_t channels, uint32_t sample_rate_hz, float decay_ms);

/** Get meter's linear peak for zero-based channel.
 *
 * @param meter Level meter node.
 * @param channel Zero-based channel index.
 * @return Meter's linear peak for zero-based channel, or zero if unavailable.
 */
LND_API float LND_NodeGetMeterPeak(const LND_NODE *meter, uint32_t channel);

/** Get meter's linear RMS level for zero-based channel.
 *
 * @param meter Level meter node.
 * @param channel Zero-based channel index.
 * @return Meter's linear RMS level for zero-based channel, or zero if unavailable.
 */
LND_API float LND_NodeGetMeterRms(const LND_NODE *meter, uint32_t channel);

#ifdef __cplusplus
}
#endif
