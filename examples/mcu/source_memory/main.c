#include "lindar.h"
#include <stdio.h>

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16LE);
    if (LND_LibraryInit() != LND_OK) return 1;
    int16_t samples[] = {0, 8192, 16384, 24576, 32767};
    LND_PCM input = {.data = samples, .frames = 5, .channels = 1, .format = LND_FORMAT_S16LE};
    LND_SOURCE_CONFIG config = {.pcm = &input, .sample_rate_hz = 8000, .channels = 1};
    size_t bytes = LND_SourceGetMemoryBytes(&config);
    max_align_t memory[(bytes + sizeof(max_align_t) - 1) / sizeof(max_align_t)];
    LND_SOURCE *source = LND_SourceInit(memory, sizeof memory, &config);
    int16_t data[3];
    LND_PCM output = {.data = data, .frames = 3, .channels = 1, .format = LND_FORMAT_S16LE};
    int64_t read = source ? LND_SourceReadPcm(source, &output, 0, 3) : -1;
    if (read == 3) printf("%d %d %d; position=%llu\n", data[0], data[1], data[2], (unsigned long long)LND_SourceGetPositionFrames(source));
    LND_SourceFree(source);
    LND_LibraryFree();
    return read == 3 ? 0 : 1;
}
