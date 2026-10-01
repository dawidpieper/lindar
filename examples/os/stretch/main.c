#include "lindar_stretch.h"
#include "lindar.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t read_sine(void *user, void *dst, uint64_t frames) {
    uint32_t *position = user;
    if (*position == 48000)
        return LND_READ_EOF;
    if (frames > 48000 - *position)
        frames = 48000 - *position;
    float *out = dst;
    for (uint64_t i = 0; i < frames; i++)
        out[i] = (float)(0.25 * sin(6.283185307179586 * 440 * (*position)++ / 48000));
    return (int64_t)frames;
}

static bool parse(const char *arg, float *value) {
    char *end;
    *value = strtof(arg, &end);
    return end != arg && !*end && isfinite(*value);
}

int main(int argc, char **argv) {
    const LND_STRETCH_BACKEND *backend = nullptr;
    if (argc > 1 && strcmp(argv[1], "auto")) {
        backend = LND_StretchBackendFind(argv[1]);
        if (!backend) return 2;
    }
    float semitones = 7, tempo = 0.75f, rate = 1;
    if (argc > 5 || (argc > 2 && !parse(argv[2], &semitones)) || (argc > 3 && !parse(argv[3], &tempo)) || (argc > 4 && !parse(argv[4], &rate)) ||
        semitones < -24 || semitones > 24 || tempo < 0.25f || tempo > 4 || rate < 0.25f || rate > 4) {
        fprintf(stderr, "stretch [auto|bungee|soundtouch] [pitch semitones -24..24] [tempo 0.25..4] [rate 0.25..4]\n");
        return 2;
    }
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) != LND_OK || LND_LibraryInit() != LND_OK)
        return 1;
    uint32_t position = 0;
    LND_SOURCE_PROCS procs = {.read = read_sine, .length_frames = 48000};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &position, LND_FORMAT_F32, 1, 48000, LND_GRAPH_SOURCE_DIRECT);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_STRETCH_CONFIG config = {.pitch_ratio = exp2f(semitones / 12), .tempo_ratio = tempo, .rate_ratio = rate, .backend = backend};
    LND_NODE *node = LND_NodeCreateStretch(1, 48000, &config);
    LND_RENDERER *renderer = nullptr;
    int result = 1;
    if (!source || !sound || !node || LND_SoundSetOutput(sound, node) != LND_OK || !(renderer = LND_RendererCreateNode(node)) || LND_SoundPlay(sound) != LND_OK)
        goto done;
    float samples[256];
    LND_PCM output = {.data = samples, .frames = 256, .channels = 1, .format = LND_FORMAT_F32};
    uint64_t total = 0;
    for (unsigned i = 0; i < 10000; i++) {
        int64_t got = LND_RendererReadPcm(renderer, &output, 0, 256);
        if (got < 0)
            goto done;
        total += (uint64_t)got;
        if (LND_NodeGetStatus(node) == LND_SOURCE_EOF)
            break;
    }
    LND_STRETCH_INFO info;
    if (LND_NodeGetStretchInfo(node, &info) != LND_OK || info.error || LND_NodeGetStatus(node) != LND_SOURCE_EOF)
        goto done;
    printf("%s: pitch=%+.2f semitones tempo=%.3f rate=%.3f frames=%llu duration=%.3f s\n", LND_StretchBackendGetName(info.backend),
           semitones, tempo, rate, (unsigned long long)total, (double)total / 48000);
    result = 0;
done:
    if (result)
        fprintf(stderr, "Stretch failed: %s\n", LND_ErrorGetString(LND_ErrorGetLast()));
    LND_LibraryFree();
    return result;
}
