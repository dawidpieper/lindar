#include "lindar_vst3.h"
#include "lindar.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: vst3_host plugin.vst3\n"); return 2; }
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_VST3_CLASS plugin;
    LND_VST3_OPTIONS options = {.path = argv[1], .channels = 2, .sample_rate_hz = 48000, .block_frames = 128, .offline = true};
    int result = 1;
    if (LND_Vst3ModuleScanClass(argv[1], 0, &plugin) != LND_OK) goto done;
    memcpy(options.class_id, plugin.id, sizeof plugin.id);
    LND_NODE *effect = LND_NodeCreateVst3(&options);
    LND_VST3_INFO info;
    if (!effect || LND_NodeGetVst3Info(effect, &info) != LND_OK) goto done;
    printf("%s: %u parameters, latency=%u frames\n", info.plugin.name, info.parameter_count, info.latency_frames);
    for (uint32_t i = 0; i < info.parameter_count; i++) {
        LND_VST3_PARAMETER parameter;
        if (LND_NodeGetVst3Parameter(effect, i, &parameter) != LND_OK) goto done;
        printf("%u: %s (%s)\n", parameter.id, parameter.name, parameter.units);
    }
    float input[256] = {0.25f, 0.25f}, output[256];
    LND_PCM pcm = {.data = input, .frames = 128, .channels = 2, .format = LND_FORMAT_F32};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .channels = 2, .sample_rate_hz = 48000});
    if (!source || LND_NodeConnect(LND_SourceEnsureNode(source), effect) != LND_OK || LND_SoundPlay(LND_SourceGetSound(source)) != LND_OK) goto done;
    LND_RENDERER *renderer = LND_RendererCreateNode(effect);
    pcm.data = output;
    if (renderer && LND_RendererFillPcm(renderer, &pcm, 0, 128) == LND_OK) {
        printf("processed 128 stereo frames\n");
        result = 0;
    }
    LND_RendererFree(renderer);
done:
    if (result) fprintf(stderr, "%s\n", LND_ErrorGetString(LND_ErrorGetLast()));
    LND_LibraryFree();
    return result;
}
