#include "lindar_files.h"
#include "lindar.h"
#include <math.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: render_file output.wav\n"); return 2; }
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (LND_LibraryInit() != LND_OK) return 1;
    int16_t samples[8000];
    for (size_t i = 0; i < 8000; i++) samples[i] = (int16_t)(8192 * sin(6.283185307179586 * 440 * i / 8000));
    LND_PCM pcm = {.data = samples, .frames = 8000, .channels = 1, .format = LND_FORMAT_S16LE};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .channels = 1, .sample_rate_hz = 8000});
    int64_t frames = source ? LND_SourceRenderFile(source, argv[1], LND_FORMAT_S16LE, 0) : -1;
    printf("%lld frames -> %s\n", (long long)frames, argv[1]);
    LND_SourceFree(source);
    LND_LibraryFree();
    return frames == 8000 ? 0 : 1;
}
