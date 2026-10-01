#pragma once

#include "lindar.h"
#include "lnd_modules.h"
#if LND_MODULE_GRAPH
#include "lindar_graph.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, allocations, frees;
static bool deny_alloc;
static int fail_after = -1;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

static void *allocate(void *user, size_t bytes) {
    (void)user;
    if (deny_alloc) {
        CHECK(false);
        return nullptr;
    }
    if (fail_after == 0) return nullptr;
    if (fail_after > 0) fail_after--;
    void *p = malloc(bytes);
    if (p) allocations++;
    return p;
}
static void release(void *user, void *memory) {
    (void)user;
    if (memory) frees++;
    free(memory);
}
static void begin(int32_t format, int32_t layout) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, format) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, layout) == LND_OK);
#if LND_MODULE_GRAPH
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) == LND_OK);
#endif
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .free = release}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
}
static void finish(void) {
    deny_alloc = false;
    LND_LibraryFree();
    CHECK(allocations == frees);
}
static int report(void) {
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
