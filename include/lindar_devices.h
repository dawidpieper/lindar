#pragma once

#include "lindar.h"
#include "lindar_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Key "devices.auto_open": Open the preferred/default output during library initialisation (0/1). */
LND_API extern LND_CONFIG_KEY *const LND_CFG_DEVICES_AUTO_OPEN;

/** Key "devices.format": Requested device LND_FORMAT; NONE lets the backend choose. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_DEVICES_FORMAT;

/** Key "devices.period_frames": Requested device period in frames; zero uses the backend default. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_DEVICES_PERIOD_FRAMES;

/** Key "devices.periods": Requested number of device buffer periods (1-64). */
LND_API extern LND_CONFIG_KEY *const LND_CFG_DEVICES_PERIODS;

/** Key "devices.exclusive": Request exclusive device access where supported (0/1). */
LND_API extern LND_CONFIG_KEY *const LND_CFG_DEVICES_EXCLUSIVE;

/** Key "devices.thread_priority": LND_DEVICE_THREAD_PRIORITY policy for audio threads. */
LND_API extern LND_CONFIG_KEY *const LND_CFG_DEVICES_THREAD_PRIORITY;

/** Key "devices.follow_default": Follow system default route changes (0/1). */
LND_API extern LND_CONFIG_KEY *const LND_CFG_DEVICES_FOLLOW_DEFAULT;

/** Key "devices.reopen_interval_ms": Device recovery retry interval in milliseconds (10-600000). */
LND_API extern LND_CONFIG_KEY *const LND_CFG_DEVICES_REOPEN_INTERVAL_MS;

enum {
    LND_ERR_NO_DEVICE = -3 /**< No suitable audio device is available. */
};

/** Borrowed device backend identity; no knowledge of other backends is required. */
typedef struct LND_DEVICE_BACKEND LND_DEVICE_BACKEND;

/** Get the number of device backends compiled into this library.
 *
 * @return The number of device backends compiled into this library.
 */
LND_API uint32_t LND_DeviceBackendGetCount(void);

/** Get a backend at zero-based index.
 *
 * @param index Zero-based entry index.
 * @return A borrowed backend at zero-based index, or NULL if out of range.
 */
LND_API const LND_DEVICE_BACKEND *LND_DeviceBackendGet(uint32_t index);

/** Get the backend matching name.
 *
 * @param name Implementation name.
 * @return The borrowed backend matching name, or NULL if unavailable.
 */
LND_API const LND_DEVICE_BACKEND *LND_DeviceBackendFind(const char *name);

/** Get backend's name.
 *
 * @param backend Borrowed device backend descriptor.
 * @return Backend's borrowed name, or NULL for an invalid descriptor.
 */
LND_API const char *LND_DeviceBackendGetName(const LND_DEVICE_BACKEND *backend);

/** Select backend before LND_LibraryInit; NULL restores automatic selection.
 *
 * @param backend Backend to prefer before LibraryInit; NULL selects automatically.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceSetPreferredBackend(const LND_DEVICE_BACKEND *backend);

/** Get the preferred backend.
 *
 * @return The borrowed preferred backend, or NULL for automatic selection.
 */
LND_API const LND_DEVICE_BACKEND *LND_DeviceGetPreferredBackend(void);

/** Get the backend currently in use.
 *
 * @return The borrowed backend currently in use, or NULL before selection.
 */
LND_API const LND_DEVICE_BACKEND *LND_DeviceGetActiveBackend(void);

/** Borrowed enumerated device or logical default route; never free directly. */
typedef struct LND_DEVICE LND_DEVICE;

/** Opened device stream and its graph node; close owned instances with DeviceInstanceClose. */
typedef struct LND_DEVICE_INSTANCE LND_DEVICE_INSTANCE;

