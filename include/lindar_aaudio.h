#pragma once

#include "lindar_devices.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Android device entry supplied by the application; SetDevices copies the name. */
typedef struct LND_AAUDIO_DEVICE {
    int32_t id; /**< Android audio device ID. */
    int32_t type; /**< LND_DEVICE_INPUT or LND_DEVICE_OUTPUT. */
    const char *name; /**< Device display name copied by SetDevices. */
} LND_AAUDIO_DEVICE;

/** Copy count Android device entries and names from devices.
 * Supply AudioManager snapshots after library initialisation and update them when devices change.
 *
 * @param devices Array of count device entries to copy.
 * @param count Number of entries.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_AaudioSetDevices(const LND_AAUDIO_DEVICE *devices, uint32_t count);

/** Notify the backend that Android routing changed so device state can be refreshed.
 * Call from application route-change handling after library initialisation.
 */
LND_API void LND_AaudioNotifyRouteChange(void);

/** Set whether Android audio may run; inactive sessions suspend backend processing.
 * Forward application activity changes after library initialisation.
 *
 * @param active True to allow audio processing; false to suspend it.
 */
LND_API void LND_AaudioSetActive(bool active);

#ifdef __cplusplus
}
#endif
