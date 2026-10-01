#pragma once

#include "lindar_codecs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Get the AudioToolbox codec descriptor.
 *
 * @return The borrowed AudioToolbox codec descriptor; no release is needed.
 */
LND_API const LND_CODEC *LND_AudioToolboxGetCodec(void);

#ifdef __cplusplus
}
#endif
