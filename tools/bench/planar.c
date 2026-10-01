#include "lindar_graph.h"
#include "lindar.h"

#include <stdio.h>
#include <time.h>

static float input[2048 * 8], output[1024 * 8];

static uint64_t now(void) {
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static void process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t rate) {
    (void)user;
    (void)rate;
    for (size_t i = 0; i < (size_t)frames * channels; i++)
        pcm[i] *= 0.875f;
}

static double median(double *t) {
    double x = t[0], y = t[1], z = t[2];
    if (x > y) {
        double swap = x;
        x = y;
        y = swap;
    }
    if (y > z) y = z;
    return x > y ? x : y;
}

static int run(uint32_t channels, uint32_t block, unsigned layouts, unsigned processors, bool resample) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_F32) != LND_OK ||
        LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, layouts & 1) != LND_OK || LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) != LND_OK ||
        LND_ConfigSet(LND_CFG_AUDIO_RESAMPLE_QUALITY, 3) != LND_OK || LND_LibraryInit() != LND_OK)
        return 1;
    void *planes[8], *destination[8];
    int in_layout = (layouts >> 1) & 1, out_layout = (layouts >> 2) & 1;
    for (uint32_t c = 0; c < channels; c++) {
        planes[c] = input + c * 2048;
        destination[c] = output + c * 1024;
        for (size_t f = 0; f < 2048; f++)
            input[in_layout ? c * 2048 + f : f * channels + c] = (float)((int)((f + c * 3) % 127) - 63) / 128;
    }
    LND_PCM pcm = {.data = input, .planes = planes, .frames = 2048, .channels = channels, .format = LND_FORMAT_F32, .layout = in_layout};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = channels, .sample_rate_hz = 48000, .block_frames = 1024};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = source ? LND_SourceEnsureSound(source, nullptr) : nullptr;
    LND_NODE *node = LND_SourceEnsureNode(source);
    if (!node || !sound || LND_SoundSetLoop(sound, true) != LND_OK || LND_SoundPlay(sound) != LND_OK) return 1;
    for (unsigned i = 0; i < processors; i++) {
        LND_NODE *next = LND_NodeCreateProcessorProc(process, nullptr, channels, 48000, LND_PROCESSOR_BOUNDED);
        if (!next || LND_NodeConnect(node, next) != LND_OK) return 1;
        node = next;
    }
    LND_NODE *mix = LND_NodeCreateMixer(channels, resample ? 44100 : 48000, LND_MIX_AVAILABLE);
    if (!mix || LND_NodeConnect(node, mix) != LND_OK) return 1;
    LND_RENDERER *renderer = LND_RendererCreateNode(mix);
    if (!renderer) return 1;
    pcm = (LND_PCM){.data = output, .planes = destination, .frames = 1024, .channels = channels, .format = LND_FORMAT_F32, .layout = out_layout};
    double timing[3], checksum = 0;
    uint32_t loops = 262144 / block;
    for (unsigned trial = 0; trial < 3; trial++) {
        for (uint32_t i = 0; i < 8192 / block; i++)
            if (LND_RendererReadPcm(renderer, &pcm, 0, block) != block) return 1;
        uint64_t start = now();
        for (uint32_t i = 0; i < loops; i++)
            if (LND_RendererReadPcm(renderer, &pcm, 0, block) != block) return 1;
        timing[trial] = (double)(now() - start) / ((uint64_t)loops * block);
        for (uint32_t c = 0; c < channels; c++)
            for (uint32_t f = 0; f < block; f++)
                checksum += output[out_layout ? c * 1024 + f : f * channels + c];
    }
    printf("%s,%u,%u,%u,%u,%u,%u,%.3f,%.9g\n",
           resample     ? "sinc32"
           : processors ? "processor"
                        : "mix",
           processors, channels, block, layouts & 1, in_layout, out_layout, median(timing), checksum);
    LND_LibraryFree();
    return 0;
}

int main(void) {
    puts("pipeline,processors,channels,block,internal_planar,input_planar,output_planar,ns_per_frame,checksum");
    for (uint32_t channels = 2; channels <= 8; channels *= 4)
        for (uint32_t block = 128; block <= 1024; block *= 8)
            for (unsigned kind = 0; kind < 4; kind++)
                for (unsigned layouts = 0; layouts < 8; layouts++)
                    if (run(channels, block, layouts, kind == 1 ? 1 : kind == 2 ? 4 : 0, kind == 3)) return 1;
    return 0;
}
