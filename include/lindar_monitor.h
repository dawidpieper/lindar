#pragma once
#include "lindar_graph.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Accumulated render timings; self time excludes upstream nodes, render time includes them. */
typedef struct LND_NODE_STATS {
    uint64_t calls; /**< Number of timed render calls. */
    uint64_t frames; /**< Frames processed by timed calls. */
    uint64_t render_ns; /**< Accumulated render time including upstream work, in nanoseconds. */
    uint64_t self_ns; /**< Accumulated local processing time, in nanoseconds. */
    uint64_t peak_ns; /**< Longest measured render call, in nanoseconds. */
    double render_percent; /**< Render time as a percentage of processed audio duration. */
    double self_percent; /**< Local processing time as a percentage of processed audio duration. */
    bool enabled; /**< Timing is currently enabled for this node. */
} LND_NODE_STATS;

/** Enable or disable timing for node; timing adds per-render overhead.
 *
 * @param node Graph node to operate on.
 * @param enabled True to enable; false to disable.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeSetMonitoring(LND_NODE *node, bool enabled);

/** Copy node's accumulated timing and frame counters into stats.
 *
 * @param node Graph node to operate on.
 * @param stats Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetStats(const LND_NODE *node, LND_NODE_STATS *stats);

/** Clear node's accumulated monitoring counters.
 *
 * @param node Graph node to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeResetStats(LND_NODE *node);
#ifdef __cplusplus
}
#endif
