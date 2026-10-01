#include "asio.h"
#include "src/context.h"
#include "src/error.h"

#include <math.h>
#include <string.h>

enum {
    LND_ASIO_API_CONFIG_GET,
    LND_ASIO_API_CONFIG_SET,
    LND_ASIO_API_INFO,
    LND_ASIO_API_CHANNEL,
    LND_ASIO_API_CLOCK_COUNT,
    LND_ASIO_API_CLOCK_GET,
    LND_ASIO_API_CLOCK_SET,
    LND_ASIO_API_RATE,
    LND_ASIO_API_PANEL,
    LND_ASIO_API_TIME,
    LND_ASIO_API_MONITOR,
    LND_ASIO_API_GAIN,
    LND_ASIO_API_METER,
    LND_ASIO_API_TRANSPORT,
    LND_ASIO_API_RESET,
};

typedef struct lnd_asio_api_request {
    lnd_asio_driver *driver;
    unsigned operation;
    int32_t type;
    int32_t index;
    uint32_t value;
    uint64_t position;
    float gain;
    void *data;
    const void *input;
} lnd_asio_api_request;

int32_t lnd_asio_config_validate(const LND_ASIO_CONFIG *config) {
    if (!config || config->sample_rate_hz > 768000 || config->buffer_frames > (1u << 20) || config->input_channels > LND_ASIO_MAX_CHANNELS ||
        config->output_channels > LND_ASIO_MAX_CHANNELS ||
        (config->flags & ~(LND_ASIO_DISABLE_INPUT | LND_ASIO_DISABLE_OUTPUT | LND_ASIO_DEFER_PROCESS | LND_ASIO_TIMECODE)) ||
        ((config->flags & (LND_ASIO_DISABLE_INPUT | LND_ASIO_DISABLE_OUTPUT)) == (LND_ASIO_DISABLE_INPUT | LND_ASIO_DISABLE_OUTPUT)) ||
        ((config->flags & LND_ASIO_DISABLE_INPUT) && config->input_channels) || ((config->flags & LND_ASIO_DISABLE_OUTPUT) && config->output_channels) ||
        (config->window && !IsWindow(config->window)))
        return LND_ERR_INVALID_ARG;
    for (unsigned direction = 0; direction < 2; direction++) {
        uint32_t count = direction ? config->input_channels : config->output_channels;
        const uint32_t *map = direction ? config->input_map : config->output_map;
        for (uint32_t i = 0; i < count; i++) {
            if (map[i] >= 4096)
                return LND_ERR_INVALID_ARG;
            for (uint32_t j = 0; j < i; j++)
                if (map[i] == map[j])
                    return LND_ERR_INVALID_ARG;
        }
    }
    return LND_OK;
}

static int32_t lnd_asio_clocks(lnd_asio_driver *driver, struct cwASIOClockSource *clocks, long *count) {
    *count = 64;
    int32_t result = lnd_asio_error(driver, driver->abi->vt->getClockSources(driver->abi, clocks, count));
    if (result == LND_OK && (*count < 0 || *count > 64))
        result = LND_ERR_FORMAT;
    return result;
}

static int32_t lnd_asio_config_check_device(lnd_asio_driver *driver, const LND_ASIO_CONFIG *config) {
    int32_t result = lnd_asio_config_validate(config);
    if (result != LND_OK)
        return result;
    for (unsigned direction = 0; direction < 2; direction++) {
        uint32_t count = direction ? config->input_channels : config->output_channels;
        uint32_t available = direction ? driver->info.input_channels : driver->info.output_channels;
        const uint32_t *map = direction ? config->input_map : config->output_map;
        for (uint32_t i = 0; i < count; i++)
            if (map[i] >= available)
                return LND_ERR_INVALID_ARG;
    }
    uint32_t frames;
    result = lnd_asio_buffer_size(config->buffer_frames, driver->info.min_buffer_frames, driver->info.max_buffer_frames, driver->info.preferred_buffer_frames,
                                  driver->info.buffer_granularity, &frames);
    if (result == LND_OK && config->sample_rate_hz)
        result = lnd_asio_error(driver, driver->abi->vt->canSampleRate(driver->abi, config->sample_rate_hz));
    if (result == LND_OK && (config->flags & LND_ASIO_TIMECODE) && !(driver->info.capabilities & LND_ASIO_CAP_TIMECODE))
        result = LND_ERR_UNSUPPORTED;
    return result;
}

