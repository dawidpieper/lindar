#include "lindar_devices.h"
#include "src/atomic.h"
#include "src/context.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks, failures;
static lnd_atomic_u32 attempts, live;
static uint32_t fail_at;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s (fail_at %u, live %u, attempts %u)\n", __FILE__, __LINE__, #x, fail_at, lnd_load(&live), lnd_load(&attempts));                   \
        }                                                                                                                                                      \
    } while (0)

static void *allocate(void *user, size_t bytes) {
    if (lnd_add(&attempts, 1) == fail_at) return nullptr;
    void *memory = malloc(bytes);
    if (memory) lnd_add(&live, 1);
    return memory;
}
static void *resize(void *user, void *memory, size_t bytes) {
    if (lnd_add(&attempts, 1) == fail_at) return nullptr;
    void *result = realloc(memory, bytes);
    if (result && !memory) lnd_add(&live, 1);
    return result;
}
static void release(void *user, void *memory) {
    if (memory) {
        lnd_sub(&live, 1);
        free(memory);
    }
}

int main(void) {
    unsigned rejected = 0, initialized = 0;
    for (fail_at = 0; fail_at < 72; fail_at++) {
        lnd_store(&attempts, 0);
        CHECK(LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("null")) == LND_OK);
        CHECK(LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 1) == LND_OK);
        CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .realloc = resize, .free = release}) == LND_OK);
        int32_t result = LND_LibraryInit();
        if (result != LND_OK) {
            rejected++;
            CHECK(!lnd_ctx.initialized && !lnd_ctx.closing);
            CHECK(LND_LibraryUpdate() == LND_ERR_STATE);
            CHECK(lnd_load(&live) == 0);
            lnd_store(&attempts, fail_at + 1);
            CHECK(LND_LibraryInit() == LND_OK);
        } else initialized++;
        LND_LibraryFree();
        CHECK(lnd_load(&live) == 0);
        CHECK(!lnd_ctx.initialized && !lnd_ctx.closing);
    }
    CHECK(rejected > 0 && initialized > 0);
    printf("%u checks, %u failures; %u rolled back, %u initialized\n", checks, failures, rejected, initialized);
    return failures ? 1 : 0;
}
