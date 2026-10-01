/** @file
 * @details macOS HAL uses the hardware rate without changing it globally or taking hog mode.
 */
#pragma once

#include "lindar_devices.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Notify the backend that the system audio route changed.
 * Forward application route changes; this hook does not call AVAudioSession.
 */
LND_API void LND_CoreAudioNotifyRouteChange(void);

/** Set whether the application's CoreAudio session is active.
 * Report external activation/interruption changes; this hook does not call AVAudioSession.
 *
 * @param active True to allow audio processing; false to suspend it.
 */
LND_API void LND_CoreAudioSetSessionActive(bool active);

#ifdef __cplusplus
}
#endif
