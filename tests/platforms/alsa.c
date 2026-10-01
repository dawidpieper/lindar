#include "io/devices/backend.h"
#include "src/atomic.h"
#include "src/thread.h"
#include "src/format.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%d: %s\n", __LINE__, #x);                                                                                                                  \
        }                                                                                                                                                      \
    } while (0)
typedef struct state {
    lnd_event ready;
    lnd_atomic_u32 calls;
    uint32_t channels, format;
    bool capture;
} state;
static void process(void *user, void *data, uint64_t frames) {
    state *s = user;
    if (!s->capture) memset(data, s->format == LND_FORMAT_U8 ? 128 : 0, (size_t)frames * s->channels * lnd_format_bytes(s->format));
    lnd_add(&s->calls, 1);
    lnd_event_signal(&s->ready);
}
int main(void) {
    lnd_backend b = {.vt = LND_DeviceBackendFind("alsa")};
    CHECK(b.vt != nullptr);
    if (!b.vt) return 1;
    CHECK(b.vt->init(&b) == LND_OK);
    lnd_device_list list = {0};
    CHECK(b.vt->enumerate(&b, LND_DEVICE_OUTPUT, &list) == LND_OK);
    CHECK(list.count > 0 && (list.items[0]->flags & LND_DEVICE_FLAG_DEFAULT));
    lnd_device_list_free(&list);
    state state = {0};
    CHECK(lnd_event_init(&state.ready) == LND_OK);
    for (unsigned capture = 0; capture < 2; capture++) {
        for (int32_t format = LND_FORMAT_U8; format <= LND_FORMAT_F64; format++) {
            lnd_device *d = lnd_device_new(capture ? LND_DEVICE_INPUT : LND_DEVICE_OUTPUT, "ALSA null", "null");
            lnd_stream_cfg cfg = {.sample_rate_hz = 44100, .channels = 2, .format = format, .period_frames = 128, .periods = 3};
            lnd_stream *s = nullptr;
            state.capture = capture;
            state.format = format;
            state.channels = 2;
            CHECK((capture ? b.vt->open_capture : b.vt->open)(&b, d, &cfg, process, &state, &s) == LND_OK);
            if (!s) {
                lnd_device_free(d);
                continue;
            }
            CHECK(cfg.sample_rate_hz == 44100 && cfg.channels == 2 && cfg.format == format && cfg.buffer_frames >= cfg.period_frames);
            for (unsigned pass = 0; pass < 3; pass++) {
                lnd_event_wait(&state.ready, 0);
                lnd_store(&state.calls, 0);
                CHECK(b.vt->start(s) == LND_OK);
                CHECK(lnd_event_wait(&state.ready, 2000));
                CHECK(b.vt->stop(s) == LND_OK);
                uint32_t count = lnd_load(&state.calls);
                CHECK(count > 0);
                lnd_sleep_ms(2);
                CHECK(lnd_load(&state.calls) == count);
                CHECK(b.vt->status(s) == LND_OK);
            }
            b.vt->close(s);
            lnd_device_free(d);
        }
    }
    lnd_device *d = lnd_device_new(LND_DEVICE_OUTPUT, "missing", "lindar_missing_pcm_8319");
    lnd_stream *s = nullptr;
    lnd_stream_cfg cfg = {.sample_rate_hz = 48000, .channels = 2, .periods = 2};
    CHECK(b.vt->open(&b, d, &cfg, process, &state, &s) != LND_OK && s == nullptr);
    cfg.loopback = true;
    CHECK(b.vt->open_capture(&b, d, &cfg, process, &state, &s) == LND_ERR_UNSUPPORTED);
    lnd_device_free(d);
    lnd_event_free(&state.ready);
    b.vt->free(&b);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
