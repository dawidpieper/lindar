#pragma once

#include "lindar_devices.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LND_ASIO_MAX_CHANNELS = 32 /**< Maximum mapped channels per ASIO input/output direction. */
};

enum {
    LND_ASIO_SAMPLE_S16BE = 0, /**< Signed 16-bit big-endian samples. */
    LND_ASIO_SAMPLE_S24BE = 1, /**< Packed signed 24-bit big-endian samples. */
    LND_ASIO_SAMPLE_S32BE = 2, /**< Signed 32-bit big-endian samples. */
    LND_ASIO_SAMPLE_F32BE = 3, /**< 32-bit floating-point big-endian samples. */
    LND_ASIO_SAMPLE_F64BE = 4, /**< 64-bit floating-point big-endian samples. */
    LND_ASIO_SAMPLE_S32BE16 = 8, /**< Signed 16-bit samples in 32-bit big-endian words. */
    LND_ASIO_SAMPLE_S32BE18 = 9, /**< Signed 18-bit samples in 32-bit big-endian words. */
    LND_ASIO_SAMPLE_S32BE20 = 10, /**< Signed 20-bit samples in 32-bit big-endian words. */
    LND_ASIO_SAMPLE_S32BE24 = 11, /**< Signed 24-bit samples in 32-bit big-endian words. */
    LND_ASIO_SAMPLE_S16LE = 16, /**< Signed 16-bit little-endian samples. */
    LND_ASIO_SAMPLE_S24LE = 17, /**< Packed signed 24-bit little-endian samples. */
    LND_ASIO_SAMPLE_S32LE = 18, /**< Signed 32-bit little-endian samples. */
    LND_ASIO_SAMPLE_F32LE = 19, /**< 32-bit floating-point little-endian samples. */
    LND_ASIO_SAMPLE_F64LE = 20, /**< 64-bit floating-point little-endian samples. */
    LND_ASIO_SAMPLE_S32LE16 = 24, /**< Signed 16-bit samples in 32-bit little-endian words. */
    LND_ASIO_SAMPLE_S32LE18 = 25, /**< Signed 18-bit samples in 32-bit little-endian words. */
    LND_ASIO_SAMPLE_S32LE20 = 26, /**< Signed 20-bit samples in 32-bit little-endian words. */
    LND_ASIO_SAMPLE_S32LE24 = 27, /**< Signed 24-bit samples in 32-bit little-endian words. */
    LND_ASIO_SAMPLE_DSD_LSB1 = 32, /**< Packed DSD, least-significant bit first; not PCM-convertible. */
    LND_ASIO_SAMPLE_DSD_MSB1 = 33, /**< Packed DSD, most-significant bit first; not PCM-convertible. */
    LND_ASIO_SAMPLE_DSD_NER8 = 40, /**< ASIO byte-based DSD representation; not PCM-convertible. */
};

enum {
    LND_ASIO_DISABLE_INPUT = 1u << 0, /**< Do not activate driver inputs. */
    LND_ASIO_DISABLE_OUTPUT = 1u << 1, /**< Do not activate driver outputs. */
    LND_ASIO_DEFER_PROCESS = 1u << 2, /**< Defer buffer processing to Lindar's worker. */
    LND_ASIO_TIMECODE = 1u << 3, /**< Request driver timecode delivery. */
};

enum {
    LND_ASIO_CAP_OUTPUT_READY = 1u << 0, /**< Driver accepts output-ready notifications. */
    LND_ASIO_CAP_TIME_INFO = 1u << 1, /**< Driver supplies timing snapshots. */
    LND_ASIO_CAP_TIMECODE = 1u << 2, /**< Driver supplies timecode. */
    LND_ASIO_CAP_INPUT_MONITOR = 1u << 3, /**< Driver supports hardware input monitoring. */
    LND_ASIO_CAP_INPUT_GAIN = 1u << 4, /**< Driver exposes input gain controls. */
    LND_ASIO_CAP_INPUT_METER = 1u << 5, /**< Driver exposes input meters. */
    LND_ASIO_CAP_OUTPUT_GAIN = 1u << 6, /**< Driver exposes output gain controls. */
    LND_ASIO_CAP_OUTPUT_METER = 1u << 7, /**< Driver exposes output meters. */
    LND_ASIO_CAP_TRANSPORT = 1u << 8, /**< Driver accepts transport commands. */
};

