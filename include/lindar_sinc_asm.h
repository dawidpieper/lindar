#pragma once

#include "lindar.h"
#include "lindar_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Key "sinc_asm.enabled": Enable assembly sinc kernels when available (0/1); set before LibraryInit. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_SINC_ASM_ENABLED;

/** Check whether this build and processor support an assembly sinc kernel.
 *
 * @return True if this build and processor support an assembly sinc kernel; false otherwise.
 */
LND_API bool LND_SincAsmIsAvailable(void);

/** Get the name of the assembly sinc implementation.
 *
 * @return The static name of the assembly sinc implementation; do not free it.
 */
LND_API const char *LND_SincAsmGetName(void);

#ifdef __cplusplus
}
#endif
