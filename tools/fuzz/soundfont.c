#include "lindar_midi.h"
#include <stdlib.h>

static void *allocate(void *user, size_t bytes) { return bytes <= 16 * 1024 * 1024 ? malloc(bytes) : nullptr; }
static void *resize(void *user, void *ptr, size_t bytes) { return bytes <= 16 * 1024 * 1024 ? realloc(ptr, bytes) : nullptr; }
static void release(void *user, void *ptr) { free(ptr); }

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    LND_AllocatorSetConfig(&(LND_ALLOCATOR_CONFIG){.alloc = allocate, .realloc = resize, .free = release});
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t bytes) {
    if (bytes > 1024 * 1024) return 0;
    LND_SOUNDFONT *font = LND_SoundfontCreateMemory(data, bytes);
    LND_SoundfontFree(font);
    return 0;
}