enum {
    LND_ASIO_TIME_SYSTEM = 1u << 0, /**< system_time_ns is valid. */
    LND_ASIO_TIME_POSITION = 1u << 1, /**< sample_position_frames is valid. */
    LND_ASIO_TIME_RATE = 1u << 2, /**< sample_rate_hz is valid. */
    LND_ASIO_TIME_SPEED = 1u << 3, /**< speed_ratio is valid. */
    LND_ASIO_TIME_CODE = 1u << 4, /**< timecode_position_frames is valid. */
    LND_ASIO_TIME_CODE_RUNNING = 1u << 5, /**< Timecode transport is running. */
    LND_ASIO_TIME_CODE_REVERSE = 1u << 6, /**< Timecode transport is moving backwards. */
};

/** ASIO settings copied before driver opening; zero dimensions select driver/backend defaults.
 * Input/output share one driver session and clock, with one active instance per direction.
 * DSD and system-loopback emulation are unsupported.
 */
typedef struct LND_ASIO_CONFIG {
    uint32_t sample_rate_hz; /**< Requested rate; zero uses the driver/backend default. */
    uint32_t buffer_frames; /**< Requested buffer frames; zero uses the driver preference. */
    uint32_t input_channels; /**< Requested active inputs; zero selects defaults unless disabled. */
    uint32_t output_channels; /**< Requested active outputs; zero selects defaults unless disabled. */
    uint32_t input_map[LND_ASIO_MAX_CHANNELS]; /**< Logical input index to physical ASIO channel index. */
    uint32_t output_map[LND_ASIO_MAX_CHANNELS]; /**< Logical output index to physical ASIO channel index. */
    uint32_t flags; /**< LND_ASIO_DISABLE_INPUT/OUTPUT, DEFER_PROCESS and TIMECODE bits. */
    void *window; /**< Optional native window handle passed to driver initialisation. */
} LND_ASIO_CONFIG;

/** Copied ASIO driver snapshot, including buffer limits, capabilities and recoverable state. */
typedef struct LND_ASIO_INFO {
    char driver_name[128]; /**< NUL-terminated driver display name. */
    int32_t driver_version; /**< Driver-reported version number. */
    uint32_t input_channels; /**< Physical input channel count. */
    uint32_t output_channels; /**< Physical output channel count. */
    uint32_t active_input_channels; /**< Number of enabled input channels. */
    uint32_t active_output_channels; /**< Number of enabled output channels. */
    uint32_t sample_rate_hz; /**< PCM sample rate in Hz. */
    uint32_t buffer_frames; /**< Device buffer length in frames. */
    uint32_t min_buffer_frames; /**< Minimum driver buffer length. */
    uint32_t max_buffer_frames; /**< Maximum driver buffer length. */
    uint32_t preferred_buffer_frames; /**< Preferred driver buffer length. */
    int32_t buffer_granularity; /**< Driver buffer step; -1 means powers of two, 0 a fixed size. */
    uint32_t input_latency_frames; /**< Driver-reported input latency. */
    uint32_t output_latency_frames; /**< Driver-reported output latency. */
    uint32_t capabilities; /**< LND_ASIO_CAP bits reported by the driver. */
    uint64_t overloads; /**< Reported processing overload count. */
    uint64_t resets; /**< Completed driver reset count. */
    int32_t driver_error; /**< Last driver result code. */
    char driver_error_message[128]; /**< NUL-terminated driver error description. */
    bool running; /**< Driver buffers are being processed. */
    bool reset_pending; /**< A requested driver reset has not yet completed. */
} LND_ASIO_INFO;