static int32_t lnd_asio_api_loaded(lnd_asio_api_request *request) {
    lnd_asio_driver *driver = request->driver;
    lnd_asio_abi *abi = driver->abi;
    switch (request->operation) {
    case LND_ASIO_API_INFO: {
        LND_ASIO_INFO *info = request->data;
        *info = driver->info;
        info->running = driver->started;
        info->reset_pending = lnd_load(&driver->pending) != 0;
        info->overloads = lnd_load(&driver->overloads);
        info->driver_error = lnd_load(&driver->driver_error);
        return LND_OK;
    }
    case LND_ASIO_API_CONFIG_SET:
        return lnd_asio_config_check_device(driver, request->input);
    case LND_ASIO_API_CHANNEL: {
        if (request->value >= (request->type == LND_DEVICE_INPUT ? driver->info.input_channels : driver->info.output_channels))
            return LND_ERR_INVALID_ARG;
        struct cwASIOChannelInfo channel = {.channel = (long)request->value, .isInput = request->type == LND_DEVICE_INPUT};
        int32_t result = lnd_asio_error(driver, abi->vt->getChannelInfo(abi, &channel));
        if (result != LND_OK)
            return result;
        LND_ASIO_CHANNEL_INFO *info = request->data;
        *info = (LND_ASIO_CHANNEL_INFO){
            .channel = request->value, .sample_type = (int32_t)channel.type, .group = (int32_t)channel.channelGroup, .active = channel.isActive != 0};
        lnd_asio_text(info->name, sizeof info->name, channel.name, sizeof channel.name);
        lnd_asio_pcm pcm;
        info->pcm_supported = lnd_asio_pcm_get((int32_t)channel.type, &pcm);
        if (info->pcm_supported) {
            info->sample_bytes = pcm.bytes;
            info->valid_bits = pcm.bits;
            info->floating_point = pcm.floating_point;
            info->big_endian = pcm.big_endian;
        }
        return LND_OK;
    }
    case LND_ASIO_API_CLOCK_COUNT:
    case LND_ASIO_API_CLOCK_GET:
    case LND_ASIO_API_CLOCK_SET: {
        struct cwASIOClockSource clocks[64] = {0};
        long count = 0;
        int32_t result = lnd_asio_clocks(driver, clocks, &count);
        if (result != LND_OK)
            return result;
        if (request->operation == LND_ASIO_API_CLOCK_COUNT) {
            *(uint32_t *)request->data = (uint32_t)count;
            return LND_OK;
        }
        if (request->operation == LND_ASIO_API_CLOCK_SET) {
            bool found = false;
            for (long i = 0; i < count; i++)
                if (clocks[i].index == request->index)
                    found = true;
            if (!found)
                return LND_ERR_INVALID_ARG;
            result = lnd_asio_error(driver, abi->vt->setClockSource(abi, request->index));
            if (result == LND_OK)
                driver->clock_index = request->index;
            return result;
        }
        if (request->value >= (uint32_t)count)
            return LND_ERR_INVALID_ARG;
        struct cwASIOClockSource *clock = &clocks[request->value];
        LND_ASIO_CLOCK_INFO *info = request->data;
        *info = (LND_ASIO_CLOCK_INFO){.index = clock->index,
                                      .associated_channel = clock->associatedChannel,
                                      .associated_group = clock->associatedGroup,
                                      .current = clock->isCurrentSource != 0};
        lnd_asio_text(info->name, sizeof info->name, clock->name, sizeof clock->name);
        return LND_OK;
    }
    case LND_ASIO_API_RATE:
        return lnd_asio_error(driver, abi->vt->canSampleRate(abi, request->value));
    case LND_ASIO_API_PANEL:
        return lnd_asio_error(driver, abi->vt->controlPanel(abi));
    case LND_ASIO_API_MONITOR: {
        const LND_ASIO_INPUT_MONITOR *monitor = request->input;
        if (monitor->input_channel < -1 || (monitor->input_channel >= 0 && (uint32_t)monitor->input_channel >= driver->info.input_channels) ||
            monitor->output_channel >= driver->info.output_channels)
            return LND_ERR_INVALID_ARG;
        struct cwASIOInputMonitor value = {.input = monitor->input_channel,
                                           .output = (long)monitor->output_channel,
                                           .gain = (long)llrint((double)monitor->gain * INT32_MAX),
                                           .state = monitor->enabled,
                                           .pan = (long)llrint(((double)monitor->pan + 1.0) * (INT32_MAX / 2.0))};
        return lnd_asio_error(driver, abi->vt->future(abi, kAsioSetInputMonitor, &value));
    }
    case LND_ASIO_API_GAIN:
    case LND_ASIO_API_METER: {
        bool input = request->type == LND_DEVICE_INPUT;
        if (request->value >= (input ? driver->info.input_channels : driver->info.output_channels))
            return LND_ERR_INVALID_ARG;
        struct cwASIOChannelControls controls = {.channel = (long)request->value, .isInput = input};
        long selector;
        if (request->operation == LND_ASIO_API_GAIN) {
            controls.gain = (long)llrint((double)request->gain * INT32_MAX);
            selector = input ? kAsioSetInputGain : kAsioSetOutputGain;
        } else
            selector = input ? kAsioGetInputMeter : kAsioGetOutputMeter;
        int32_t result = lnd_asio_error(driver, abi->vt->future(abi, selector, &controls));
        if (result == LND_OK && request->operation == LND_ASIO_API_METER) {
            if (controls.meter < 0)
                return LND_ERR_FORMAT;
            *(float *)request->data = (float)((double)controls.meter / INT32_MAX);
        }
        return result;
    }
    case LND_ASIO_API_TRANSPORT: {
        struct cwASIOTransportParameters transport = {.command = request->index, .track = (long)request->value};
        transport.samplePosition.hi = (unsigned long)(request->position >> 32);
        transport.samplePosition.lo = (unsigned long)request->position;
        return lnd_asio_error(driver, abi->vt->future(abi, kAsioTransport, &transport));
    }
    default:
        return LND_ERR_INVALID_ARG;
    }
}

