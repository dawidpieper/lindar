#pragma once

#include "lindar_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LND_SLIDE_LINEAR = 0, /**< Interpolate parameter value linearly. */
    LND_SLIDE_LOGARITHMIC = 1, /**< Interpolate positive values logarithmically. */
};

/** Parameter transition timing in node frames; curve controls interpolation. */
typedef struct LND_SLIDE_CONFIG {
    uint64_t duration_frames; /**< Transition duration in node frames. */
    uint32_t step_frames; /**< Parameter update interval in frames; zero uses the default. */
    int32_t curve; /**< LND_SLIDE_LINEAR or LOGARITHMIC. */
} LND_SLIDE_CONFIG;

/** Copied parameter transition progress and current value. */
typedef struct LND_SLIDE_STATE {
    float value; /**< Parameter value in its defined units. */
    float target; /**< Requested final parameter value. */
    uint64_t elapsed_frames; /**< Frames elapsed since the slide started. */
    uint64_t duration_frames; /**< Transition duration in node frames. */
    bool active; /**< The parameter transition is still running. */
} LND_SLIDE_STATE;

/** Move node's param towards target using config's duration and curve.
 * The first nonzero-duration slide for each node/parameter allocates retained control storage.
 *
 * @param node Graph node to operate on.
 * @param param Parameter identifier defined by the node type.
 * @param target Target value in the selected parameter's units.
 * @param config Required settings, borrowed during the call.
 * @return LND_OK or a negative error; parameter must support sliding.
 */
LND_API int32_t LND_NodeSlideParam(LND_NODE *node, int32_t param, float target, const LND_SLIDE_CONFIG *config);

/** Cancel node's slide for param, retaining its current value.
 *
 * @param node Graph node to operate on.
 * @param param Parameter identifier defined by the node type.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeCancelSlide(LND_NODE *node, int32_t param);

/** Check whether node's param has an active slide.
 *
 * @param node Graph node to operate on.
 * @param param Parameter identifier defined by the node type.
 * @return True if node's param has an active slide; false otherwise.
 */
LND_API bool LND_NodeIsSliding(const LND_NODE *node, int32_t param);

/** Copy node's slide for param into state.
 *
 * @param node Graph node to operate on.
 * @param param Parameter identifier defined by the node type.
 * @param state Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_NodeGetSlideState(const LND_NODE *node, int32_t param, LND_SLIDE_STATE *state);

#ifdef __cplusplus
}
#endif
