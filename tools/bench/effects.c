#include "lindar_effects.h"
#include "lindar.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static float input_pcm[2048];
static uint32_t position;

static uint64_t now(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static int64_t input(void *user, void *dst, uint64_t frames) {
    (void)user;
    float *out = dst;
    uint64_t done = 0;
    while (done < frames) {
        uint64_t count = frames - done < 1024 - position ? frames - done : 1024 - position;
        memcpy(out + done * 2, input_pcm + position * 2, (size_t)count * 2 * sizeof(float));
        done += count;
        position = (position + (uint32_t)count) % 1024;
    }
    return (int64_t)frames;
}

static double median(double a, double b, double c) {
    if (a > b) {
        double t = a;
        a = b;
        b = t;
    }
    if (b > c) b = c;
    return a > b ? a : b;
}

int main(void) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) != LND_OK || LND_LibraryInit() != LND_OK) return 1;
    const char *names[] = {"echo",       "reverb",  "chorus",  "flanger", "phaser",       "distortion",
                           "compressor", "autowah", "peak_eq", "biquad",  "dynamic_gain", "rotation"};
    for (uint32_t f = 0; f < 1024; f++) {
        float x = ((float)(f % 64) / 64 - 0.5f) * 0.6f;
        input_pcm[f * 2] = x;
        input_pcm[f * 2 + 1] = x * 0.7f;
    }
    puts("effect,channels,rate,block_frames,bypass_ns_per_frame,active_ns_per_frame,checksum");
    for (int32_t type = 0; type < LND_EFFECT_COUNT; type++) {
        LND_SOURCE_PROCS procs = {.read = input};
        LND_SOURCE *source = LND_SourceCreateProc(&procs, nullptr, LND_FORMAT_F32, 2, 48000, LND_GRAPH_SOURCE_DIRECT);
        LND_SOUND *sound = source ? LND_SourceEnsureSound(source, nullptr) : nullptr;
        LND_NODE *node = LND_NodeCreateEffect(2, 48000, type, nullptr);
        if (!source || !sound || !node || LND_NodeConnect(LND_SourceEnsureNode(source), node) != LND_OK) return 1;
        LND_RENDERER *renderer = LND_RendererCreateNode(node);
        if (!renderer || LND_SoundPlay(sound) != LND_OK) return 1;
        float samples[2048];
        LND_PCM pcm = {.data = samples, .frames = 1024, .channels = 2, .format = LND_FORMAT_F32};
        for (uint32_t block = 128; block <= 1024; block *= 8) {
            double timings[2][3], checksum = 0;
            uint32_t loops = 4194304 / block;
            for (uint32_t trial = 0; trial < 3; trial++) {
                for (uint32_t order = 0; order < 2; order++) {
                    uint32_t active = order ^ (trial & 1);
                    if (LND_NodeSetParam(node, LND_EFFECT_PARAM_BYPASS, !active) != LND_OK || LND_NodeResetEffect(node) != LND_OK) return 1;
                    position = 0;
                    for (uint32_t i = 0; i < 16384 / block; i++)
                        if (LND_RendererReadPcm(renderer, &pcm, 0, block) != block) return 1;
                    uint64_t start = now();
                    for (uint32_t i = 0; i < loops; i++)
                        if (LND_RendererReadPcm(renderer, &pcm, 0, block) != block) return 1;
                    timings[active][trial] = (double)(now() - start) / ((uint64_t)loops * block);
                    for (uint32_t i = 0; i < block * 2; i++)
                        checksum += samples[i] * samples[i];
                }
            }
            printf("%s,2,48000,%u,%.3f,%.3f,%.9g\n", names[type], block, median(timings[0][0], timings[0][1], timings[0][2]),
                   median(timings[1][0], timings[1][1], timings[1][2]), checksum);
        }
        LND_RendererFree(renderer);
        LND_NodeFree(node);
        LND_SourceFree(source);
    }
    LND_LibraryFree();
    return 0;
}
