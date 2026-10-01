#pragma once

#include "lindar_codecs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Get the MediaCodec descriptor.
 *
 * @return The borrowed MediaCodec descriptor; no release is needed.
 */
LND_API const LND_CODEC *LND_MediaCodecGetCodec(void);

#ifdef __cplusplus
}
#endif
