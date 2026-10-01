#include "sine_mcu.h"

#include <stdio.h>

int main(void) {
    // Reuse the completed DMA buffer only after the hardware releases it.
    if (sine_mcu_configure() != LND_OK) return 1;
    size_t bytes = sine_mcu_memory_size();
    max_align_t memory[(bytes + sizeof(max_align_t) - 1) / sizeof(max_align_t)];
    sine_mcu sine = {0};
    int32_t r = sine_mcu_init(&sine, memory, sizeof memory, 48000, 440);
    if (r != LND_OK) return 1;
    int16_t buffers[2][128];
    int16_t previous = 0;
    uint32_t crossings = 0;
    uint32_t checksum = 2166136261;
    for (uint32_t block = 0; block < 750; block++) {
        int16_t *buffer = buffers[block & 1];
        r = sine_mcu_fill(&sine, buffer, 128);
        if (r != LND_OK) break;
        for (uint32_t frame = 0; frame < 128; frame++) {
            int16_t sample = buffer[frame];
            if (previous <= 0 && sample > 0) crossings++;
            checksum = (checksum ^ (uint16_t)sample) * 16777619;
            previous = sample;
        }
    }
    if (r == LND_OK) printf("sine_mcu: 440 Hz, 48000 Hz PCM, 96000 frames, %u cycles, checksum %08x, workspace %zu bytes\n", crossings, checksum, bytes);
    sine_mcu_free(&sine);
    LND_LibraryFree();
    return r == LND_OK && crossings == 880 ? 0 : 1;
}
