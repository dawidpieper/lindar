#pragma once

#include "lindar_graph.h"
#include "lindar_output.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opened device stream and its graph node; close owned instances with DeviceInstanceClose. */
typedef struct LND_DEVICE_INSTANCE LND_DEVICE_INSTANCE;

/** Create a sink borrowing output and clocked by instance, or the default for NULL.
 *
 * @param output Borrowed encoder output; retain it until the sink is freed.
 * @param instance Clock device instance; NULL selects the default output instance.
 * @return Owned node or NULL; release with LND_NodeFree.
 */
LND_API LND_NODE *LND_NodeCreateSink(LND_OUTPUT *output, LND_DEVICE_INSTANCE *instance);

/** Get sink's encoder output.
 *
 * @param sink Encoder sink node.
 * @return Sink's borrowed encoder output, or NULL for a different node type.
 */
LND_API LND_OUTPUT *LND_NodeGetSinkOutput(const LND_NODE *sink);

#ifdef __cplusplus
}
#endif