static int32_t lnd_asio_api_command(lnd_asio_backend *backend, void *argument) {
    LND_UNUSED(backend);
    lnd_asio_api_request *request = argument;
    lnd_asio_driver *driver = request->driver;
    if (request->operation == LND_ASIO_API_CONFIG_GET) {
        *(LND_ASIO_CONFIG *)request->data = driver->config;
        return LND_OK;
    }
    if (request->operation == LND_ASIO_API_TIME)
        return lnd_asio_read_time(driver, request->data);
    if (request->operation == LND_ASIO_API_RESET) {
        lnd_asio_invalidate(driver, LND_ERR_EXTERNAL);
        if (driver->owner->backend->wake)
            driver->owner->backend->wake();
        return LND_OK;
    }
    bool active = lnd_load(&driver->streams[0]) || lnd_load(&driver->streams[1]);
    if (active && (request->operation == LND_ASIO_API_CONFIG_SET || request->operation == LND_ASIO_API_CLOCK_SET))
        return LND_ERR_BUSY;
    bool loaded = driver->abi != nullptr;
    int32_t result = lnd_asio_load(driver);
    if (result == LND_OK)
        result = lnd_asio_api_loaded(request);
    if (!loaded && driver->abi)
        lnd_asio_unload(driver);
    if (result == LND_OK && request->operation == LND_ASIO_API_CONFIG_SET) {
        lnd_asio_unload(driver);
        driver->config = *(const LND_ASIO_CONFIG *)request->input;
        lnd_store(&driver->recovery_rate, 0);
        lnd_store(&driver->recovery_buffer, 0);
    }
    return result;
}

static int32_t lnd_asio_api(LND_DEVICE *device, lnd_asio_api_request request) {
    if (!lnd_context_enter())
        return lnd_error(LND_ERR_BUSY);
    device = lnd_device_resolve((LND_DEVICE *)device);
    lnd_asio_driver *driver = lnd_asio_device_driver(device);
    int32_t result = LND_ERR_INVALID_ARG;
    if (driver) {
        request.driver = driver;
        result = lnd_asio_call(driver->owner, lnd_asio_api_command, &request);
    }
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_DeviceGetAsioConfig(const LND_DEVICE *object, LND_ASIO_CONFIG *config) {
    LND_DEVICE *device = (LND_DEVICE *)object;
    if (!config)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_CONFIG_GET, .data = config});
}

