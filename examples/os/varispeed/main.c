#include "lindar_dsp.h"
#include "lindar.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct sine {
    uint64_t position;
    uint32_t sample_rate_hz;
} sine;

static int64_t read_sine(void *user, void *dst, uint64_t frames) {
    sine *s = user;
    if (s->position == s->sample_rate_hz) return LND_READ_EOF;
    if (frames > s->sample_rate_hz - s->position) frames = s->sample_rate_hz - s->position;
    float *pcm = dst;
    for (uint64_t f = 0; f < frames; f++)
        pcm[f] = (float)(0.25 * sin(6.283185307179586 * 440 * s->position++ / s->sample_rate_hz));
    return (int64_t)frames;
}

int main(int argc, char **argv) {
    char *end = nullptr;
    float rate_ratio = argc > 1 ? strtof(argv[1], &end) : 0.5f;
    if (argc > 2 || (end && *end) || !isfinite(rate_ratio) || rate_ratio < 0.25f || rate_ratio > 4.0f) {
        fprintf(stderr, "Usage: varispeed [0.25..4.0]\n");
        return 1;
    }
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) != LND_OK || LND_LibraryInit() != LND_OK)
        return 1;
    sine input = {.sample_rate_hz = 48000};
    LND_SOURCE_PROCS procs = {.read = read_sine, .length_frames = input.sample_rate_hz};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &input, LND_FORMAT_F32, 1, input.sample_rate_hz, LND_GRAPH_SOURCE_DIRECT);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_NODE *node = LND_NodeCreateVarispeed(1, input.sample_rate_hz, 0, LND_RESAMPLE_SINC32);
    LND_RENDERER *renderer = nullptr;
    int result = 1;
    if (!source || !sound || !node || LND_SoundSetOutput(sound, node) != LND_OK || LND_NodeSetParam(node, LND_DSP_PARAM_RATE_RATIO, rate_ratio) != LND_OK ||
        !(renderer = LND_RendererCreateNode(node)) || LND_SoundPlay(sound) != LND_OK)
        goto done;
    float samples[256];
    LND_PCM output = {.data = samples, .frames = 256, .channels = 1, .format = LND_FORMAT_F32};
    uint64_t total = 0;
    int64_t got;
    while ((got = LND_RendererReadPcm(renderer, &output, 0, output.frames)) > 0)
        total += (uint64_t)got;
    if (got < 0 || LND_NodeGetStatus(node) != LND_SOURCE_EOF) goto done;
    printf("sample_rate=%u Hz, rate_ratio=%.3f, frames=%llu, duration=%.3f s, tone=%.3f Hz\n", LND_NodeGetVarispeedBaseSampleRateHz(node),
           LND_NodeGetParam(node, LND_DSP_PARAM_RATE_RATIO), (unsigned long long)total, (double)total / LND_RendererGetSampleRateHz(renderer),
           440.0 * rate_ratio);
    result = 0;
done:
    if (renderer) LND_RendererFree(renderer);
    LND_LibraryFree();
    return result;
}