enum {
    LND_DEVICE_THREAD_PRIORITY_NORMAL = 0, /**< Use ordinary audio worker priority. */
    LND_DEVICE_THREAD_PRIORITY_REALTIME = 1, /**< Request the backend's realtime audio priority. */
};

/** Report event and its borrowed instance, possibly NULL, to user. May run on a library thread.
 *
 * @param user Borrowed callback context.
 * @param event LND_DEVICE_EVENT notification kind.
 * @param instance Borrowed affected instance; may be NULL.
 */
typedef void (*LND_DEVICE_PROC)(void *user, int32_t event, LND_DEVICE_INSTANCE *instance);

enum {
    LND_DEVICE_OUTPUT = 0, /**< Playback/output direction. */
    LND_DEVICE_INPUT = 1, /**< Recording/input direction. */
};

enum {
    LND_CAPTURE_RESAMPLE = 1u << 0, /**< Allow capture rate/channel conversion. */
    LND_CAPTURE_LOOPBACK = 1u << 1, /**< Capture the playback mix where supported. */
    LND_CAPTURE_EXCLUSIVE = 1u << 2, /**< Request exclusive capture access. */
    LND_CAPTURE_NONBLOCKING = 1u << 3, /**< Return immediately when capture PCM is unavailable. */
};

enum {
    LND_DEVICE_FLAG_SHARED = 1u << 0, /**< Shared device access is supported. */
    LND_DEVICE_FLAG_EXCLUSIVE = 1u << 1, /**< Exclusive device access is supported. */
    LND_DEVICE_FLAG_MULTI_INSTANCE = 1u << 2, /**< Multiple simultaneous instances are supported. */
    LND_DEVICE_FLAG_DEFAULT = 1u << 3, /**< Device is the current default for its direction. */
    LND_DEVICE_FLAG_STALE = 1u << 4, /**< Descriptor remains known but the device is no longer present. */
    LND_DEVICE_FLAG_LOOPBACK = 1u << 5, /**< Loopback capture is supported. */
};

enum {
    LND_DEVICE_EVENT_DEVICES_CHANGED = 0, /**< Enumerated device set changed. */
    LND_DEVICE_EVENT_DEFAULT_OUTPUT_CHANGED = 1, /**< System default output changed. */
    LND_DEVICE_EVENT_DEFAULT_INPUT_CHANGED = 2, /**< System default input changed. */
    LND_DEVICE_EVENT_INSTANCE_FAILED = 3, /**< An open device instance failed. */
    LND_DEVICE_EVENT_INSTANCE_REOPENED = 4, /**< An instance was reopened after a device/route change. */
};

/** One reported device format; backends may expose only a preferred mode. */
typedef struct LND_DEVICE_MODE {
    uint32_t sample_rate_hz; /**< PCM sample rate in Hz. */
    uint32_t channels; /**< Number of PCM channels. */
    int32_t format; /**< LND_FORMAT sample representation. */
    uint32_t flags; /**< LND_DEVICE_FLAG access/capability bits for this mode. */
} LND_DEVICE_MODE;

/** Borrowed logical input handle following the current default route. */
LND_API extern LND_DEVICE *const LND_DEVICE_DEFAULT_INPUT;

/** Borrowed logical output handle following the current default route. */
LND_API extern LND_DEVICE *const LND_DEVICE_DEFAULT_OUTPUT;

/** Get node's device instance.
 *
 * @param node Graph node to operate on.
 * @return Node's borrowed device instance, or NULL if it is not a device node.
 */
LND_API LND_DEVICE_INSTANCE *LND_NodeGetDeviceInstance(const LND_NODE *node);

/** Resolve a logical default device handle to its current physical device.
 *
 * @param device Borrowed device descriptor.
 * @return A borrowed device or NULL.
 */
LND_API LND_DEVICE *LND_DeviceResolve(LND_DEVICE *device);

