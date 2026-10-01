#include "sine_mcu.h"
#include "platform.h"

static alignas(max_align_t) uint8_t workspace[1024];
static int16_t buffers[2][64];

int main(void) {
    if (sine_mcu_configure() != LND_OK) qemu_exit(1);
    sine_mcu sine;
    if (sine_mcu_init(&sine, workspace, sizeof workspace, 8000, 440) != LND_OK) qemu_exit(2);
    uint32_t checksum = 2166136261u, cycles = 0;
    int16_t previous = 0;
    // A board driver would hand back these buffers after each DMA transfer.
    for (unsigned block = 0; block < 125; block++) {
        int16_t *pcm = buffers[block & 1];
        if (sine_mcu_fill(&sine, pcm, 64) != LND_OK) qemu_exit(3);
        for (unsigned i = 0; i < 64; i++) {
            cycles += previous <= 0 && pcm[i] > 0;
            previous = pcm[i];
            checksum = (checksum ^ (uint16_t)pcm[i]) * UINT32_C(16777619);
        }
    }
    qemu_value("LND_CASE sine", checksum);
    qemu_value("LND_CASE cycles", cycles);
    qemu_value("LND_MEMORY", (uint32_t)sine_mcu_memory_size());
    int32_t result = sine_mcu_free(&sine);
    LND_LibraryFree();
    qemu_exit(result == LND_OK && cycles == 440 ? 0 : 4);
}
