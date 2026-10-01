#include "lindar_dsp.h"
#include "playback/graph/context.h"
#include "playback/graph/node.h"
#include "playback/graph/native.h"
#include "playback/graph/gain.h"
#include "src/thread.h"
#include "../tests/delay_reference.h"
#include <stdio.h>

static double median(double *values) {
    for (unsigned i = 1; i < 5; i++)
        for (unsigned j = i; j && values[j] < values[j - 1]; j--) {
            double value = values[j];
            values[j] = values[j - 1];
            values[j - 1] = value;
        }
    return values[2];
}
static void gains(void) {
    static const unsigned blocks[] = {16, 256, 2048};
    static const unsigned counts[] = {1, 2, 6};
    uint8_t *memory = malloc(2048 * 6 * 4);
    for (unsigned format = LND_FORMAT_S16; format <= LND_FORMAT_F32; format += LND_FORMAT_F32 - LND_FORMAT_S16)
        for (unsigned layout = 0; layout < 2; layout++)
            for (unsigned c = 0; c < 3; c++)
                for (unsigned b = 0; b < 3; b++)
                    for (unsigned active = 0; active < 2; active++) {
                        uint32_t frames = blocks[b], channels = counts[c];
                        size_t bytes = LND_PcmGetSampleBytes(format);
                        void *planes[6];
                        for (unsigned channel = 0; channel < channels; channel++) planes[channel] = memory + channel * frames * bytes;
                        LND_PCM pcm = {
                            .data = memory, .planes = planes, .frames = frames, .channels = channels, .format = (int32_t)format, .layout = (int32_t)layout};
                        unsigned iterations = 1000000 / frames / channels;
                        if (!active) iterations *= format == LND_FORMAT_F32 ? 256 : 8;
                        double timings[2][5];
                        for (unsigned trial = 0; trial < 5; trial++)
                            for (unsigned order = 0; order < 2; order++) {
                                unsigned variant = order ^ (trial & 1);
                                memset(memory, 0, frames * channels * bytes);
                                uint64_t start = lnd_time_ns();
                                for (unsigned i = 0; i < iterations; i++) {
                                    float gain = 0.5f, step = 1.0f / frames;
                                    uint32_t remaining = active ? frames : 0;
                                    if (variant) lnd_graph_pcm_ramp(&pcm, 0, frames, &gain, step, 1.5f, &remaining);
                                    else if (!active) lnd_graph_pcm_gain(&pcm, 0, frames, gain);
                                    else
                                        for (unsigned frame = 0; frame < frames; frame++) {
                                            gain += step;
                                            if (!--remaining) gain = 1.5f;
                                            lnd_graph_pcm_gain(&pcm, frame, 1, gain);
                                        }
                                }
                                timings[variant][trial] = (double)(lnd_time_ns() - start) / iterations;
                            }
                        printf("gain,%s,%u,%u,%u,%u,%.2f,%.2f\n", format == LND_FORMAT_S16 ? "s16" : "f32", layout, channels, frames, active,
                               median(timings[0]), median(timings[1]));
                    }
    free(memory);
}
static void delays(void) {
    for (uint32_t channels = 1; channels <= 6; channels += channels == 1 ? 1 : 4) {
        LND_NODE *node = LND_NodeCreateDelay(channels, 48000, &(LND_DELAY_CONFIG){.max_delay_ms = 10, .delay_ms = 3.71f, .feedback = 0.7f, .mix = 0.5f});
        const LND_PROCESSOR_PROCS *procs = LND_NodeGetProcessorProcs(node);
        LND_PROCESS_PROC volatile process = procs->process;
        LND_PROCESS_PROC volatile baseline = delay_reference_process;
        void *user = LND_NodeGetProcessorUser(node);
        delay_reference reference = delay_reference_create(channels, 48000, 10, 3.71f, 0.7f, 0.5f);
        float pcm[256 * 6] = {0};
        double timings[2][5];
        for (unsigned trial = 0; trial < 5; trial++)
            for (unsigned order = 0; order < 2; order++) {
                unsigned variant = order ^ (trial & 1);
                uint64_t start = lnd_time_ns();
                for (unsigned i = 0; i < 40000; i++) {
                    if (variant) process(user, pcm, 256, channels, 48000);
                    else baseline(&reference, pcm, 256, channels, 48000);
                }
                timings[variant][trial] = (double)(lnd_time_ns() - start) / 40000;
            }
        printf("delay,f32,0,%u,256,1,%.2f,%.2f\n", channels, median(timings[0]), median(timings[1]));
        free(reference.ring);
        LND_NodeFree(node);
    }
}
static void maintenance(void) {
    static const unsigned sizes[] = {64, 512, 4096};
    for (unsigned size = 0; size < 3; size++) {
        unsigned count = sizes[size];
        LND_NODE **nodes = calloc(count, sizeof *nodes);
        for (unsigned i = 0; i < count; i++) nodes[i] = LND_NodeCreateBus(1, 48000);
        double timings[2][5];
        lnd_context_lock();
        for (unsigned trial = 0; trial < 5; trial++)
            for (unsigned variant = 0; variant < 2; variant++) {
                uint64_t start = lnd_time_ns();
                for (unsigned i = 0; i < 1000; i++) {
                    if (variant) lnd_nodes_gc();
                    else
                        for (lnd_node *node = lnd_graph_ctx.nodes; node; node = node->next) lnd_node_drain(node, false);
                }
                timings[variant][trial] = (double)(lnd_time_ns() - start) / 1000;
            }
        lnd_context_unlock();
        printf("maintenance,idle,0,%u,0,0,%.2f,%.2f\n", count, median(timings[0]), median(timings[1]));
        for (unsigned i = 0; i < count; i++) LND_NodeFree(nodes[i]);
        free(nodes);
    }
}
int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_MANUAL);
    if (LND_LibraryInit() != LND_OK) return 1;
    puts("operation,format,layout,channels_or_nodes,frames,active,before_ns,after_ns");
    gains();
    delays();
    maintenance();
    LND_LibraryFree();
    return 0;
}
