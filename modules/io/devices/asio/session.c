#include "asio.h"
#include "src/alloc.h"
#include "src/config.h"

#include <avrt.h>
#include <math.h>
#include <string.h>

typedef struct lnd_asio_slot {
    lnd_atomic_ptr driver;
    lnd_atomic_u32 readers;
} lnd_asio_slot;

static lnd_asio_slot lnd_asio_slots[LND_ASIO_SLOTS];

static lnd_asio_driver *lnd_asio_acquire(unsigned slot) {
    atomic_fetch_add_explicit(&lnd_asio_slots[slot].readers, 1, memory_order_seq_cst);
    return atomic_load_explicit(&lnd_asio_slots[slot].driver, memory_order_seq_cst);
}

static void lnd_asio_release(unsigned slot) { atomic_fetch_sub_explicit(&lnd_asio_slots[slot].readers, 1, memory_order_seq_cst); }

void lnd_asio_request(lnd_asio_driver *driver, uint32_t flags) {
    atomic_fetch_or_explicit(&driver->pending, flags, memory_order_release);
    if (driver->owner->backend->wake)
        driver->owner->backend->wake();
}

static void lnd_asio_overload(lnd_asio_driver *driver) {
    lnd_add(&driver->overloads, 1);
    lnd_asio_request(driver, LND_ASIO_RESYNC);
}

static uint64_t lnd_asio_u64(cwASIOSamples value) { return ((uint64_t)value.hi << 32) | value.lo; }

static LND_ASIO_TIME lnd_asio_time_decode(const struct cwASIOTime *time) {
    LND_ASIO_TIME result = {0};
    if (!time)
        return result;
    const struct cwASIOTimeInfo *info = &time->timeInfo;
    if (info->flags & kSystemTimeValid) {
        result.system_time_ns = ((uint64_t)info->systemTime.hi << 32) | info->systemTime.lo;
        result.flags |= LND_ASIO_TIME_SYSTEM;
    }
    if (info->flags & kSamplePositionValid) {
        result.sample_position_frames = lnd_asio_u64(info->samplePosition);
        result.flags |= LND_ASIO_TIME_POSITION;
    }
    if ((info->flags & kSampleRateValid) && isfinite(info->sampleRate) && info->sampleRate > 0) {
        result.sample_rate_hz = info->sampleRate;
        result.flags |= LND_ASIO_TIME_RATE;
    }
    if ((info->flags & kSpeedValid) && isfinite(info->speed)) {
        result.speed_ratio = info->speed;
        result.flags |= LND_ASIO_TIME_SPEED;
    }
    if (time->timeCode.flags & kTcValid) {
        result.timecode_position_frames = lnd_asio_u64(time->timeCode.timeCodeSamples);
        result.timecode_speed_ratio = (time->timeCode.flags & kTcSpeedValid) && isfinite(time->timeCode.speed) ? time->timeCode.speed : 1.0;
        result.flags |= LND_ASIO_TIME_CODE;
        if (time->timeCode.flags & kTcRunning)
            result.flags |= LND_ASIO_TIME_CODE_RUNNING;
        if (time->timeCode.flags & kTcReverse)
            result.flags |= LND_ASIO_TIME_CODE_REVERSE;
    }
    return result;
}

static void lnd_asio_time_publish(lnd_asio_driver *driver, const LND_ASIO_TIME *time) {
    uint64_t values[6] = {time->sample_position_frames, time->system_time_ns, time->timecode_position_frames};
    memcpy(&values[3], &time->sample_rate_hz, sizeof(double));
    memcpy(&values[4], &time->speed_ratio, sizeof(double));
    memcpy(&values[5], &time->timecode_speed_ratio, sizeof(double));
    atomic_fetch_add_explicit(&driver->time_sequence, 1, memory_order_seq_cst);
    for (unsigned i = 0; i < 6; i++)
        atomic_store_explicit(&driver->time_values[i], values[i], memory_order_seq_cst);
    atomic_store_explicit(&driver->time_flags, time->flags, memory_order_seq_cst);
    atomic_fetch_add_explicit(&driver->time_sequence, 1, memory_order_seq_cst);
}

