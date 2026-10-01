#pragma once

#include "lindar_stretch.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LND_BUNGEE_PARAM_TEMPO_RATIO = LND_STRETCH_PARAM_TEMPO_RATIO, /**< Tempo multiplier without pitch change; 1 is unchanged. */
    LND_BUNGEE_PARAM_PITCH_RATIO = LND_STRETCH_PARAM_PITCH_RATIO, /**< Pitch multiplier without duration change; 1 is unchanged. */
    LND_BUNGEE_PARAM_RATE_RATIO = LND_STRETCH_PARAM_RATE_RATIO, /**< Speed and pitch multiplier; 1 is unchanged. */
};

enum {
    LND_BUNGEE_RESAMPLE_AUTO = 0, /**< Let Bungee select resampling placement. */
    LND_BUNGEE_RESAMPLE_INPUT, /**< Resample before grain processing. */
    LND_BUNGEE_RESAMPLE_OUTPUT, /**< Resample after grain processing. */
};

/** Bungee settings; zero ratios select unity. Settings are copied at node creation. */
typedef struct LND_BUNGEE_CONFIG {
  float tempo_ratio; /**< Tempo multiplier without pitch change; 0 defaults to 1. */
  float pitch_ratio; /**< Pitch multiplier without tempo change; 0 defaults to 1. */
  float rate_ratio; /**< Speed and pitch multiplier; 0 defaults to 1. */
  int32_t grain_adjust; /**< Grain-size adjustment from -1 to 1; zero uses Bungee's default. */
  int32_t resample_mode; /**< LND_BUNGEE_RESAMPLE_AUTO, INPUT or OUTPUT. */
} LND_BUNGEE_CONFIG;

/** Copied Bungee processing counters and buffering state. */
typedef struct LND_BUNGEE_INFO {
  uint64_t input_frames; /**< Total input frames consumed. */
  uint64_t output_frames; /**< Total output frames produced. */
  double duration_ratio; /**< Output duration divided by input duration. */
  uint32_t initial_latency_frames; /**< Initial processing latency in frames. */
  uint32_t grain_frames; /**< Current grain length in frames. */
  uint32_t hop_frames; /**< Distance between grains in frames. */
  uint32_t max_input_frames; /**< Maximum input request made by the algorithm. */
  uint32_t buffered_output_frames; /**< Processed frames waiting to be read. */
  uint32_t unprocessed_frames; /**< Input frames still held by the algorithm. */
  bool input_ended; /**< No more input is expected; output may still drain. */
  int32_t error; /**< LND_OK or the recorded negative processing error. */
} LND_BUNGEE_INFO;

/** Create a Bungee node at channels and rate with optional config.
 * Construction and reset allocate through Bungee; normal processing uses prepared buffers.
 *
 * @param channels Number of PCM channels.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @param config Settings to apply; NULL selects defaults.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateBungee(uint32_t channels,
                                       uint32_t sample_rate_hz,
                                       const LND_BUNGEE_CONFIG *config);

/** Copy node's Bungee counters, buffering and state into info.
 *
 * @param node Graph node to operate on.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetBungeeInfo(const LND_NODE *node, LND_BUNGEE_INFO *info);

/** Clear node's Bungee history and end state for new input.
 * Allocates through Bungee.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeResetBungee(LND_NODE *node);

/** Mark node's Bungee input complete and drain its tail; Reset is required for reuse.
 * Only already-supplied input drains; see LND_NodeEndStretchInput for upstream completion.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeEndBungeeInput(LND_NODE *node);

/** Get the Bungee version string.
 *
 * @return The static Bungee version string; do not free it.
 */
LND_API const char *LND_BungeeGetVersion(void);

#ifdef __cplusplus
}
#endif
