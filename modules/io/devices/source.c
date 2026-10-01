#include "context.h"
#include "engine.h"
#include "playback/graph/context.h"
#include "src/config.h"
#include "src/error.h"
#include "capture.h"
#include "playback/graph/sound.h"

LND_SOURCE *LND_SourceCreateDevice(LND_DEVICE *device, uint32_t channels, uint32_t sample_rate_hz, uint32_t flags) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    const uint32_t known = LND_CAPTURE_RESAMPLE | LND_CAPTURE_LOOPBACK | LND_CAPTURE_EXCLUSIVE | LND_CAPTURE_NONBLOCKING;
    if (channels > LND_MAX_CHANNELS || (flags & ~known)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_context_gc();
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_STATE);
    }
    lnd_device *d = device;
    if (!d) d = lnd_cfg_ptr((flags & LND_CAPTURE_LOOPBACK) ? LND_CFG_DEVICES_OUTPUT_DEVICE : LND_CFG_DEVICES_INPUT_DEVICE);
    if (!d) d = (flags & LND_CAPTURE_LOOPBACK) ? LND_DEVICE_DEFAULT_OUTPUT : LND_DEVICE_DEFAULT_INPUT;
    bool follow = lnd_device_is_default_handle(d);
    d = lnd_device_resolve(d);
    if (!d) {
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_NO_DEVICE);
    }
    lnd_instance *i = lnd_instance_open_capture(d, sample_rate_hz, flags, follow);
    if (!i) {
        lnd_context_unlock();
        return nullptr;
    }
    lnd_source *source = lnd_capture_source_create(i->capture, channels, sample_rate_hz, flags);
    if (!source) {
        lnd_capture *c = i->capture;
        lnd_instance_close(i);
        lnd_capture_free(c);
        lnd_context_unlock();
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    }
    lnd_graph_source *s = lnd_source_obj_create(source, 0, LND_FORMAT_F32);
    lnd_context_unlock();
    return (LND_SOURCE *)s;
}
