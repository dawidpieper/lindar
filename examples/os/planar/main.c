#include "lindar_graph.h"
#include "lindar.h"

#include <stdio.h>
#include <string.h>

static int32_t process(void *user, const LND_PCM *pcm, size_t offset, uint32_t frames, uint32_t rate) {
    (void)user;
    (void)rate;
    size_t stride = pcm->stride_bytes ? pcm->stride_bytes : sizeof(float);
    for (uint32_t c = 0; c < pcm->channels; c++) {
        unsigned char *data = pcm->planes[c];
        for (size_t f = offset; f < offset + frames; f++) {
            float value;
            memcpy(&value, data + f * stride, sizeof value);
            value *= 0.5f;
            memcpy(data + f * stride, &value, sizeof value);
        }
    }
    return LND_OK;
}

int main(void) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_F32) != LND_OK ||
        LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, LND_LAYOUT_PLANAR) != LND_OK || LND_LibraryInit() != LND_OK)
        return 1;
    float left[] = {0.25f, 0.5f, 0.25f, 0}, right[] = {-0.5f, -0.25f, 0, -0.25f};
    void *planes[] = {left, right};
    LND_PCM input = {.planes = planes, .frames = 4, .channels = 2, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_PLANAR};
    LND_SOURCE_CONFIG config = {.pcm = &input, .channels = 2, .sample_rate_hz = 48000, .block_frames = 128};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = source ? LND_SourceEnsureSound(source, nullptr) : nullptr;
    LND_PROCESSOR_PROCS procs = {
        .process_pcm = process, .process_format = LND_FORMAT_F32, .process_layouts = LND_LAYOUT_MASK_PLANAR, .flags = LND_PROCESSOR_BOUNDED};
    LND_NODE *node = LND_NodeCreateProcessor(&procs, nullptr, 2, 48000);
    LND_NODE *source_node = LND_SourceEnsureNode(source);
    LND_RENDERER *renderer = node ? LND_RendererCreateNode(node) : nullptr;
    int32_t result = LND_ERR_STATE;
    if (source_node && node && renderer && LND_NodeConnect(source_node, node) == LND_OK && LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundPlay(sound) == LND_OK) {
        int16_t a[130], b[130];
        void *output_planes[] = {a, b};
        LND_PCM output = {.planes = output_planes, .frames = 130, .channels = 2, .format = LND_FORMAT_S16LE, .layout = LND_LAYOUT_PLANAR};
        result = LND_RendererFillPcm(renderer, &output, 1, 128);
        if (result == LND_OK) printf("128 frames: left=%d, right=%d\n", a[1], b[1]);
    }
    LND_RendererFree(renderer);
    LND_NodeFree(node);
    LND_NodeFree(source_node);
    LND_SourceFree(source);
    LND_LibraryFree();
    return result == LND_OK ? 0 : 1;
}