int32_t lnd_asio_read_time(lnd_asio_driver *driver, LND_ASIO_TIME *time) {
    for (unsigned attempt = 0; attempt < 8; attempt++) {
        uint32_t seq = atomic_load_explicit(&driver->time_sequence, memory_order_seq_cst);
        if (seq & 1)
            continue;
        uint64_t values[6];
        for (unsigned i = 0; i < 6; i++)
            values[i] = atomic_load_explicit(&driver->time_values[i], memory_order_seq_cst);
        uint32_t flags = atomic_load_explicit(&driver->time_flags, memory_order_seq_cst);
        if (seq != atomic_load_explicit(&driver->time_sequence, memory_order_seq_cst))
            continue;
        *time = (LND_ASIO_TIME){.sample_position_frames = values[0], .system_time_ns = values[1], .timecode_position_frames = values[2], .flags = flags};
        memcpy(&time->sample_rate_hz, &values[3], sizeof(double));
        memcpy(&time->speed_ratio, &values[4], sizeof(double));
        memcpy(&time->timecode_speed_ratio, &values[5], sizeof(double));
        return flags ? LND_OK : LND_ERR_STATE;
    }
    return LND_ERR_BUSY;
}

static bool lnd_asio_stream_process(lnd_asio_driver *driver, unsigned direction, float *pcm) {
    lnd_stream *stream = atomic_load_explicit(&driver->streams[direction], memory_order_seq_cst);
    if (!stream)
        return false;
    atomic_fetch_add_explicit(&stream->readers, 1, memory_order_seq_cst);
    bool active = atomic_load_explicit(&stream->active, memory_order_seq_cst) != 0;
    if (active) {
        lnd_callback_enter();
        stream->proc(stream->user, pcm, stream->cfg.period_frames);
        lnd_callback_leave();
    }
    atomic_fetch_sub_explicit(&stream->readers, 1, memory_order_seq_cst);
    return active;
}

static void lnd_asio_process(lnd_asio_driver *driver, const lnd_asio_block *block) {
    if (!lnd_load(&driver->accepting))
        return;
    uint32_t frames = driver->info.buffer_frames;
    uint32_t inputs = driver->info.active_input_channels;
    uint32_t outputs = driver->info.active_output_channels;
    lnd_asio_time_publish(driver, &block->time);
    lnd_stream *capture = lnd_load(&driver->streams[LND_DEVICE_INPUT]);
    bool input_active = capture && lnd_load(&capture->active);
    for (uint32_t channel = 0; input_active && channel < inputs; channel++) {
        driver->conversion[channel].pcm.read(driver->buffers[channel].buffers[block->index], driver->pcm[LND_DEVICE_INPUT] + channel, frames, inputs);
    }
    if (input_active)
        lnd_asio_stream_process(driver, LND_DEVICE_INPUT, driver->pcm[LND_DEVICE_INPUT]);
    bool output = outputs && lnd_asio_stream_process(driver, LND_DEVICE_OUTPUT, driver->pcm[LND_DEVICE_OUTPUT]);
    for (uint32_t channel = 0; channel < outputs; channel++) {
        lnd_asio_pcm *pcm = &driver->conversion[inputs + channel].pcm;
        void *buffer = driver->buffers[inputs + channel].buffers[block->index];
        if (output)
            pcm->write(driver->pcm[LND_DEVICE_OUTPUT] + channel, buffer, frames, outputs);
        else
            memset(buffer, 0, (size_t)frames * pcm->bytes);
    }
    if (driver->info.capabilities & LND_ASIO_CAP_OUTPUT_READY) {
        cwASIOError error = driver->abi->vt->outputReady(driver->abi);
        if (error != ASE_OK) {
            lnd_store(&driver->driver_error, (int32_t)error);
            lnd_asio_request(driver, LND_ASIO_RESET);
        }
    }
}

static void lnd_asio_render(void *user) {
    lnd_asio_driver *driver = user;
    DWORD task = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    if (mmcss)
        AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);
    lnd_thread_set_priority(LND_DEVICE_THREAD_PRIORITY_REALTIME);
    while (!lnd_load(&driver->render_stop)) {
        lnd_event_wait(&driver->render_event, UINT32_MAX);
        if (lnd_load(&driver->render_stop))
            break;
        for (;;) {
            uint32_t read = lnd_load(&driver->queue_read);
            if (read == lnd_load(&driver->queue_write))
                break;
            uint32_t expected = 0;
            if (!lnd_cas(&driver->processing, &expected, 1))
                break;
            read = lnd_load(&driver->queue_read);
            if (read == lnd_load(&driver->queue_write)) {
                lnd_store(&driver->processing, 0);
                break;
            }
            lnd_asio_block block = driver->queue[read % LND_ASIO_QUEUE];
            lnd_store(&driver->queue_read, read + 1);
            lnd_asio_process(driver, &block);
            atomic_fetch_and_explicit(&driver->buffers_busy, ~(1u << block.index), memory_order_release);
            lnd_store(&driver->processing, 0);
        }
    }
    if (mmcss)
        AvRevertMmThreadCharacteristics(mmcss);
}

