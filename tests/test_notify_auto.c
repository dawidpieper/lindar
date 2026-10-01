#include "lindar_notify.h"
#include "lindar.h"
#include "src/atomic.h"
#include "src/context.h"
#include "src/thread.h"
#include "lnd_modules.h"
#if LND_MODULE_DEVICES
#include "lindar_devices.h"
#endif
#if LND_MODULE_GRAPH
#include "lindar_graph.h"
#endif

#include <stdio.h>
#include <stdlib.h>

static unsigned checks, failures;
static lnd_atomic_u32 live, attempts, fail_at = UINT32_MAX;
static thread_local unsigned thread_kind;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)
#define REQUIRE(x)                                                                                                                                             \
    do {                                                                                                                                                       \
        CHECK(x);                                                                                                                                              \
        if (failures)                                                                                                                                          \
            exit(1);                                                                                                                                           \
    } while (0)

static void *allocate(void *user, size_t size) {
    (void)user;
    if (lnd_add(&attempts, 1) == lnd_load(&fail_at))
        return nullptr;
    void *p = malloc(size);
    if (p)
        lnd_add(&live, 1);
    return p;
}

static void *resize(void *user, void *memory, size_t size) {
    (void)user;
    if (lnd_add(&attempts, 1) == lnd_load(&fail_at))
        return nullptr;
    void *p = realloc(memory, size);
    if (p && !memory)
        lnd_add(&live, 1);
    return p;
}

static void release(void *user, void *memory) {
    (void)user;
    if (memory)
        lnd_sub(&live, 1);
    free(memory);
}

static void begin(void) {
    REQUIRE(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .realloc = resize, .free = release}) == LND_OK);
    REQUIRE(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_REALTIME) == LND_OK);
    REQUIRE(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) == LND_OK);
#if LND_MODULE_DEVICES
    REQUIRE(LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("null")) == LND_OK);
    REQUIRE(LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0) == LND_OK);
#endif
    REQUIRE(LND_LibraryInit() == LND_OK);
}

static void finish(void) {
    LND_LibraryFree();
    CHECK(lnd_load(&live) == 0);
}

static LND_SOURCE *source_create(LND_SOUND **sound) {
    static int16_t data[4];
    LND_PCM pcm = {.data = data, .frames = 4, .channels = 1, .format = LND_FORMAT_S16};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .channels = 1, .sample_rate_hz = 8000});
    REQUIRE(source != nullptr);
    *sound = LND_SourceEnsureSound(source, nullptr);
    REQUIRE(*sound != nullptr);
    REQUIRE(LND_SoundPlay(*sound) == LND_OK);
    return source;
}

static int64_t render(LND_SOUND *sound) {
    int16_t data[4];
    LND_PCM pcm = {.data = data, .frames = 4, .channels = 1, .format = LND_FORMAT_S16};
    return LND_SoundRenderPcm(sound, &pcm, 0, 4);
}

typedef struct receiver {
    lnd_atomic_u32 count, failed;
    lnd_event ready, gate;
    LND_SUBSCRIPTION *subscription;
    unsigned expected_thread;
    int32_t type;
    uint64_t position;
    bool block, reenter;
} receiver;

static void receiver_init(receiver *r, unsigned expected_thread, int32_t type, uint64_t position) {
    *r = (receiver){.expected_thread = expected_thread, .type = type, .position = position};
    REQUIRE(lnd_event_init(&r->ready) == LND_OK);
    REQUIRE(lnd_event_init(&r->gate) == LND_OK);
}

static void receiver_free(receiver *r) {
    CHECK(!lnd_load(&r->failed));
    lnd_event_free(&r->gate);
    lnd_event_free(&r->ready);
}

