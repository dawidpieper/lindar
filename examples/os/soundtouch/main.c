#include "lindar_soundtouch.h"
#include "lindar.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct sine {
    uint32_t position;
} sine;

static int64_t read_sine(void *user, void *dst, uint64_t frames) {
    sine *s = user;
    if (s->position == 48000) return LND_READ_EOF;
    if (frames > 48000 - s->position) frames = 48000 - s->position;
    float *out = dst;
    for (uint64_t f = 0; f < frames; f++)
        out[f] = (float)(0.25 * sin(6.283185307179586 * 440 * s->position++ / 48000));
    return (int64_t)frames;
}

static bool parse(const char *arg, float *value) {
    char *end;
    *value = strtof(arg, &end);
    return end != arg && !*end && isfinite(*value);
}

int main(int argc, char **argv) {
    float semitones = 7, tempo = 0.75f, rate_ratio = 1;
    if (argc > 4 || (argc > 1 && !parse(argv[1], &semitones)) || (argc > 2 && !parse(argv[2], &tempo)) || (argc > 3 && !parse(argv[3], &rate_ratio)) ||
        semitones < -24 || semitones > 24 || tempo < 0.25f || tempo > 4 || rate_ratio < 0.25f || rate_ratio > 4) {
        fprintf(stderr, "Usage: soundtouch [pitch semitones -24..24] [tempo 0.25..4] [rate 0.25..4]\n");
        return 1;
    }
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) != LND_OK || LND_LibraryInit() != LND_OK)
        return 1;
    sine input = {0};
    LND_SOURCE_PROCS procs = {.read = read_sine, .length_frames = 48000};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &input, LND_FORMAT_F32, 1, 48000, LND_GRAPH_SOURCE_DIRECT);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_SOUNDTOUCH_CONFIG config = {.pitch_ratio = exp2f(semitones / 12), .tempo_ratio = tempo, .rate_ratio = rate_ratio};
    LND_NODE *node = LND_NodeCreateSoundTouch(1, 48000, &config);
    LND_RENDERER *renderer = nullptr;
    int result = 1;
    if (!source || !sound || !node || LND_SoundSetOutput(sound, node) != LND_OK || !(renderer = LND_RendererCreateNode(node)) || LND_SoundPlay(sound) != LND_OK)
        goto done;
    float samples[256];
    LND_PCM output = {.data = samples, .frames = 256, .channels = 1, .format = LND_FORMAT_F32};
    uint64_t total = 0;
    int64_t got;
    while ((got = LND_RendererReadPcm(renderer, &output, 0, output.frames)) > 0)
        total += (uint64_t)got;
    if (got < 0 || LND_NodeGetStatus(node) != LND_SOURCE_EOF) goto done;
    LND_SOUNDTOUCH_INFO info;
    if (LND_NodeGetSoundTouchInfo(node, &info) != LND_OK) goto done;
    printf("SoundTouch %s: pitch=%+.2f semitones, tempo=%.3f, rate=%.3f, frames=%llu, duration=%.3f s, initial latency=%u input frames\n",
           LND_SoundTouchGetVersion(), semitones, tempo, rate_ratio, (unsigned long long)total, (double)total / 48000, info.initial_latency_frames);
    result = 0;
done:
    if (renderer) LND_RendererFree(renderer);
    LND_LibraryFree();
    return result;
}