static void lnd_asio_dispatch(lnd_asio_driver *driver, long index, cwASIOBool direct, const LND_ASIO_TIME *time) {
    if (!lnd_load(&driver->accepting))
        return;
    if (index != 0 && index != 1) {
        lnd_asio_request(driver, LND_ASIO_RESET);
        return;
    }
    uint32_t bit = 1u << (unsigned)index;
    if (atomic_fetch_or_explicit(&driver->buffers_busy, bit, memory_order_acq_rel) & bit) {
        lnd_asio_overload(driver);
        return;
    }
    lnd_asio_block block = {.index = (uint32_t)index, .time = *time};
    uint32_t expected = 0;
    if (direct && !(driver->config.flags & LND_ASIO_DEFER_PROCESS) && lnd_load(&driver->queue_read) == lnd_load(&driver->queue_write) &&
        lnd_cas(&driver->processing, &expected, 1)) {
        lnd_asio_process(driver, &block);
        atomic_fetch_and_explicit(&driver->buffers_busy, ~bit, memory_order_release);
        lnd_store(&driver->processing, 0);
        if (lnd_load(&driver->queue_read) != lnd_load(&driver->queue_write))
            lnd_event_signal(&driver->render_event);
        return;
    }
    expected = 0;
    if (!lnd_cas(&driver->enqueuing, &expected, 1)) {
        atomic_fetch_and_explicit(&driver->buffers_busy, ~bit, memory_order_release);
        lnd_asio_overload(driver);
        return;
    }
    uint32_t write = lnd_load(&driver->queue_write);
    if (write - lnd_load(&driver->queue_read) >= LND_ASIO_QUEUE) {
        atomic_fetch_and_explicit(&driver->buffers_busy, ~bit, memory_order_release);
        lnd_asio_overload(driver);
    } else {
        driver->queue[write % LND_ASIO_QUEUE] = block;
        lnd_store(&driver->queue_write, write + 1);
    }
    lnd_store(&driver->enqueuing, 0);
    lnd_event_signal(&driver->render_event);
}

static void lnd_asio_buffer_switch(unsigned slot, long index, cwASIOBool direct) {
    lnd_asio_driver *driver = lnd_asio_acquire(slot);
    if (driver && lnd_load(&driver->accepting)) {
        struct cwASIOTime time = {0};
        if (driver->abi->vt->getSamplePosition(driver->abi, &time.timeInfo.samplePosition, &time.timeInfo.systemTime) == ASE_OK)
            time.timeInfo.flags = kSamplePositionValid | kSystemTimeValid;
        time.timeInfo.sampleRate = driver->info.sample_rate_hz;
        time.timeInfo.flags |= kSampleRateValid;
        LND_ASIO_TIME decoded = lnd_asio_time_decode(&time);
        lnd_asio_dispatch(driver, index, direct, &decoded);
    }
    lnd_asio_release(slot);
}

static struct cwASIOTime *lnd_asio_buffer_time(unsigned slot, struct cwASIOTime *time, long index, cwASIOBool direct) {
    lnd_asio_driver *driver = lnd_asio_acquire(slot);
    if (driver && lnd_load(&driver->accepting)) {
        LND_ASIO_TIME decoded = lnd_asio_time_decode(time);
        if (time && (time->timeInfo.flags & (kSampleRateChanged | kClockSourceChanged))) {
            if (decoded.flags & LND_ASIO_TIME_RATE) {
                double rate = decoded.sample_rate_hz;
                lnd_store(&driver->recovery_rate, rate <= 768000 ? (uint32_t)(rate + 0.5) : 0);
            }
            lnd_asio_request(driver, LND_ASIO_RESET);
        }
        lnd_asio_dispatch(driver, index, direct, &decoded);
    }
    lnd_asio_release(slot);
    return time;
}

