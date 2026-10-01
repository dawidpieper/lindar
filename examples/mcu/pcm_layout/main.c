#include "lindar.h"
#include <stdio.h>

int main(void) {
    uint8_t stereo[] = {10, 110, 20, 120, 30, 130};
    uint8_t left[6] = {0}, right[6] = {0};
    void *planes[] = {left, right};
    LND_PCM src = {.data = stereo, .frames = 3, .channels = 2, .format = LND_FORMAT_U8};
    LND_PCM dst = {.planes = planes, .frames = 3, .stride_bytes = 2, .channels = 2, .format = LND_FORMAT_U8, .layout = LND_LAYOUT_PLANAR};
    if (LND_PcmConvert(&dst, 0, &src, 0, 3) != LND_OK) return 1;
    for (size_t i = 0; i < 3; i++) printf("L=%u R=%u\n", left[i * 2], right[i * 2]);
    return 0;
}