/** Notify the backend of a default change for input/output type.
 * Call when external routing changes but the observable default identity stays unchanged.
 *
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceNotifyDefaultChanged(int32_t type);

/** Refresh device enumeration and default routes.
 *
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceRefresh(void);

/** Select device for input/output type; NULL clears the preference.
 *
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @param device Preferred device; NULL clears the preference.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceSetPreferred(int32_t type, LND_DEVICE *device);

/** Get the preferred device for input/output type.
 *
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @return The borrowed preferred device for input/output type, or NULL if unset.
 */
LND_API LND_DEVICE *LND_DeviceGetPreferred(int32_t type);

/** Select instance as the library output; NULL clears the selection.
 *
 * @param instance Output instance to select; NULL clears the selection.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceSetOutputInstance(LND_DEVICE_INSTANCE *instance);

/** Set proc and borrowed user together; NULL proc disables callbacks.
 *
 * @param proc Callback to install; user supplies its context.
 * @param user Borrowed callback context.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceSetEventCallback(LND_DEVICE_PROC proc, void *user);

/** Get the enumerated device count for input/output type.
 *
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @return The enumerated device count for input/output type.
 */
LND_API uint32_t LND_DeviceGetCount(int32_t type);

/** Get a device of type at zero-based index.
 *
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @param index Zero-based entry index.
 * @return A borrowed device of type at zero-based index, or NULL if out of range.
 */
LND_API LND_DEVICE *LND_DeviceGet(int32_t type, uint32_t index);

/** Get the current physical default device of type.
 *
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @return The borrowed current physical default device of type, or NULL if unavailable.
 */
LND_API LND_DEVICE *LND_DeviceGetDefault(int32_t type);

/** Get type's logical default handle.
 *
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @return Type's borrowed logical default handle; it follows future default changes.
 */
LND_API LND_DEVICE *LND_DeviceGetDefaultHandle(int32_t type);

/** Get the device of type with id.
 * ALSA's default PCM follows system routing, including PulseAudio/PipeWire; these have no separate
 * Lindar backends.
 *
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @param id Backend device identifier.
 * @return The borrowed device of type with id, or NULL if absent.
 */
LND_API LND_DEVICE *LND_DeviceFind(int32_t type, const char *id);

/** Get d's display name.
 *
 * @param d Borrowed device descriptor.
 * @return D's borrowed display name, or an empty string if unavailable.
 */
LND_API const char *LND_DeviceGetName(const LND_DEVICE *d);

/** Get d's backend identifier.
 *
 * @param d Borrowed device descriptor.
 * @return D's borrowed backend identifier, or an empty string for NULL.
 */
LND_API const char *LND_DeviceGetId(const LND_DEVICE *d);

/** Get d's input/output type.
 *
 * @param d Borrowed device descriptor.
 * @return D's input/output type; NULL yields LND_DEVICE_OUTPUT.
 */
LND_API int32_t LND_DeviceGetType(const LND_DEVICE *d);

/** Check whether d is currently the default device of its type.
 *
 * @param d Borrowed device descriptor.
 * @return True if d is currently the default device of its type; false otherwise.
 */
LND_API bool LND_DeviceIsDefault(const LND_DEVICE *d);

/** Get d's LND_DEVICE_FLAG capability and state bits.
 *
 * @param d Borrowed device descriptor.
 * @return D's LND_DEVICE_FLAG capability and state bits.
 */
LND_API uint32_t LND_DeviceGetFlags(const LND_DEVICE *d);

/** Get d's preferred sample rate in Hz.
 *
 * @param d Borrowed device descriptor.
 * @return D's preferred sample rate in Hz, or zero if unavailable.
 */
LND_API uint32_t LND_DeviceGetSampleRateHz(const LND_DEVICE *d);

/** Get d's preferred channel count.
 *
 * @param d Borrowed device descriptor.
 * @return D's preferred channel count, or zero if unavailable.
 */
LND_API uint32_t LND_DeviceGetChannels(const LND_DEVICE *d);