/** Copied description of one physical ASIO channel; pcm_supported controls Lindar PCM compatibility. */
typedef struct LND_ASIO_CHANNEL_INFO {
    char name[128]; /**< NUL-terminated channel display name. */
    uint32_t channel; /**< Zero-based physical channel index. */
    int32_t sample_type; /**< LND_ASIO_SAMPLE representation reported by the driver. */
    int32_t group; /**< Driver-defined channel group. */
    uint32_t sample_bytes; /**< Storage bytes per channel sample. */
    uint32_t valid_bits; /**< Significant bits in an integer sample. */
    bool floating_point; /**< True for an IEEE floating-point sample type. */
    bool big_endian; /**< True if sample byte order is big-endian. */
    bool active; /**< Channel is part of the current buffer set. */
    bool pcm_supported; /**< Lindar can convert this representation to/from PCM. */
} LND_ASIO_CHANNEL_INFO;

/** Copied ASIO clock descriptor; select by index, not enumeration ordinal. */
typedef struct LND_ASIO_CLOCK_INFO {
    char name[128]; /**< NUL-terminated clock display name. */
    int32_t index; /**< Driver clock ID used by SetAsioClock. */
    int32_t associated_channel; /**< Associated driver channel, or the driver's unassociated sentinel. */
    int32_t associated_group; /**< Associated driver channel group. */
    bool current; /**< This clock is currently selected. */
} LND_ASIO_CLOCK_INFO;

/** Copied timing snapshot; flags identify valid fields and timecode state. */
typedef struct LND_ASIO_TIME {
    uint64_t sample_position_frames; /**< Device sample position; valid with TIME_POSITION. */
    uint64_t system_time_ns; /**< System timestamp in nanoseconds; valid with TIME_SYSTEM. */
    uint64_t timecode_position_frames; /**< Timecode sample position; valid with TIME_CODE. */
    double sample_rate_hz; /**< Current sample rate; valid with TIME_RATE. */
    double speed_ratio; /**< Device speed relative to nominal; valid with TIME_SPEED. */
    double timecode_speed_ratio; /**< Timecode speed relative to nominal. */
    uint32_t flags; /**< LND_ASIO_TIME validity and transport state bits. */
} LND_ASIO_TIME;

/** Hardware input monitoring route; availability depends on driver capabilities. */
typedef struct LND_ASIO_INPUT_MONITOR {
    int32_t input_channel; /**< Physical input channel; -1 selects all inputs where supported. */
    uint32_t output_channel; /**< Physical output channel for monitoring. */
    float gain; /**< Linear monitor gain in [0, 1]. */
    float pan; /**< Monitor pan in [-1, 1]; -1 is left, +1 is right. */
    bool enabled; /**< Enable this hardware monitoring route. */
} LND_ASIO_INPUT_MONITOR;

enum {
    LND_ASIO_TRANSPORT_START = 1, /**< Start driver transport. */
    LND_ASIO_TRANSPORT_STOP, /**< Stop driver transport. */
    LND_ASIO_TRANSPORT_LOCATE, /**< Move transport to position_frames. */
    LND_ASIO_TRANSPORT_PUNCH_IN, /**< Enter recording on the selected track. */
    LND_ASIO_TRANSPORT_PUNCH_OUT, /**< Leave recording on the selected track. */
    LND_ASIO_TRANSPORT_ARM_ON, /**< Arm the selected track for recording. */
    LND_ASIO_TRANSPORT_ARM_OFF, /**< Disarm the selected track. */
    LND_ASIO_TRANSPORT_MONITOR_ON, /**< Enable monitoring for the selected track. */
    LND_ASIO_TRANSPORT_MONITOR_OFF, /**< Disable monitoring for the selected track. */
};

