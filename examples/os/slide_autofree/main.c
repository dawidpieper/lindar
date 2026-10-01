#include "lindar_autofree.h"
#include "lindar_slide.h"
#include "lindar_pcm_float.h"
#include "lnd_modules.h"
#if LND_MODULE_DSP
#include "lindar_dsp.h"
#endif
#if LND_MODULE_FILES
#include "lindar_files.h"
#endif

#include <math.h>
#include <stdio.h>

static float samples[48000], output[256];

int main(int argc, char **argv) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) != LND_OK || LND_LibraryInit() != LND_OK)
        return 1;
    LND_SOURCE *source = nullptr;
#if LND_MODULE_FILES
    if (argc > 1) source = LND_SourceCreateFile(argv[1], LND_ENCODED_SOURCE_LIGHTWEIGHT | LND_ENCODED_SOURCE_DIRECT, nullptr);
#else
    (void)argc;
    (void)argv;
#endif
    if (!source && argc <= 1) {
        for (size_t i = 0; i < 48000; i++)
            samples[i] = 0.25f * sinf((float)i * 0.0575958653f);
        LND_PCM pcm = {.data = samples, .frames = 48000, .channels = 1, .format = LND_FORMAT_F32};
        LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 48000, .block_frames = 256};
        source = LND_SourceCreate(&config);
    }
    LND_NODE *bus = LND_NodeCreateBus(1, 48000), *input = bus;
    int result = 1;
#if LND_MODULE_DSP
    input = LND_NodeCreateBiquad(1, 48000, &(LND_BIQUAD_CONFIG){.type = LND_BIQUAD_LOWPASS, .frequency_hz = 300, .q = 0.7071f, .gain_db = 0});
    if (!input || LND_NodeConnect(input, bus) != LND_OK) goto done;
    LND_SLIDE_CONFIG sweep = {.duration_frames = 48000, .step_frames = 32, .curve = LND_SLIDE_LOGARITHMIC};
    if (LND_NodeSlideParam(input, LND_DSP_PARAM_FREQUENCY_HZ, 6000, &sweep) != LND_OK) goto done;
#endif
    LND_RENDERER *renderer = bus ? LND_RendererCreateNode(bus) : nullptr;
    LND_SLIDE_CONFIG fade = {.duration_frames = 24000};
    if (!source || !renderer || LND_NodeSetGain(bus, 0) != LND_OK || LND_NodeSlideParam(bus, LND_PARAM_GAIN, 1, &fade) != LND_OK) goto done;
    LND_AUTOFREE id = LND_AutofreeTakeSource(source, input);
    if (!id) goto done;
    source = nullptr;
    LND_PCM pcm = {.data = output, .frames = 256, .channels = 1, .format = LND_FORMAT_F32};
    uint64_t frames = 0;
    double energy = 0;
    while (LND_AutofreeIsValid(id)) {
        if (LND_RendererFillPcm(renderer, &pcm, 0, 256) != LND_OK || LND_LibraryUpdate() != LND_OK) goto done;
        for (size_t i = 0; i < 256; i++)
            energy += output[i] * output[i];
        frames += 256;
    }
    printf("frames=%llu energy=%.6f autofree=complete\n", (unsigned long long)frames, energy);
    result = 0;
done:
    LND_LibraryFree();
    return result;
}