/** Get d's preferred PCM format.
 *
 * @param d Borrowed device descriptor.
 * @return D's preferred PCM format, or NONE if unavailable.
 */
LND_API int32_t LND_DeviceGetFormat(const LND_DEVICE *d);

/** Get d's suggested processing period in frames.
 *
 * @param d Borrowed device descriptor.
 * @return D's suggested processing period in frames, or zero if unknown.
 */
LND_API uint32_t LND_DeviceGetDefaultPeriodFrames(const LND_DEVICE *d);

/** Get d's minimum reported processing period in frames.
 *
 * @param d Borrowed device descriptor.
 * @return D's minimum reported processing period in frames, or zero if unknown.
 */
LND_API uint32_t LND_DeviceGetMinPeriodFrames(const LND_DEVICE *d);

/** Get the number of reported modes for d.
 *
 * @param d Borrowed device descriptor.
 * @return The number of reported modes for d.
 */
LND_API uint32_t LND_DeviceGetModeCount(const LND_DEVICE *d);

/** Copy d's mode at zero-based index into out.
 *
 * @param d Borrowed device descriptor.
 * @param index Zero-based entry index.
 * @param out Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceGetMode(const LND_DEVICE *d, uint32_t index, LND_DEVICE_MODE *out);

/** Get d's reported simultaneous instance limit.
 *
 * @param d Borrowed device descriptor.
 * @return D's reported simultaneous instance limit.
 */
LND_API uint32_t LND_DeviceGetMaxInstances(const LND_DEVICE *d);

/** Get the number of open instances associated with d.
 *
 * @param d Borrowed device descriptor.
 * @return The number of open instances associated with d.
 */
LND_API uint32_t LND_DeviceGetInstanceCount(const LND_DEVICE *d);

/** Get d's open instance at zero-based index.
 *
 * @param d Borrowed device descriptor.
 * @param index Zero-based entry index.
 * @return D's borrowed open instance at zero-based index, or NULL if out of range.
 */
LND_API LND_DEVICE_INSTANCE *LND_DeviceGetInstance(const LND_DEVICE *d, uint32_t index);

/** Open d using current device settings.
 *
 * @param d Borrowed device descriptor.
 * @return An owned instance or NULL; release with LND_DeviceInstanceClose.
 */
LND_API LND_DEVICE_INSTANCE *LND_DeviceInstanceOpen(LND_DEVICE *d);

/** Close i and release its device resources.
 *
 * @param i Open device instance.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceInstanceClose(LND_DEVICE_INSTANCE *i);

/** Get the current output instance.
 *
 * @return The borrowed current output instance, or NULL; does not open one.
 */
LND_API LND_DEVICE_INSTANCE *LND_DeviceGetOutputInstance(void);

/** Get the current output node.
 *
 * @return The borrowed current output node, or NULL; does not open a device.
 */
LND_API LND_NODE *LND_DeviceGetOutputNode(void);

/** Open the default output if needed.
 *
 * @return The borrowed output instance, or NULL on failure. The library owns it.
 */
LND_API LND_DEVICE_INSTANCE *LND_DeviceEnsureOutputInstance(void);

/** Open the default output if needed.
 *
 * @return The borrowed output node, or NULL on failure. The device instance owns it.
 */
LND_API LND_NODE *LND_DeviceEnsureOutputNode(void);

/** Get i's device descriptor.
 *
 * @param i Open device instance.
 * @return I's borrowed device descriptor, or NULL if unavailable.
 */
LND_API LND_DEVICE *LND_DeviceInstanceGetDevice(const LND_DEVICE_INSTANCE *i);

/** Get i's graph node.
 *
 * @param i Open device instance.
 * @return I's borrowed graph node, or NULL if unavailable.
 */
LND_API LND_NODE *LND_DeviceInstanceGetNode(const LND_DEVICE_INSTANCE *i);