/** Copy device's ASIO configuration into config.
 *
 * @param device Borrowed device descriptor.
 * @param config Receives the current settings.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceGetAsioConfig(const LND_DEVICE *device, LND_ASIO_CONFIG *config);

/** Copy config for device before opening its driver.
 * Both input and output must be closed. For initial setup, disable LND_CFG_DEVICES_AUTO_OPEN and
 * select the ASIO backend before LND_LibraryInit, then configure channels, buffers and clock.
 *
 * @param device Borrowed device descriptor.
 * @param config Required settings, borrowed during the call.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceSetAsioConfig(LND_DEVICE *device, const LND_ASIO_CONFIG *config);

/** Copy device's ASIO driver state and capabilities into info.
 *
 * @param device Borrowed device descriptor.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceGetAsioInfo(const LND_DEVICE *device, LND_ASIO_INFO *info);

/** Copy ASIO channel details for device, input/output type and zero-based channel into info.
 *
 * @param device Borrowed device descriptor.
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @param channel Zero-based channel index.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceGetAsioChannelInfo(const LND_DEVICE *device, int32_t type, uint32_t channel, LND_ASIO_CHANNEL_INFO *info);

/** Write device's ASIO clock count to count.
 *
 * @param device Borrowed device descriptor.
 * @param count Receives the number of entries.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceGetAsioClockCount(const LND_DEVICE *device, uint32_t *count);

/** Copy device's ASIO clock at zero-based ordinal into info.
 *
 * @param device Borrowed device descriptor.
 * @param ordinal Zero-based clock ordinal, distinct from its driver index.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceGetAsioClock(const LND_DEVICE *device, uint32_t ordinal, LND_ASIO_CLOCK_INFO *info);

/** Select device's ASIO clock by driver index.
 * Both input and output must be closed.
 *
 * @param device Borrowed device descriptor.
 * @param index Driver clock index reported in LND_ASIO_CLOCK_INFO; not its ordinal.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceSetAsioClock(LND_DEVICE *device, int32_t index);

/** Ask whether device's ASIO driver accepts sample_rate_hz.
 *
 * @param device Borrowed device descriptor.
 * @param sample_rate_hz PCM sample rate in Hz.
 * @return LND_OK if supported or a negative error.
 */
LND_API int32_t LND_DeviceCheckAsioSampleRateHz(LND_DEVICE *device, uint32_t sample_rate_hz);

/** Open device's driver control panel.
 *
 * @param device Borrowed device descriptor.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceShowAsioControlPanel(LND_DEVICE *device);

/** Copy device's latest ASIO timing snapshot into time; check its validity flags.
 *
 * @param device Borrowed device descriptor.
 * @param time Receives the latest ASIO timing snapshot; check its validity flags.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceGetAsioTime(const LND_DEVICE *device, LND_ASIO_TIME *time);

/** Apply monitor's hardware input-to-output routing on device.
 *
 * @param device Borrowed device descriptor.
 * @param monitor Hardware monitoring route and levels.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceSetAsioInputMonitor(LND_DEVICE *device, const LND_ASIO_INPUT_MONITOR *monitor);

/** Set device's hardware gain for input/output type and channel.
 *
 * @param device Borrowed device descriptor.
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @param channel Zero-based channel index.
 * @param gain Hardware gain in [0, 1], scaled by the driver.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceSetAsioChannelGain(LND_DEVICE *device, int32_t type, uint32_t channel, float gain);

/** Copy device's hardware level for input/output type and channel into meter.
 *
 * @param device Borrowed device descriptor.
 * @param type LND_DEVICE_INPUT or LND_DEVICE_OUTPUT.
 * @param channel Zero-based channel index.
 * @param meter Receives the hardware level in [0, 1].
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceGetAsioChannelMeter(const LND_DEVICE *device, int32_t type, uint32_t channel, float *meter);

/** Send command, sample position and track to device's ASIO transport.
 * Driver transport is independent of Sound playback.
 *
 * @param device Borrowed device descriptor.
 * @param command LND_ASIO_TRANSPORT command.
 * @param position_frames Absolute position in frames.
 * @param track Driver-defined transport track number.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceSendAsioTransport(LND_DEVICE *device, int32_t command, uint64_t position_frames, int32_t track);

/** Request device's ASIO driver reset/reinitialisation.
 * The worker reopens both active directions together, including for driver-issued reset requests.
 *
 * @param device Borrowed device descriptor.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DeviceResetAsio(LND_DEVICE *device);

#ifdef __cplusplus
}
#endif