static void lnd_asio_rate_changed(unsigned slot, double rate) {
    lnd_asio_driver *driver = lnd_asio_acquire(slot);
    if (driver && !lnd_load(&driver->changing) && rate != driver->info.sample_rate_hz) {
        lnd_store(&driver->recovery_rate, isfinite(rate) && rate >= 1 && rate <= 768000 ? (uint32_t)(rate + 0.5) : 0);
        lnd_asio_request(driver, LND_ASIO_RESET);
    }
    lnd_asio_release(slot);
}

static long lnd_asio_message(unsigned slot, long selector, long value) {
    lnd_asio_driver *driver = lnd_asio_acquire(slot);
    long result = 0;
    if (driver) {
        long query = selector == kAsioSelectorSupported ? value : selector;
        switch (query) {
        case kAsioEngineVersion:
            result = selector == kAsioSelectorSupported ? 1 : 2;
            break;
        case kAsioSupportsTimeInfo:
        case kAsioSupportsInputMonitor:
            result = 1;
            break;
        case kAsioSupportsTimeCode:
            result = (driver->config.flags & LND_ASIO_TIMECODE) != 0;
            break;
        case kAsioResetRequest:
        case kAsioBufferSizeChange:
        case kAsioResyncRequest:
        case kAsioLatenciesChanged:
        case kAsioOverload:
            result = 1;
            if (selector == kAsioSelectorSupported)
                break;
            if (selector == kAsioOverload)
                lnd_add(&driver->overloads, 1);
            else if (selector == kAsioLatenciesChanged)
                lnd_asio_request(driver, LND_ASIO_LATENCIES);
            else {
                if (selector == kAsioBufferSizeChange) {
                    if (value <= 0 || value > (1 << 20)) {
                        result = 0;
                        break;
                    }
                    lnd_store(&driver->recovery_buffer, (uint32_t)value);
                }
                lnd_asio_request(driver, selector == kAsioResyncRequest ? LND_ASIO_RESYNC : LND_ASIO_RESET);
            }
            break;
        default:
            break;
        }
    }
    lnd_asio_release(slot);
    return result;
}

/* Driver callbacks have no user argument, so each slot needs its own functions. */
#define LND_ASIO_SLOT(N)                                                                                                                                       \
    static void lnd_asio_switch_##N(long index, cwASIOBool direct) { lnd_asio_buffer_switch(N, index, direct); }                                               \
    static void lnd_asio_rate_##N(double rate) { lnd_asio_rate_changed(N, rate); }                                                                             \
    static long lnd_asio_message_##N(long selector, long value, void *message, double *opt) {                                                                  \
        LND_UNUSED(message);                                                                                                                                   \
        LND_UNUSED(opt);                                                                                                                                       \
        return lnd_asio_message(N, selector, value);                                                                                                           \
    }                                                                                                                                                          \
    static struct cwASIOTime *lnd_asio_time_##N(struct cwASIOTime *time, long index, cwASIOBool direct) { return lnd_asio_buffer_time(N, time, index, direct); }

#define LND_ASIO_EACH(M) M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8) M(9) M(10) M(11) M(12) M(13) M(14) M(15)
LND_ASIO_EACH(LND_ASIO_SLOT)
#define LND_ASIO_CALLBACKS(N) {lnd_asio_switch_##N, lnd_asio_rate_##N, lnd_asio_message_##N, lnd_asio_time_##N},
static const struct cwASIOCallbacks lnd_asio_callbacks[] = {LND_ASIO_EACH(LND_ASIO_CALLBACKS)};

void lnd_asio_text(char *output, size_t capacity, const char *input, size_t length) {
    WCHAR wide[128];
    if (!capacity)
        return;
    output[0] = 0;
    size_t bytes = 0;
    while (bytes < length && input[bytes])
        bytes++;
    if (!bytes || bytes > 127)
        return;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, (int)bytes, wide, LND_COUNTOF(wide));
    if (!count)
        count = MultiByteToWideChar(CP_ACP, 0, input, (int)bytes, wide, LND_COUNTOF(wide));
    while (count > 0) {
        int needed = WideCharToMultiByte(CP_UTF8, 0, wide, count, nullptr, 0, nullptr, nullptr);
        if (needed > 0 && (size_t)needed < capacity) {
            if (WideCharToMultiByte(CP_UTF8, 0, wide, count, output, needed, nullptr, nullptr))
                output[needed] = 0;
            return;
        }
        count--;
        if (count && wide[count - 1] >= 0xd800 && wide[count - 1] <= 0xdbff)
            count--;
    }
}

