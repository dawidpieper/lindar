#pragma once

#include "lindar_codecs.h"
#include "lindar.h"
#include "lindar_output.h"
#include "lnd_modules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if LND_THREADS
static _Atomic unsigned checks, failures, allocations, live_allocations;
#else
static unsigned checks, failures, allocations, live_allocations;
#endif
static int fail_allocation = -1;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

static void *test_alloc(void *user, size_t bytes) {
    allocations++;
    if (fail_allocation == 0) return nullptr;
    if (fail_allocation > 0) fail_allocation--;
    void *p = malloc(bytes);
    if (p) live_allocations++;
    return p;
}
static void *test_realloc(void *user, void *memory, size_t bytes) {
    if (!memory) return test_alloc(user, bytes);
    allocations++;
    if (fail_allocation == 0) return nullptr;
    if (fail_allocation > 0) fail_allocation--;
    return realloc(memory, bytes);
}
static void test_free(void *user, void *memory) {
    if (memory) live_allocations--;
    free(memory);
}
static void test_init(int32_t layout) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, (uint64_t)layout) == LND_OK);
    CHECK(LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = test_alloc, .realloc = test_realloc, .free = test_free}) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
}
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    CHECK(f != nullptr);
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    rewind(f);
    uint8_t *data = bytes >= 0 ? malloc((size_t)bytes + 1) : nullptr;
    CHECK(data != nullptr);
    if (data) CHECK(fread(data, 1, (size_t)bytes, f) == (size_t)bytes);
    fclose(f);
    *size = bytes >= 0 ? (size_t)bytes : 0;
    return data;
}

typedef struct test_input {
    const uint8_t *data;
    size_t size, limit;
    unsigned closes, reads;
} test_input;
static size_t read_at(void *user, uint64_t offset, void *dst, size_t size) {
    test_input *s = user;
    s->reads++;
    if (offset >= s->size) return 0;
    size_t n = s->size - (size_t)offset;
    if (n > size) n = size;
    if (s->limit && n > s->limit) n = s->limit;
    memcpy(dst, s->data + offset, n);
    return n;
}
static void close_input(void *user) { ((test_input *)user)->closes++; }
static const LND_IO_INPUT_PROCS input_procs = {.read_at = read_at, .close = close_input};

#if LND_MODULE_OUTPUT
typedef struct test_output {
    uint8_t data[262144];
    size_t pos, size;
} test_output;
static size_t write_output(void *user, const void *data, size_t size) {
    test_output *s = user;
    if (size > sizeof s->data - s->pos) return 0;
    memcpy(s->data + s->pos, data, size);
    s->pos += size;
    if (s->pos > s->size) s->size = s->pos;
    return size;
}
static int32_t seek_output(void *user, uint64_t offset) {
    test_output *s = user;
    if (offset > s->size) return LND_ERR_IO;
    s->pos = (size_t)offset;
    return LND_OK;
}
static const LND_IO_OUTPUT_PROCS output_procs = {.write = write_output, .seek = seek_output};
#endif
