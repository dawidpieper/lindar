#include "lindar.h"
#include "lindar_metadata.h"
#include "src/thread.h"
#include "src/atomic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
static lnd_atomic_u32 live, errors, ready, go, done, copies;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        ++checks;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            ++failures;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

static void *allocate(void *user, size_t size) {
    (void)user;
    void *p = malloc(size);
    if (p) lnd_add(&live, 1);
    return p;
}
static void *reallocate(void *user, void *p, size_t size) {
    if (!p) return allocate(user, size);
    return realloc(p, size);
}
static void release(void *user, void *p) {
    (void)user;
    if (p) {
        lnd_sub(&live, 1);
        free(p);
    }
}
static void reader(void *user) {
    LND_SOURCE *source = user;
    LND_METADATA *m = LND_MetadataCreate(nullptr);
    if (!m) lnd_add(&errors, 1);
    lnd_add(&ready, 1);
    while (!lnd_load(&go)) lnd_sleep_ms(1);
    for (unsigned i = 0; m && (!lnd_load(&done) || i < 1000); ++i) {
        int32_t r = LND_SourceCopyMetadata(source, m);
        if (!r) {
            const char *title = LND_MetadataGetValue(m, "TITLE", 0);
            if (!title || (strcmp(title, "A") && strcmp(title, "B")) || LND_MetadataGetFieldCount(m) != 33) lnd_add(&errors, 1);
            lnd_add(&copies, 1);
        } else if (r != LND_ERR_INVALID_ARG && r != LND_ERR_BUSY)
            lnd_add(&errors, 1);
    }
    LND_MetadataFree(m);
}
int main(void) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_MANUAL) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .realloc = reallocate, .free = release}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    int16_t samples[2] = {1, 2};
    LND_PCM pcm = {.data = samples, .frames = 2, .channels = 1, .format = LND_FORMAT_S16};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .sample_rate_hz = 48000, .channels = 1};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_METADATA *a = LND_MetadataCreate(nullptr), *b = nullptr;
    CHECK(source && a);
    if (source && a) {
        char text[1025];
        memset(text, 'x', 1024);
        text[1024] = 0;
        for (unsigned i = 0; i < 32; ++i) {
            char key[24];
            snprintf(key, sizeof key, "FIELD%u", i);
            CHECK(LND_MetadataSetValue(a, key, text) == LND_OK);
        }
        CHECK(LND_MetadataSetValue(a, "TITLE", "A") == LND_OK);
        b = LND_MetadataClone(a);
        CHECK(b != nullptr);
        if (b) {
            CHECK(LND_MetadataSetValue(b, "TITLE", "B") == LND_OK);
            CHECK(LND_SourceSetMetadata(source, a) == LND_OK);
            lnd_thread threads[4] = {0};
            unsigned created = 0;
            for (unsigned i = 0; i < 4; ++i) {
                int32_t r = lnd_thread_create(threads + i, reader, source);
                CHECK(r == LND_OK);
                if (r) break;
                ++created;
            }
            while (lnd_load(&ready) < created) lnd_sleep_ms(1);
            lnd_store(&go, 1);
            uint64_t deadline = lnd_time_ns() + UINT64_C(2000000000);
            while (lnd_load(&copies) < created && lnd_time_ns() < deadline) lnd_sleep_ms(1);
            CHECK(lnd_load(&copies) >= created);
            for (unsigned i = 0; i < 500; ++i) {
                CHECK(LND_SourceSetMetadata(source, i & 1 ? a : b) == LND_OK);
                if (!(i & 31)) lnd_sleep_ms(1);
            }
            CHECK(LND_SourceFree(source) == LND_OK);
            source = nullptr;
            lnd_store(&done, 1);
            for (unsigned i = 0; i < created; ++i) lnd_thread_join(threads + i);
            CHECK(lnd_load(&copies) != 0);
        }
    }
    if (source) CHECK(LND_SourceFree(source) == LND_OK);
    LND_MetadataFree(a);
    LND_MetadataFree(b);
    LND_LibraryFree();
    CHECK(!lnd_load(&errors));
    CHECK(!lnd_load(&live));
    printf("%u checks, %u failures, %u concurrent copies\n", checks, failures, lnd_load(&copies));
    return failures ? 1 : 0;
}