int32_t lnd_asio_error(lnd_asio_driver *driver, cwASIOError error) {
    if (error == ASE_OK || error == ASE_SUCCESS)
        return LND_OK;
    lnd_store(&driver->driver_error, (int32_t)error);
    if (driver->abi) {
        char message[124] = {0};
        driver->abi->vt->getErrorMessage(driver->abi, message);
        lnd_asio_text(driver->info.driver_error_message, sizeof driver->info.driver_error_message, message, sizeof message);
    }
    switch (error) {
    case ASE_NotPresent:
        return LND_ERR_UNSUPPORTED;
    case ASE_NoMemory:
        return LND_ERR_OUT_OF_MEMORY;
    case ASE_InvalidParameter:
        return LND_ERR_INVALID_ARG;
    case ASE_InvalidMode:
        return LND_ERR_STATE;
    case ASE_NoClock:
        return LND_ERR_NO_DEVICE;
    default:
        return LND_ERR_EXTERNAL;
    }
}

void lnd_asio_refresh_latencies(lnd_asio_driver *driver) {
    long input = 0, output = 0;
    if (driver->abi->vt->getLatencies(driver->abi, &input, &output) == ASE_OK && input >= 0 && output >= 0) {
        driver->info.input_latency_frames = (uint32_t)input;
        driver->info.output_latency_frames = (uint32_t)output;
        for (unsigned direction = 0; direction < 2; direction++) {
            lnd_stream *stream = lnd_load(&driver->streams[direction]);
            if (stream)
                stream->cfg.latency_frames = direction == LND_DEVICE_INPUT ? (uint32_t)input : (uint32_t)output;
        }
    }
}

int32_t lnd_asio_query(lnd_asio_driver *driver) {
    lnd_asio_abi *abi = driver->abi;
    long inputs = 0, outputs = 0, minimum = 0, maximum = 0, preferred = 0, granularity = 0;
    double rate = 0;
    int32_t result = lnd_asio_error(driver, abi->vt->getChannels(abi, &inputs, &outputs));
    if (result != LND_OK)
        return result;
    if (inputs < 0 || outputs < 0 || inputs > 4096 || outputs > 4096 || (!inputs && !outputs))
        return LND_ERR_FORMAT;
    result = lnd_asio_error(driver, abi->vt->getBufferSize(abi, &minimum, &maximum, &preferred, &granularity));
    if (result != LND_OK)
        return result;
    uint32_t checked;
    result = lnd_asio_buffer_size(0, minimum, maximum, preferred, granularity, &checked);
    if (result != LND_OK)
        return result;
    result = lnd_asio_error(driver, abi->vt->getSampleRate(abi, &rate));
    if (result != LND_OK)
        return result;
    if (!isfinite(rate) || rate < 1 || rate > 768000 || fabs(rate - floor(rate + 0.5)) > 0.001)
        return LND_ERR_FORMAT;
    char name[32] = {0};
    abi->vt->getDriverName(abi, name);
    name[sizeof name - 1] = 0;
    lnd_asio_text(driver->info.driver_name, sizeof driver->info.driver_name, name, sizeof name);
    driver->info.driver_version = abi->vt->getDriverVersion(abi);
    driver->info.input_channels = (uint32_t)inputs;
    driver->info.output_channels = (uint32_t)outputs;
    driver->info.sample_rate_hz = (uint32_t)(rate + 0.5);
    driver->info.min_buffer_frames = (uint32_t)minimum;
    driver->info.max_buffer_frames = (uint32_t)maximum;
    driver->info.preferred_buffer_frames = (uint32_t)preferred;
    driver->info.buffer_granularity = (int32_t)granularity;
    lnd_asio_refresh_latencies(driver);
    return LND_OK;
}