static void notified(void *user, const LND_NOTIFICATION *event) {
    receiver *r = user;
    if (thread_kind != r->expected_thread || event->type != r->type || event->position_frames != r->position || event->sample_rate_hz != 8000)
        lnd_store(&r->failed, 1);
    if (r->reenter) {
        if (LND_LibraryUpdate() != LND_ERR_BUSY || LND_SubscriptionFree(r->subscription) != LND_ERR_BUSY)
            lnd_store(&r->failed, 1);
        LND_LibraryFree();
        if (LND_ErrorGetLast() != LND_ERR_BUSY)
            lnd_store(&r->failed, 1);
    }
    lnd_add(&r->count, 1);
    lnd_event_signal(&r->ready);
    if (r->block && !lnd_event_wait(&r->gate, 3000))
        lnd_store(&r->failed, 1);
}

static LND_SUBSCRIPTION_CONFIG config(receiver *r, uint32_t flags) {
    return (LND_SUBSCRIPTION_CONFIG){.type = r->type, .position_frames = r->position, .notification_capacity = 64, .flags = flags, .proc = notified, .user = r};
}

static void delivery(void) {
    begin();
    LND_SOUND *sound;
    LND_SOURCE *source = source_create(&sound);
    receiver automatic, manual, cancelled;
    receiver_init(&automatic, 0, LND_NOTIFY_END, 4);
    receiver_init(&manual, 1, LND_NOTIFY_END, 4);
    receiver_init(&cancelled, 0, LND_NOTIFY_END, 4);
    LND_SUBSCRIPTION_CONFIG a = config(&automatic, 0), m = config(&manual, LND_NOTIFY_MANUAL), c = config(&cancelled, 0);
    automatic.subscription = LND_SoundSubscribe(sound, &a);
    manual.subscription = LND_SoundSubscribe(sound, &m);
    cancelled.subscription = LND_SoundSubscribe(sound, &c);
    LND_SUBSCRIPTION *poll = LND_SoundSubscribe(sound, &(LND_SUBSCRIPTION_CONFIG){.type = LND_NOTIFY_END});
    REQUIRE(automatic.subscription && manual.subscription && cancelled.subscription && poll);
    CHECK(LND_SubscriptionIsAutomatic(automatic.subscription));
    CHECK(!LND_SubscriptionIsAutomatic(manual.subscription));
    CHECK(!LND_SubscriptionIsAutomatic(poll));
    CHECK(!LND_SubscriptionIsAutomatic(nullptr));
    automatic.reenter = manual.reenter = true;
    LND_NOTIFICATION event;
    CHECK(LND_SubscriptionRead(automatic.subscription, &event) == LND_ERR_STATE);
    lnd_context_lock();
    CHECK(render(sound) == 4);
    CHECK(LND_LibraryUpdate() == LND_OK);
    CHECK(lnd_load(&manual.count) == 1 && !lnd_load(&automatic.count));
    CHECK(LND_SubscriptionRead(poll, &event) == 1 && event.type == LND_NOTIFY_END);
    CHECK(LND_SubscriptionFree(cancelled.subscription) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(!LND_SubscriptionIsAttached(automatic.subscription));
    lnd_context_unlock();
    CHECK(lnd_event_wait(&automatic.ready, 2000));
    CHECK(LND_LibraryUpdate() == LND_OK);
    CHECK(lnd_load(&automatic.count) == 1 && !lnd_load(&cancelled.count));
    CHECK(LND_SubscriptionFree(automatic.subscription) == LND_OK);
    CHECK(LND_SubscriptionFree(manual.subscription) == LND_OK);
    CHECK(LND_SubscriptionFree(poll) == LND_OK);
    finish();
    receiver_free(&automatic);
    receiver_free(&manual);
    receiver_free(&cancelled);
}

static void allocation_failure(void) {
    begin();
    LND_SOUND *sound;
    source_create(&sound);
    receiver r;
    receiver_init(&r, 0, LND_NOTIFY_END, 4);
    LND_SUBSCRIPTION_CONFIG c = config(&r, 0);
    lnd_context_lock();
    lnd_store(&fail_at, lnd_load(&attempts) + 1);
    CHECK(!LND_SoundSubscribe(sound, &c));
    CHECK(LND_ErrorGetLast() == LND_ERR_OUT_OF_MEMORY);
    lnd_store(&fail_at, UINT32_MAX);
    r.subscription = LND_SoundSubscribe(sound, &c);
    REQUIRE(r.subscription != nullptr);
    uint32_t before = lnd_load(&attempts);
    CHECK(render(sound) == 4);
    CHECK(LND_LibraryUpdate() == LND_OK);
    CHECK(lnd_load(&attempts) == before);
    lnd_context_unlock();
    CHECK(lnd_event_wait(&r.ready, 2000));
    CHECK(LND_SubscriptionFree(r.subscription) == LND_OK);
    CHECK(lnd_load(&attempts) == before);
    finish();
    receiver_free(&r);
}

typedef struct removal {
    LND_SUBSCRIPTION *subscription;
    bool library;
    lnd_event entered, done;
    int32_t result;
} removal;

static void remove_subscription(void *user) {
    removal *r = user;
    lnd_event_signal(&r->entered);
    if (r->library)
        LND_LibraryFree();
    else
        r->result = LND_SubscriptionFree(r->subscription);
    lnd_event_signal(&r->done);
}

static void cancellation(bool library) {
    begin();
    LND_SOUND *sound;
    source_create(&sound);
    receiver r;
    receiver_init(&r, 0, LND_NOTIFY_END, 4);
    r.block = true;
    LND_SUBSCRIPTION_CONFIG c = config(&r, 0);
    r.subscription = LND_SoundSubscribe(sound, &c);
    REQUIRE(r.subscription != nullptr);
    CHECK(render(sound) == 4);
    REQUIRE(lnd_event_wait(&r.ready, 2000));
    removal rem = {.subscription = r.subscription, .library = library};
    REQUIRE(lnd_event_init(&rem.entered) == LND_OK && lnd_event_init(&rem.done) == LND_OK);
    lnd_thread thread;
    REQUIRE(lnd_thread_create(&thread, remove_subscription, &rem) == LND_OK);
    CHECK(lnd_event_wait(&rem.entered, 2000));
    CHECK(!lnd_event_wait(&rem.done, 30));
    lnd_event_signal(&r.gate);
    CHECK(lnd_event_wait(&rem.done, 2000));
    if (library)
        CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .realloc = resize, .free = release}) == LND_OK);
    lnd_thread_join(&thread);
    CHECK(rem.result == LND_OK);
    lnd_event_free(&rem.entered);
    lnd_event_free(&rem.done);
    finish();
    CHECK(lnd_load(&r.count) == 1);
    receiver_free(&r);
}