/** Get i's actual sample rate in Hz.
 *
 * @param i Open device instance.
 * @return I's actual sample rate in Hz, or zero if unavailable.
 */
LND_API uint32_t LND_DeviceInstanceGetSampleRateHz(const LND_DEVICE_INSTANCE *i);

/** Get i's actual channel count.
 *
 * @param i Open device instance.
 * @return I's actual channel count, or zero if unavailable.
 */
LND_API uint32_t LND_DeviceInstanceGetChannels(const LND_DEVICE_INSTANCE *i);

/** Get i's actual PCM format.
 *
 * @param i Open device instance.
 * @return I's actual PCM format, or NONE if unavailable.
 */
LND_API int32_t LND_DeviceInstanceGetFormat(const LND_DEVICE_INSTANCE *i);

/** Get i's processing period in frames.
 *
 * @param i Open device instance.
 * @return I's processing period in frames, or zero if unavailable.
 */
LND_API uint32_t LND_DeviceInstanceGetPeriodFrames(const LND_DEVICE_INSTANCE *i);

/** Get i's device buffer length in frames.
 *
 * @param i Open device instance.
 * @return I's device buffer length in frames, or zero if unavailable.
 */
LND_API uint32_t LND_DeviceInstanceGetBufferFrames(const LND_DEVICE_INSTANCE *i);

/** Get i's reported latency in frames.
 *
 * @param i Open device instance.
 * @return I's reported latency in frames, or zero if unavailable.
 */
LND_API uint32_t LND_DeviceInstanceGetLatencyFrames(const LND_DEVICE_INSTANCE *i);

/** Check whether i has exclusive device access.
 *
 * @param i Open device instance.
 * @return True if i has exclusive device access; false otherwise.
 */
LND_API bool LND_DeviceInstanceIsExclusive(const LND_DEVICE_INSTANCE *i);

/** Check whether i is currently running.
 *
 * @param i Open device instance.
 * @return True if i is currently running; false otherwise.
 */
LND_API bool LND_DeviceInstanceIsRunning(const LND_DEVICE_INSTANCE *i);

/** Get i's device clock position in frames.
 *
 * @param i Open device instance.
 * @return I's device clock position in frames.
 */
LND_API uint64_t LND_DeviceInstanceGetPositionFrames(const LND_DEVICE_INSTANCE *i);

/** Copied capture queue state and loss counters. */
typedef struct LND_CAPTURE_INFO {
    uint64_t dropped_frames; /**< Capture frames lost because the queue was full. */
    uint32_t buffered_frames; /**< Frames currently available to read. */
    uint32_t capacity_frames; /**< Queue capacity in frames. */
    bool paused; /**< Capture delivery is paused. */
} LND_CAPTURE_INFO;

/** Pause or resume device capture into source.
 *
 * @param source Source to operate on.
 * @param pause True to pause; false to resume.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SourceSetCapturePause(LND_SOURCE *source, bool pause);

/** Discard source's buffered capture and reset its counters.
 *
 * @param source Source to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SourceResetCapture(LND_SOURCE *source);

/** Copy source's capture counters and pause state into info.
 *
 * @param source Source to operate on.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SourceGetCaptureInfo(const LND_SOURCE *source, LND_CAPTURE_INFO *info);

/** Capture device using channels, rate and LND_CAPTURE flags; zero dimensions select defaults.
 *
 * @param device Input or loopback device; NULL selects the preferred/default input.
 * @param channels Capture channels; 0 selects the device default.
 * @param sample_rate_hz Capture rate in Hz; 0 selects the device default.
 * @param flags LND_CAPTURE option bits.
 * @return Owned source or NULL; release with LND_SourceFree.
 */
LND_API LND_SOURCE *LND_SourceCreateDevice(LND_DEVICE *device, uint32_t channels, uint32_t sample_rate_hz, uint32_t flags);

#ifdef __cplusplus
}
#endif
