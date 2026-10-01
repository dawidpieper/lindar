#pragma once

#include "lindar_coreaudio.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LND_IOS_CATEGORY_DEFAULT,         /**< Preserve the current system category. */
    LND_IOS_CATEGORY_AMBIENT,         /**< Mix playback with other applications and honour the silent switch. */
    LND_IOS_CATEGORY_SOLO_AMBIENT,    /**< Playback honouring the silent switch, without mixing. */
    LND_IOS_CATEGORY_PLAYBACK,        /**< Media playback independent of the silent switch. */
    LND_IOS_CATEGORY_RECORD,          /**< Audio recording. */
    LND_IOS_CATEGORY_PLAY_AND_RECORD, /**< Simultaneous input and output. */
    LND_IOS_CATEGORY_MULTI_ROUTE      /**< Route audio through multiple supported ports. */
};

enum {
    LND_IOS_MODE_DEFAULT,         /**< Use the standard mode when setting a category; otherwise preserve the current mode. */
    LND_IOS_MODE_VOICE_CHAT,      /**< Two-way voice communication. */
    LND_IOS_MODE_VIDEO_CHAT,      /**< Two-way video communication. */
    LND_IOS_MODE_GAME_CHAT,       /**< Voice communication within a game. */
    LND_IOS_MODE_VIDEO_RECORDING, /**< Capture audio alongside video. */
    LND_IOS_MODE_MEASUREMENT,     /**< Minimise system processing. */
    LND_IOS_MODE_MOVIE_PLAYBACK,  /**< Movie playback. */
    LND_IOS_MODE_SPOKEN_AUDIO     /**< Spoken-word playback. */
};

enum {
    LND_IOS_SESSION_MIX_WITH_OTHERS = 1u << 0,       /**< Mix with other sessions. */
    LND_IOS_SESSION_DUCK_OTHERS = 1u << 1,           /**< Reduce other sessions' volume. */
    LND_IOS_SESSION_ALLOW_BLUETOOTH = 1u << 2,       /**< Allow Bluetooth hands-free input/output. */
    LND_IOS_SESSION_ALLOW_BLUETOOTH_A2DP = 1u << 3,  /**< Allow Bluetooth A2DP output. */
    LND_IOS_SESSION_ALLOW_AIRPLAY = 1u << 4,         /**< Allow AirPlay output. */
    LND_IOS_SESSION_DEFAULT_TO_SPEAKER = 1u << 5,    /**< Prefer the speaker for play-and-record sessions. */
    LND_IOS_SESSION_INTERRUPT_SPOKEN_AUDIO = 1u << 6 /**< Interrupt spoken audio while mixing with other audio. */
};

enum {
    LND_IOS_SESSION_MANUAL = 1u << 0,       /**< No automatic AVAudioSession calls; report external activation with CoreAudioSetSessionActive. */
    LND_IOS_SESSION_ALLOW_HAPTICS = 1u << 1 /**< Allow haptics and system sounds while recording; requires iOS 13. */
};

enum {
    LND_IOS_RECORDING_DEFAULT, /**< Preserve the system's input channels and microphone selection. */
    LND_IOS_RECORDING_MONO,    /**< Request one input channel. */
    LND_IOS_RECORDING_STEREO   /**< Request two real input channels; unsupported hardware returns an error. Built-in stereo requires iOS 14. */
};

enum {
    LND_IOS_ORIENTATION_DEFAULT,              /**< Preserve the input orientation; also represents unknown device orientation. */
    LND_IOS_ORIENTATION_AUTO,                 /**< Follow the orientation supplied through IosSetDeviceOrientation. */
    LND_IOS_ORIENTATION_PORTRAIT,             /**< Connector at the bottom. */
    LND_IOS_ORIENTATION_PORTRAIT_UPSIDE_DOWN, /**< Connector at the top. */
    LND_IOS_ORIENTATION_LANDSCAPE_LEFT,       /**< Connector on the left. */
    LND_IOS_ORIENTATION_LANDSCAPE_RIGHT       /**< Connector on the right. */
};