typedef struct producer {
    LND_SOUND *sound;
    lnd_atomic_u32 done, failed;
    unsigned count;
} producer;

static void produce(void *user) {
    producer *p = user;
    thread_kind = 2;
    for (unsigned i = 0; i < p->count; i++)
        if (render(p->sound) != 4)
            lnd_store(&p->failed, 1);
    lnd_store(&p->done, 1);
}

static void stress(void) {
    begin();
    LND_SOUND *sound;
    source_create(&sound);
    REQUIRE(LND_SoundSetLoop(sound, true) == LND_OK);
    receiver r;
    receiver_init(&r, 0, LND_NOTIFY_POSITION, 2);
    LND_SUBSCRIPTION_CONFIG c = config(&r, 0);
    r.subscription = LND_SoundSubscribe(sound, &c);
    REQUIRE(r.subscription != nullptr);
    LND_SOUND *second;
    source_create(&second);
    REQUIRE(LND_SoundSetLoop(second, true) == LND_OK);
    receiver other;
    receiver_init(&other, 0, LND_NOTIFY_POSITION, 2);
    c = config(&other, 0);
    other.subscription = LND_SoundSubscribe(second, &c);
    REQUIRE(other.subscription != nullptr);
    producer p[2] = {{.sound = sound, .count = 20000}, {.sound = second, .count = 20000}};
    receiver *receivers[2] = {&r, &other};
    lnd_thread threads[2];
    for (unsigned k = 0; k < 2; k++)
        REQUIRE(lnd_thread_create(&threads[k], produce, &p[k]) == LND_OK);
    while (!lnd_load(&p[0].done) || !lnd_load(&p[1].done)) {
        CHECK(LND_LibraryUpdate() == LND_OK);
        lnd_sleep_ms(1);
    }
    for (unsigned k = 0; k < 2; k++) {
        lnd_thread_join(&threads[k]);
        receiver *current = receivers[k];
        uint64_t dropped = LND_SubscriptionGetDroppedCount(current->subscription);
        uint64_t deadline = lnd_time_ns() + 2000000000;
        while (lnd_load(&current->count) + dropped < p[k].count && lnd_time_ns() < deadline)
            lnd_event_wait(&current->ready, 10);
        CHECK(!lnd_load(&p[k].failed));
        CHECK(lnd_load(&current->count) + dropped == p[k].count);
        printf("%u automatic, %llu dropped, expected %u\n", lnd_load(&current->count), (unsigned long long)dropped, p[k].count);
        CHECK(LND_SubscriptionFree(current->subscription) == LND_OK);
    }
    finish();
    receiver_free(&r);
    receiver_free(&other);
}

