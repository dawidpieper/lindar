#include "file_mcu.h"
#include "sample.h"

#include <stdio.h>

int main(void) {
    // Lindar uses the arena; the Opus dependencies still need their libc allocator.
    static max_align_t memory[8192 / sizeof(max_align_t)];
    file_mcu player = {0};
    int32_t r = file_mcu_configure(&player, memory, sizeof memory);
    if (r == LND_OK) r = file_mcu_init(&player, opus_mcu_file, sizeof opus_mcu_file);
    int16_t buffers[2][128];
    uint32_t checksum = 2166136261;
    size_t allocations = player.allocations;
    uint64_t length = LND_SourceGetLengthFrames(player.source);
    for (uint64_t block = 0; r == LND_OK && block < (length + 127) / 128; block++) {
        int16_t *buffer = buffers[block & 1];
        r = file_mcu_fill(&player, buffer, 128);
        if (r != LND_OK) break;
        for (size_t f = 0; f < 128; f++) checksum = (checksum ^ (uint16_t)buffer[f]) * 16777619;
    }
    bool complete = r == LND_OK && LND_SourceGetPositionFrames(player.source) == length && player.allocations == allocations && length;
    printf("opus_mcu: %llu frames, %u Hz, checksum %08x, Lindar arena %zu bytes, playback allocations %zu\n", (unsigned long long)length,
           LND_SourceGetSampleRateHz(player.source), checksum, player.used, player.allocations - allocations);
    file_mcu_free(&player);
    LND_LibraryFree();
    return complete ? 0 : 1;
}