int32_t LND_DeviceSetAsioConfig(LND_DEVICE *device, const LND_ASIO_CONFIG *config) {
    int32_t result = lnd_asio_config_validate(config);
    if (result != LND_OK)
        return lnd_error(result);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_CONFIG_SET, .input = config});
}

int32_t LND_DeviceGetAsioInfo(const LND_DEVICE *object, LND_ASIO_INFO *info) {
    LND_DEVICE *device = (LND_DEVICE *)object;
    if (!info)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_INFO, .data = info});
}

int32_t LND_DeviceGetAsioChannelInfo(const LND_DEVICE *object, int32_t type, uint32_t channel, LND_ASIO_CHANNEL_INFO *info) {
    LND_DEVICE *device = (LND_DEVICE *)object;
    if (!info || (type != LND_DEVICE_INPUT && type != LND_DEVICE_OUTPUT))
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_CHANNEL, .type = type, .value = channel, .data = info});
}

int32_t LND_DeviceGetAsioClockCount(const LND_DEVICE *object, uint32_t *count) {
    LND_DEVICE *device = (LND_DEVICE *)object;
    if (!count)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_CLOCK_COUNT, .data = count});
}

int32_t LND_DeviceGetAsioClock(const LND_DEVICE *object, uint32_t ordinal, LND_ASIO_CLOCK_INFO *info) {
    LND_DEVICE *device = (LND_DEVICE *)object;
    if (!info)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_CLOCK_GET, .value = ordinal, .data = info});
}

int32_t LND_DeviceSetAsioClock(LND_DEVICE *device, int32_t index) {
    if (index < 0)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_CLOCK_SET, .index = index});
}

int32_t LND_DeviceCheckAsioSampleRateHz(LND_DEVICE *device, uint32_t sample_rate_hz) {
    if (!sample_rate_hz || sample_rate_hz > 768000)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_RATE, .value = sample_rate_hz});
}

int32_t LND_DeviceShowAsioControlPanel(LND_DEVICE *device) { return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_PANEL}); }

int32_t LND_DeviceGetAsioTime(const LND_DEVICE *object, LND_ASIO_TIME *time) {
    LND_DEVICE *device = (LND_DEVICE *)object;
    if (!time)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_TIME, .data = time});
}

int32_t LND_DeviceSetAsioInputMonitor(LND_DEVICE *device, const LND_ASIO_INPUT_MONITOR *monitor) {
    if (!monitor || !isfinite(monitor->gain) || monitor->gain < 0 || monitor->gain > 1 || !isfinite(monitor->pan) || monitor->pan < -1 || monitor->pan > 1)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_MONITOR, .input = monitor});
}

int32_t LND_DeviceSetAsioChannelGain(LND_DEVICE *device, int32_t type, uint32_t channel, float gain) {
    if ((type != LND_DEVICE_INPUT && type != LND_DEVICE_OUTPUT) || !isfinite(gain) || gain < 0 || gain > 1)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_GAIN, .type = type, .value = channel, .gain = gain});
}

int32_t LND_DeviceGetAsioChannelMeter(const LND_DEVICE *object, int32_t type, uint32_t channel, float *meter) {
    LND_DEVICE *device = (LND_DEVICE *)object;
    if (!meter || (type != LND_DEVICE_INPUT && type != LND_DEVICE_OUTPUT))
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_METER, .type = type, .value = channel, .data = meter});
}

int32_t LND_DeviceSendAsioTransport(LND_DEVICE *device, int32_t command, uint64_t position_frames, int32_t track) {
    if (command < LND_ASIO_TRANSPORT_START || command > LND_ASIO_TRANSPORT_MONITOR_OFF || track < 0)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_asio_api(device,
                        (lnd_asio_api_request){.operation = LND_ASIO_API_TRANSPORT, .index = command, .value = (uint32_t)track, .position = position_frames});
}

int32_t LND_DeviceResetAsio(LND_DEVICE *device) { return lnd_asio_api(device, (lnd_asio_api_request){.operation = LND_ASIO_API_RESET}); }