static void wakeups(void) {
    begin();
    LND_SOUND *sound;
    source_create(&sound);
    REQUIRE(LND_SoundSetLoop(sound, true) == LND_OK);
    receiver r;
    receiver_init(&r, 0, LND_NOTIFY_POSITION, 2);
    LND_SUBSCRIPTION_CONFIG c = config(&r, 0);
    r.subscription = LND_SoundSubscribe(sound, &c);
    REQUIRE(r.subscription != nullptr);
    for (unsigned i = 0; i < 256; i++) {
        CHECK(render(sound) == 4);
        REQUIRE(lnd_event_wait(&r.ready, 2000));
        CHECK(lnd_load(&r.count) == i + 1);
    }
    finish();
    receiver_free(&r);
}

static void lifecycle(void) {
    for (unsigned i = 0; i < 32; i++) {
        begin();
        LND_SOUND *sound;
        source_create(&sound);
        receiver r;
        receiver_init(&r, 0, LND_NOTIFY_END, 4);
        LND_SUBSCRIPTION_CONFIG c = config(&r, 0);
        r.subscription = LND_SoundSubscribe(sound, &c);
        REQUIRE(r.subscription != nullptr);
        if (i & 1)
            CHECK(render(sound) == 4);
        finish();
        CHECK(lnd_load(&r.count) <= 1);
        receiver_free(&r);
    }
}

#if LND_MODULE_GRAPH
static void process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t rate) {
    (void)user;
    (void)pcm;
    (void)frames;
    (void)channels;
    (void)rate;
}

static void graph(void) {
    begin();
    receiver r;
    receiver_init(&r, 0, LND_NOTIFY_POSITION, 2);
    LND_NODE *node = LND_NodeCreateProcessorProc(process, nullptr, 1, 8000, 0);
    LND_SUBSCRIPTION_CONFIG c = config(&r, LND_NOTIFY_ONCE);
    r.subscription = LND_NodeSubscribe(node, &c);
    LND_RENDERER *renderer = LND_RendererCreateNode(node);
    REQUIRE(node && r.subscription && renderer);
    CHECK(LND_SubscriptionIsAutomatic(r.subscription));
    int16_t data[4];
    LND_PCM pcm = {.data = data, .frames = 4, .channels = 1, .format = LND_FORMAT_S16};
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 4) == LND_OK);
    CHECK(lnd_event_wait(&r.ready, 2000));
    CHECK(LND_SubscriptionFree(r.subscription) == LND_OK);
    CHECK(lnd_load(&r.count) == 1);
    finish();
    receiver_free(&r);
}
#endif

int main(void) {
    thread_kind = 1;
    allocation_failure();
    delivery();
    cancellation(false);
    cancellation(true);
    stress();
    wakeups();
    lifecycle();
#if LND_MODULE_GRAPH
    graph();
#endif
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
