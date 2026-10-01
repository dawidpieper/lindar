#pragma once

#include "lindar_codecs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Get the GStreamer codec descriptor.
 * Available formats depend on installed GStreamer plugins.
 *
 * @return The borrowed GStreamer codec descriptor; no release is needed.
 */
LND_API const LND_CODEC *LND_GStreamerGetCodec(void);

#ifdef __cplusplus
}
#endif
