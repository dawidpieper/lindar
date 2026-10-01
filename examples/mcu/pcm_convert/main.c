#include "lindar.h"
#include <stdio.h>

int main(void) {
    uint8_t input[] = {0, 64, 128, 192, 255};
    uint8_t output[10];
    LND_PCM src = {.data = input, .frames = 5, .channels = 1, .format = LND_FORMAT_U8};
    LND_PCM dst = {.data = output, .frames = 5, .channels = 1, .format = LND_FORMAT_S16LE};
    if (LND_PcmConvert(&dst, 0, &src, 0, 5) != LND_OK) return 1;
    for (size_t i = 0; i < 5; i++) {
        int value = output[2 * i] | (output[2 * i + 1] << 8);
        printf("%u -> %d\n", input[i], value < 32768 ? value : value - 65536);
    }
    return 0;
}
