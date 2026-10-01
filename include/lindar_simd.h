#pragma once

#include "lindar.h"
#include "lindar_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Get the name of the selected PCM SIMD implementation.
 *
 * @return The static name of the selected PCM SIMD implementation; do not free it.
 */
LND_API const char *LND_SimdGetName(void);

/** Get the name of the selected sinc implementation.
 *
 * @return The static name of the selected sinc implementation; do not free it.
 */
LND_API const char *LND_SimdGetSincName(void);

#ifdef __cplusplus
}
#endif