int32_t lnd_asio_load(lnd_asio_driver *driver) {
    if (driver->abi)
        return LND_OK;
    int32_t result = lnd_asio_driver_load(&driver->clsid, &driver->abi);
    if (result != LND_OK)
        return result;
    lnd_asio_abi *abi = driver->abi;
    if (!abi) return LND_ERR_EXTERNAL;
    if (!abi->vt->init(abi, driver->config.window ? driver->config.window : driver->owner->window))
        result = LND_ERR_NO_DEVICE;
    if (result == LND_OK && driver->clock_index >= 0)
        result = lnd_asio_error(driver, abi->vt->setClockSource(abi, driver->clock_index));
    if (result == LND_OK)
        result = lnd_asio_query(driver);
    if (result != LND_OK) {
        lnd_asio_driver_unload(abi);
        driver->abi = nullptr;
        return result;
    }
    static const struct {
        long selector;
        uint32_t flag;
    } capabilities[] = {
        {kAsioCanTimeInfo, LND_ASIO_CAP_TIME_INFO},       {kAsioCanTimeCode, LND_ASIO_CAP_TIMECODE},      {kAsioCanInputMonitor, LND_ASIO_CAP_INPUT_MONITOR},
        {kAsioCanInputGain, LND_ASIO_CAP_INPUT_GAIN},     {kAsioCanInputMeter, LND_ASIO_CAP_INPUT_METER}, {kAsioCanOutputGain, LND_ASIO_CAP_OUTPUT_GAIN},
        {kAsioCanOutputMeter, LND_ASIO_CAP_OUTPUT_METER}, {kAsioCanTransport, LND_ASIO_CAP_TRANSPORT},
    };
    driver->info.capabilities = 0;
    for (size_t i = 0; i < LND_COUNTOF(capabilities); i++)
        if (abi->vt->future(abi, capabilities[i].selector, nullptr) == ASE_SUCCESS)
            driver->info.capabilities |= capabilities[i].flag;
    if (abi->vt->outputReady(abi) == ASE_OK)
        driver->info.capabilities |= LND_ASIO_CAP_OUTPUT_READY;
    return LND_OK;
}

void lnd_asio_stream_quiesce(lnd_stream *stream) {
    atomic_store_explicit(&stream->active, 0, memory_order_seq_cst);
    while (atomic_load_explicit(&stream->readers, memory_order_seq_cst))
        Sleep(0);
}

void lnd_asio_stop(lnd_asio_driver *driver) {
    lnd_store(&driver->accepting, 0);
    if (driver->started) {
        lnd_asio_error(driver, driver->abi->vt->stop(driver->abi));
        driver->started = false;
    }
    if (driver->slot >= 0)
        while (atomic_load_explicit(&lnd_asio_slots[driver->slot].readers, memory_order_seq_cst))
            Sleep(0);
    for (;;) {
        uint32_t expected = 0;
        if (lnd_cas(&driver->processing, &expected, 1))
            break;
        Sleep(0);
    }
    lnd_store(&driver->queue_read, lnd_load(&driver->queue_write));
    lnd_store(&driver->buffers_busy, 0);
    lnd_store(&driver->processing, 0);
}

int32_t lnd_asio_start(lnd_asio_driver *driver) {
    if (driver->started)
        return LND_OK;
    lnd_store(&driver->accepting, 1);
    int32_t result = lnd_asio_error(driver, driver->abi->vt->start(driver->abi));
    if (result == LND_OK)
        driver->started = true;
    else {
        lnd_store(&driver->accepting, 0);
        driver->abi->vt->stop(driver->abi);
        while (lnd_load(&driver->processing))
            Sleep(0);
    }
    return result;
}

void lnd_asio_dispose(lnd_asio_driver *driver) {
    lnd_asio_stop(driver);
    if (driver->slot >= 0) {
        atomic_store_explicit(&lnd_asio_slots[driver->slot].driver, nullptr, memory_order_seq_cst);
        while (atomic_load_explicit(&lnd_asio_slots[driver->slot].readers, memory_order_seq_cst))
            Sleep(0);
    }
    lnd_store(&driver->render_stop, 1);
    if (driver->render_event.handle)
        lnd_event_signal(&driver->render_event);
    lnd_thread_join(&driver->render_thread);
    lnd_event_free(&driver->render_event);
    if (driver->timecode_enabled) {
        driver->abi->vt->future(driver->abi, kAsioDisableTimeCodeRead, nullptr);
        driver->timecode_enabled = false;
    }
    if (driver->buffers_created) {
        lnd_asio_error(driver, driver->abi->vt->disposeBuffers(driver->abi));
        driver->buffers_created = false;
    }
    driver->slot = -1;
    lnd_free(driver->buffers);
    lnd_free(driver->conversion);
    lnd_free_aligned(driver->pcm[0]);
    lnd_free_aligned(driver->pcm[1]);
    driver->buffers = nullptr;
    driver->conversion = nullptr;
    driver->pcm[0] = driver->pcm[1] = nullptr;
    driver->buffer_count = 0;
    driver->info.active_input_channels = driver->info.active_output_channels = 0;
    driver->info.buffer_frames = 0;
    lnd_store(&driver->time_flags, 0);
}

