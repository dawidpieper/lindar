#include "lindar.h"
#include <stdio.h>

static int64_t read_pcm(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    size_t *remaining = user;
    if (!*remaining) return LND_READ_EOF;
    if (frames > *remaining) frames = *remaining;
    int32_t result = LND_PcmSilence(pcm, offset, frames);
    if (result != LND_OK) return result;
    *remaining -= frames;
    return (int64_t)frames;
}

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16LE);
    if (LND_LibraryInit() != LND_OK) return 1;
    size_t remaining = 150;
    LND_SOURCE_CONFIG config = {.read = read_pcm, .user = &remaining, .channels = 1, .sample_rate_hz = 8000};
    size_t bytes = LND_SourceGetMemoryBytes(&config);
    max_align_t memory[(bytes + sizeof(max_align_t) - 1) / sizeof(max_align_t)];
    LND_SOURCE *source = LND_SourceInit(memory, sizeof memory, &config);
    uint8_t samples[64 * 2];
    LND_PCM output = {.data = samples, .frames = 64, .channels = 1, .format = LND_FORMAT_S16LE};
    int64_t total = 0, got = -1;
    if (source) while ((got = LND_SourceReadPcm(source, &output, 0, 64)) > 0) total += got;
    printf("%lld frames, status=%d\n", (long long)total, LND_SourceGetStatus(source));
    int result = got == 0 && total == 150 && LND_SourceGetStatus(source) == LND_SOURCE_EOF ? 0 : 1;
    LND_SourceFree(source);
    LND_LibraryFree();
    return result;
}
