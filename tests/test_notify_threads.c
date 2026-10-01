#include "lindar_notify.h"
#include "lindar.h"
#include "src/thread.h"
#include "src/atomic.h"

#include <stdio.h>
#include <stdlib.h>

static void *allocate(void *user, size_t bytes) {
    (void)user;
    return malloc(bytes);
}
static void release(void *user, void *memory) {
    (void)user;
    free(memory);
}

typedef struct producer {
    LND_SOUND *sound;
    lnd_atomic_u32 done;
    lnd_atomic_u32 failed;
} producer;

static void produce(void *user) {
    producer *p = user;
    int16_t data[4];
    LND_PCM pcm = {.data = data, .frames = 4, .channels = 1, .format = LND_FORMAT_S16};
    for (unsigned i = 0; i < 20000;) {
        int64_t result = LND_SoundRenderPcm(p->sound, &pcm, 0, 4);
        if (result == 4) i++;
        else if (result == LND_ERR_BUSY) lnd_sleep_ms(0);
        else {
            lnd_store(&p->failed, 1);
            break;
        }
    }
    lnd_store(&p->done, 1);
}

static void notified(void *user, const LND_NOTIFICATION *e) {
    uint64_t *count = user;
    if (e->type == LND_NOTIFY_POSITION && e->position_frames == 2 && e->sample_rate_hz == 8000)
        (*count)++;
}

int main(void) {
    if (LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .free = release}) != LND_OK ||
        LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_MANUAL) != LND_OK || LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) != LND_OK || LND_LibraryInit() != LND_OK)
        return 1;
    int16_t data[4] = {0};
    LND_PCM pcm = {.data = data, .frames = 4, .channels = 1, .format = LND_FORMAT_S16};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .channels = 1, .sample_rate_hz = 8000});
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    uint64_t count = 0;
    LND_SUBSCRIPTION_CONFIG cfg = {.type = LND_NOTIFY_POSITION, .position_frames = 2, .notification_capacity = 64, .proc = notified, .user = &count};
    LND_SUBSCRIPTION *sub = LND_SoundSubscribe(sound, &cfg), *cancel = LND_SoundSubscribe(sound, &cfg);
    if (!sub || !cancel || LND_SoundSetLoop(sound, true) != LND_OK || LND_SoundPlay(sound) != LND_OK)
        return 1;
    producer p = {.sound = sound};
    lnd_thread thread;
    if (lnd_thread_create(&thread, produce, &p) != LND_OK)
        return 1;
    if (LND_SubscriptionFree(cancel) != LND_OK)
        return 1;
    while (!lnd_load(&p.done))
        LND_LibraryUpdate();
    lnd_thread_join(&thread);
    LND_LibraryUpdate();
    uint64_t dropped = LND_SubscriptionGetDroppedCount(sub);
    int result = lnd_load(&p.failed) || count + dropped != 20000;
    printf("%llu delivered, %llu dropped, expected 20000\n", (unsigned long long)count, (unsigned long long)dropped);
    LND_LibraryFree();
    return result;
}