void lnd_asio_unload(lnd_asio_driver *driver) {
    lnd_asio_dispose(driver);
    if (driver->abi)
        lnd_asio_driver_unload(driver->abi);
    driver->abi = nullptr;
}

void lnd_asio_invalidate(lnd_asio_driver *driver, int32_t error) {
    lnd_asio_stop(driver);
    for (unsigned i = 0; i < 2; i++) {
        lnd_stream *stream = atomic_exchange_explicit(&driver->streams[i], nullptr, memory_order_seq_cst);
        if (stream) {
            lnd_asio_stream_quiesce(stream);
            lnd_store(&stream->failed, error);
        }
    }
    lnd_asio_unload(driver);
    driver->info.resets++;
}

int32_t lnd_asio_prepare(lnd_asio_driver *driver, const lnd_stream_cfg *cfg) {
    if (driver->buffers_created)
        return LND_OK;
    lnd_exchange(&driver->pending, 0);
    int32_t result = lnd_asio_load(driver);
    if (result != LND_OK)
        return result;
    uint32_t recovery_rate = lnd_load(&driver->recovery_rate);
    uint32_t rate = driver->config.sample_rate_hz ? driver->config.sample_rate_hz : recovery_rate ? recovery_rate : cfg->sample_rate_hz;
    if (!rate)
        rate = driver->info.sample_rate_hz;
    if (rate != driver->info.sample_rate_hz) {
        result = lnd_asio_error(driver, driver->abi->vt->canSampleRate(driver->abi, rate));
        if (result != LND_OK)
            goto fail;
        lnd_store(&driver->changing, 1);
        result = lnd_asio_error(driver, driver->abi->vt->setSampleRate(driver->abi, rate));
        lnd_store(&driver->changing, 0);
        if (result != LND_OK)
            goto fail;
        result = lnd_asio_query(driver);
        if (result != LND_OK)
            goto fail;
        if (driver->info.sample_rate_hz != rate) {
            result = LND_ERR_FORMAT;
            goto fail;
        }
    }
    uint32_t requested = lnd_load(&driver->recovery_buffer);
    if (!requested)
        requested = driver->config.buffer_frames ? driver->config.buffer_frames : cfg->period_frames;
    result = lnd_asio_buffer_size(requested, driver->info.min_buffer_frames, driver->info.max_buffer_frames, driver->info.preferred_buffer_frames,
                                  driver->info.buffer_granularity, &driver->info.buffer_frames);
    if (result != LND_OK)
        goto fail;
    uint32_t inputs = driver->config.flags & LND_ASIO_DISABLE_INPUT ? 0
                      : driver->config.input_channels               ? driver->config.input_channels
                                                                    : LND_MIN(driver->info.input_channels, LND_ASIO_MAX_CHANNELS);
    uint32_t outputs = driver->config.flags & LND_ASIO_DISABLE_OUTPUT ? 0
                       : driver->config.output_channels               ? driver->config.output_channels
                                                                      : LND_MIN(driver->info.output_channels, cfg->channels ? cfg->channels : 2);
    if (!inputs && !outputs) {
        result = LND_ERR_INVALID_ARG;
        goto fail;
    }
    driver->info.active_input_channels = inputs;
    driver->info.active_output_channels = outputs;
    driver->buffer_count = inputs + outputs;
    driver->buffers = lnd_alloc_zero(driver->buffer_count * sizeof *driver->buffers);
    driver->conversion = lnd_alloc_zero(driver->buffer_count * sizeof *driver->conversion);
    if (!driver->buffers || !driver->conversion) {
        result = LND_ERR_OUT_OF_MEMORY;
        goto fail;
    }
    for (uint32_t i = 0; i < driver->buffer_count; i++) {
        bool input = i < inputs;
        uint32_t channel = input ? i : i - inputs;
        uint32_t mapped = input ? (driver->config.input_channels ? driver->config.input_map[channel] : channel)
                                : (driver->config.output_channels ? driver->config.output_map[channel] : channel);
        if (mapped >= (input ? driver->info.input_channels : driver->info.output_channels)) {
            result = LND_ERR_INVALID_ARG;
            goto fail;
        }
        struct cwASIOChannelInfo info = {.channel = (long)mapped, .isInput = input};
        result = lnd_asio_error(driver, driver->abi->vt->getChannelInfo(driver->abi, &info));
        if (result != LND_OK)
            goto fail;
        if (!lnd_asio_pcm_get((int32_t)info.type, &driver->conversion[i].pcm)) {
            result = LND_ERR_UNSUPPORTED;
            goto fail;
        }
        driver->conversion[i].channel = channel;
        driver->conversion[i].input = input;
        driver->buffers[i].isInput = input;
        driver->buffers[i].channelNum = (long)mapped;
    }
    if (inputs)
        driver->pcm[LND_DEVICE_INPUT] = lnd_alloc_aligned((size_t)inputs * driver->info.buffer_frames * sizeof(float), LND_CACHE_LINE);
    if (outputs)
        driver->pcm[LND_DEVICE_OUTPUT] = lnd_alloc_aligned((size_t)outputs * driver->info.buffer_frames * sizeof(float), LND_CACHE_LINE);
    if ((inputs && !driver->pcm[LND_DEVICE_INPUT]) || (outputs && !driver->pcm[LND_DEVICE_OUTPUT])) {
        result = LND_ERR_OUT_OF_MEMORY;
        goto fail;
    }
    for (unsigned i = 0; i < LND_ASIO_SLOTS; i++) {
        void *expected = nullptr;
        if (atomic_compare_exchange_strong_explicit(&lnd_asio_slots[i].driver, &expected, driver, memory_order_seq_cst, memory_order_seq_cst)) {
            driver->slot = (int32_t)i;
            break;
        }
    }
    if (driver->slot < 0) {
        result = LND_ERR_BUSY;
        goto fail;
    }
    lnd_store(&driver->render_stop, 0);
    lnd_store(&driver->queue_read, 0);
    lnd_store(&driver->queue_write, 0);
    result = lnd_event_init(&driver->render_event);
    if (result == LND_OK)
        result = lnd_thread_create(&driver->render_thread, lnd_asio_render, driver);
    if (result != LND_OK)
        goto fail;
    struct cwASIOIoFormat format = {.FormatType = kASIOPCMFormat};
    cwASIOError io = driver->abi->vt->future(driver->abi, kAsioSetIoFormat, &format);
    if (io != ASE_OK && io != ASE_SUCCESS && io != ASE_NotPresent) {
        result = lnd_asio_error(driver, io);
        goto fail;
    }
    result = lnd_asio_error(driver, driver->abi->vt->createBuffers(driver->abi, driver->buffers, (long)driver->buffer_count, (long)driver->info.buffer_frames,
                                                                   &lnd_asio_callbacks[driver->slot]));
    if (result != LND_OK) {
        driver->abi->vt->disposeBuffers(driver->abi);
        goto fail;
    }
    driver->buffers_created = true;
    for (uint32_t i = 0; i < driver->buffer_count; i++) {
        for (unsigned half = 0; half < 2; half++) {
            if (!driver->buffers[i].buffers[half]) {
                result = LND_ERR_FORMAT;
                goto fail;
            }
            if (!driver->buffers[i].isInput)
                memset(driver->buffers[i].buffers[half], 0, (size_t)driver->info.buffer_frames * driver->conversion[i].pcm.bytes);
        }
    }
    if (driver->config.flags & LND_ASIO_TIMECODE) {
        result = lnd_asio_error(driver, driver->abi->vt->future(driver->abi, kAsioEnableTimeCodeRead, nullptr));
        if (result != LND_OK)
            goto fail;
        driver->timecode_enabled = true;
    }
    lnd_asio_refresh_latencies(driver);
    return LND_OK;
fail:
    lnd_asio_unload(driver);
    return result;
}
