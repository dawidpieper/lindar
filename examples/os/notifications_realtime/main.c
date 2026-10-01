#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "lindar_notify.h"
#include "lindar.h"
#include "lnd_modules.h"
#if LND_MODULE_DEVICES
#include "lindar_devices.h"
#endif

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void pause_ms(unsigned ms) { Sleep(ms); }
#else
#include <time.h>
static void pause_ms(unsigned ms) {
    struct timespec delay = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000};
    nanosleep(&delay, nullptr);
}
#endif

static void notified(void *user, const LND_NOTIFICATION *event) {
    _Atomic unsigned *events = user;
    atomic_fetch_or(events, event->type == LND_NOTIFY_END ? 1u : 2u);
}

int main(int argc, char **argv) {
    // Automatic notifications may run on a library thread.
    bool manual = argc == 2 && !strcmp(argv[1], "--manual");
    if (argc > 1 && !manual) {
        fprintf(stderr, "notifications_realtime [--manual]\n");
        return 2;
    }
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_REALTIME) != LND_OK || LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) != LND_OK)
        return 1;
#if LND_MODULE_DEVICES
    if (LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("null")) != LND_OK || LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0) != LND_OK)
        return 1;
#endif
    if (LND_LibraryInit() != LND_OK)
        return 1;
    int16_t input[32] = {0}, output[8];
    LND_PCM pcm = {.data = input, .frames = 32, .channels = 1, .format = LND_FORMAT_S16};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .channels = 1, .sample_rate_hz = 8000});
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    _Atomic unsigned events = 0;
    LND_SUBSCRIPTION_CONFIG config = {.type = LND_NOTIFY_END, .flags = manual ? LND_NOTIFY_MANUAL : 0, .proc = notified, .user = &events};
    LND_SUBSCRIPTION *end = LND_SoundSubscribe(sound, &config);
    config.type = LND_NOTIFY_POSITION;
    config.position_frames = 17;
    config.flags |= LND_NOTIFY_ONCE;
    LND_SUBSCRIPTION *position = LND_SoundSubscribe(sound, &config);
    LND_RENDERER *renderer =
        LND_RendererCreateProc(&(LND_RENDERER_CONFIG){.render = LND_SoundRenderPcm, .user = sound, .channels = 1, .sample_rate_hz = 8000, .block_frames = 8});
    int result = 1;
    if (!source || !sound || !end || !position || !renderer || LND_SoundPlay(sound) != LND_OK)
        goto done;
    if (LND_SubscriptionIsAutomatic(end) == manual)
        goto done;
    pcm = (LND_PCM){.data = output, .frames = 8, .channels = 1, .format = LND_FORMAT_S16};
    for (unsigned i = 0; i < 4; i++)
        if (LND_RendererFillPcm(renderer, &pcm, 0, 8) != LND_OK)
            goto done;
    for (unsigned i = 0; i < 1000 && atomic_load(&events) != 3; i++) {
        if (manual && LND_LibraryUpdate() != LND_OK)
            goto done;
        pause_ms(2);
    }
    result = atomic_load(&events) != 3;
    printf("%s: position=%s end=%s\n", manual ? "Update" : "automatic", atomic_load(&events) & 2 ? "received" : "missing",
           atomic_load(&events) & 1 ? "received" : "missing");
done:
    LND_LibraryFree();
    return result;
}
