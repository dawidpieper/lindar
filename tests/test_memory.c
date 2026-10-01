#include "src/alloc.h"
#include "src/platform.h"
#include "lindar_buffers.h"

#include <stdio.h>

static unsigned checks, failures;

#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

typedef struct fixed_allocator {
    uint8_t data[16];
    unsigned allocations;
    unsigned frees;
} fixed_allocator;

static void *fixed_alloc(void *user, size_t bytes) {
    fixed_allocator *state = user;
    state->allocations++;
    return bytes <= sizeof state->data ? state->data : nullptr;
}

static void fixed_free(void *user, void *ptr) {
    fixed_allocator *state = user;
    CHECK(ptr == state->data);
    state->frees++;
}

int main(void) {
    CHECK(lnd_alloc_aligned(SIZE_MAX, 64) == nullptr);
    CHECK(lnd_alloc_aligned(SIZE_MAX - 63, 64) == nullptr);
    CHECK(lnd_alloc_aligned(16, 17) == nullptr);
    for (size_t alignment = sizeof(void *); alignment <= 4096; alignment *= 2) {
        void *p = lnd_alloc_aligned(27, alignment);
        CHECK(p && (uintptr_t)p % alignment == 0);
        lnd_free_aligned(p);
    }
    CHECK(lnd_next_pow2_size(SIZE_MAX) == 0);
    CHECK(lnd_next_pow2_size(SIZE_MAX / 2 + 1) == SIZE_MAX / 2 + 1);
    CHECK(LND_BufferCreate(LND_FORMAT_U8, 1, 8000, SIZE_MAX) == nullptr);
    CHECK(LND_BufferCreate(LND_FORMAT_S32, 32, 8000, UINT64_MAX) == nullptr);
    LND_BUFFER *b = LND_BufferCreate(LND_FORMAT_U8, 1, 8000, 16);
    CHECK(b != nullptr);
    LND_BufferFree(b);
    LND_LibraryFree();
    fixed_allocator state = {0};
    const LND_ALLOCATOR_CONFIG config = {.alloc = fixed_alloc, .free = fixed_free, .user = &state};
    CHECK(LND_AllocatorSetConfig(&config) == LND_OK);
    void *memory = lnd_realloc(nullptr, 8);
    CHECK(memory == state.data && state.allocations == 1);
    state.data[0] = 73;
    CHECK(lnd_realloc(memory, 16) == nullptr && state.data[0] == 73 && !state.frees);
    lnd_free(memory);
    CHECK(state.frees == 1);
    CHECK(LND_AllocatorSetConfig(nullptr) == LND_OK);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