/** Session preferences copied by IosSessionSetConfig. Zero preserves system choices and enables
 * activation on the first Lindar stream and deactivation on the last. Recording needs an input-capable
 * category, microphone permission and NSMicrophoneUsageDescription; background audio needs the app's
 * audio background mode.
 */
typedef struct LND_IOS_SESSION_CONFIG {
    int32_t category;            /**< LND_IOS_CATEGORY choice. */
    int32_t mode;                /**< LND_IOS_MODE choice; the OS validates category/mode combinations. */
    uint32_t options;            /**< LND_IOS_SESSION category-option bits; zero preserves options when category is DEFAULT. */
    uint32_t flags;              /**< LND_IOS_SESSION_MANUAL and ALLOW_HAPTICS bits. */
    uint32_t sample_rate_hz;     /**< Preferred sample rate in Hz, up to 384000; zero preserves the system choice. */
    uint32_t buffer_duration_us; /**< Preferred IO buffer duration in microseconds, up to 1000000; zero preserves the system choice. */
    int32_t recording;           /**< LND_IOS_RECORDING choice, applied when opening an input. */
    int32_t orientation;         /**< LND_IOS_ORIENTATION choice; non-default values require stereo recording. */
} LND_IOS_SESSION_CONFIG;

/** Store preferences for subsequent device opens; no AVAudioSession call is made.
 * Set before LND_LibraryInit if automatic device opening is enabled; otherwise close existing streams first.
 * @param config Preferences to copy; NULL restores zero defaults.
 * @return LND_OK, LND_ERR_BUSY while Lindar owns an open stream or active session, or a negative error.
 */
LND_API int32_t LND_IosSessionSetConfig(const LND_IOS_SESSION_CONFIG *config);

/** Copy the stored session preferences.
 * @param config Receives the current preferences, not negotiated hardware values.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_IosSessionGetConfig(LND_IOS_SESSION_CONFIG *config);

/** Explicitly apply category, mode and timing preferences without activating the session.
 * Manual setup order: IosSessionApply, IosSessionSetActive(true), IosSessionApplyInput, then open devices.
 * Failed OS calls can leave partial changes.
 * @return LND_OK or a negative error; works in manual mode.
 */
LND_API int32_t LND_IosSessionApply(void);

/** Explicitly apply recording and orientation preferences to an already active session.
 * Built-in stereo selects a supported stereo polar pattern, preferring the front data source.
 * External stereo routes are retained; device-relative orientation affects only built-in microphones.
 * @return LND_OK or a negative error; works in manual mode and may change the selected microphone.
 */
LND_API int32_t LND_IosSessionApplyInput(void);

/** Explicitly activate/deactivate AVAudioSession and notify the backend on success.
 * @param active True to activate; false deactivates and notifies other sessions.
 * @return LND_OK or a negative error; works in manual mode.
 */
LND_API int32_t LND_IosSessionSetActive(bool active);

/** Supply the application's device/scene orientation; AUTO input orientation follows valid updates.
 * Supply the initial orientation and subsequent changes; Lindar does not select a UIKit scene or read
 * motion sensors. Map UIKit values to Lindar's connector-position convention; enum values differ.
 * In manual session mode this only stores orientation; the application must apply input preferences.
 * @param orientation Physical LND_IOS_ORIENTATION value or DEFAULT if unknown; AUTO is invalid here.
 * @return LND_OK or a negative error. Unknown updates preserve the last known orientation.
 */
LND_API int32_t LND_IosSetDeviceOrientation(int32_t orientation);

/** Get the last accepted physical orientation supplied by the application.
 * @return LND_IOS_ORIENTATION value, DEFAULT before a valid update, or a negative error.
 */
LND_API int32_t LND_IosGetDeviceOrientation(void);

#ifdef __cplusplus
}
#endif
