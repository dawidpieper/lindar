#include "lindar_slide.h"
#include "playback/graph/node.h"
#include "src/thread.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct state {
    lnd_event allocating, resume;
    lnd_atomic_u32 block;
    LND_NODE *node;
    int32_t result;
} state;
static void *allocate(void *user, size_t bytes) {
    state *s = user;
    uint32_t expected = 1;
    if (lnd_cas(&s->block, &expected, 0)) {
        lnd_event_signal(&s->allocating);
        if (!lnd_event_wait(&s->resume, 5000)) return nullptr;
    }
    return malloc(bytes);
}
static void release(void *user, void *memory) { free(memory); }
static void worker(void *user) {
    state *s = user;
    lnd_store(&s->block, 1);
    LND_SLIDE_CONFIG config = {.duration_frames = 48000, .curve = LND_SLIDE_LOGARITHMIC};
    s->result = LND_NodeSlideParam(s->node, LND_PARAM_GAIN, 0.5f, &config);
}
#define REQUIRE(x)                                                                                                                                             \
    do {                                                                                                                                                       \
        if (!(x)) {                                                                                                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                                                                         \
            return 1;                                                                                                                                          \
        }                                                                                                                                                      \
    } while (0)
int main(void) {
    state s = {0};
    REQUIRE(lnd_event_init(&s.allocating) == LND_OK && lnd_event_init(&s.resume) == LND_OK);
    REQUIRE(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_MANUAL) == LND_OK);
    REQUIRE(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .free = release, .user = &s}) == LND_OK);
    REQUIRE(LND_LibraryInit() == LND_OK);
    s.node = LND_NodeCreateBus(1, 48000);
    REQUIRE(s.node);
    lnd_thread thread = {0};
    REQUIRE(lnd_thread_create(&thread, worker, &s) == LND_OK);
    REQUIRE(lnd_event_wait(&s.allocating, 2000));
    uint64_t start = lnd_time_ns();
    bool acquired = lnd_spinlock_try(&s.node->lock);
    uint64_t elapsed = lnd_time_ns() - start;
    if (acquired) lnd_spinlock_unlock(&s.node->lock);
    lnd_event_signal(&s.resume);
    lnd_thread_join(&thread);
    REQUIRE(acquired && s.result == LND_OK);
    REQUIRE(LND_NodeFree(s.node) == LND_OK);
    LND_LibraryFree();
    lnd_event_free(&s.allocating);
    lnd_event_free(&s.resume);
    printf("Render lock available while allocation paused: %llu ns\n", (unsigned long long)elapsed);
    return 0;
}
