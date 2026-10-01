#include "lindar.h"
#include "lnd_modules.h"
#include <stdio.h>

#if LND_MODULE_DSP
#include "lindar_dsp.h"
static int test_dsp(void) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_LibraryInit() != LND_OK) return 1;
    const LND_DELAY_CONFIG config = {100, 10, 0.5f, 0.5f};
    LND_NODE *node = LND_NodeCreateDelay(1, 48000, &config);
    if (!node) return 2;
    if (LND_NodeGetChannels(node) != 1) return 3;
    LND_NodeFree(node);
    LND_LibraryFree();
    return 0;
}
#endif

#if LND_MODULE_ASIO
#include "lindar_asio.h"

static int test_asio(void) {
    LND_ASIO_CONFIG config = {0};
    LND_ASIO_INFO info = {};
    LND_ASIO_TIME time = {0};
    if (LND_DeviceGetAsioConfig(nullptr, &config) != LND_ERR_INVALID_ARG)
        return 1;
    if (LND_DeviceGetAsioInfo(nullptr, &info) != LND_ERR_INVALID_ARG)
        return 2;
    if (LND_DeviceGetAsioTime(nullptr, &time) != LND_ERR_INVALID_ARG)
        return 3;
    if (LND_DeviceCheckAsioSampleRateHz(nullptr, 48000) != LND_ERR_INVALID_ARG)
        return 4;
    if (LND_DeviceResetAsio(nullptr) != LND_ERR_INVALID_ARG)
        return 5;
    return 0;
}
#endif

#if LND_MODULE_EFFECTS
#include "lindar_effects.h"

static int test_effects(void) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_LibraryInit() != LND_OK) return 1;
    LND_NODE *(*const create[])(uint32_t, uint32_t, const LND_EFFECT_CONFIG *) = {
        LND_NodeCreateEcho,          LND_NodeCreateReverb,       LND_NodeCreateChorus,      LND_NodeCreateFlanger,
        LND_NodeCreatePhaser,        LND_NodeCreateDistortion,   LND_NodeCreateCompressor,  LND_NodeCreateAutoWah,
        LND_NodeCreatePeakEqualizer, LND_NodeCreateBiquadEffect, LND_NodeCreateDynamicGain, LND_NodeCreateRotation};
    for (int32_t i = 0; i < LND_EFFECT_COUNT; i++) {
        LND_NODE *node = create[i](2, 48000, 0);
        LND_EFFECT_INFO info;
        if (!node || LND_NodeGetEffectInfo(node, &info) != LND_OK || info.type != i || info.channel_mask != 3 || LND_NodeSetEffectChannelMask(node, 1) != LND_OK ||
            LND_NodeSetParam(node, LND_EFFECT_PARAM_BYPASS, 1) != LND_OK || LND_NodeGetParam(node, LND_EFFECT_PARAM_BYPASS) != 1 ||
            LND_NodeResetEffect(node) != LND_OK || LND_NodeFree(node) != LND_OK)
            return 2;
    }
    LND_EFFECT_PARAM parameter = {LND_EFFECT_PARAM_SWEEP_MS_PER_SECOND, 50};
    LND_EFFECT_CONFIG config = {0};
    config.params = &parameter;
    config.param_count = 1;
    config.flags = LND_EFFECT_NO_TAIL;
    LND_NODE *node = LND_NodeCreateEffect(0, 0, LND_EFFECT_FLANGER, &config);
    if (!node || LND_NodeGetParam(node, parameter.id) != 50 || LND_NodeFree(node) != LND_OK) return 3;
    LND_LibraryFree();
    return 0;
}
#endif

#if LND_MODULE_BUNGEE && LND_MODULE_SOUNDTOUCH
#include "lindar_bungee.h"
#include "lindar_soundtouch.h"

static int test_stretch(void) {
    if (!LND_StretchBackendFind("bungee") ||
        !LND_StretchBackendFind("soundtouch") ||
        LND_StretchBackendSetPriority(LND_StretchBackendFind("soundtouch"), 300) != LND_OK ||
        LND_StretchBackendGetPriority(LND_StretchBackendFind("soundtouch")) != 300 ||
        LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK ||
        LND_LibraryInit() != LND_OK)
        return 1;
    LND_NODE *pitch = LND_NodeCreatePitch(1, 48000, 2);
    LND_NODE *tempo = LND_NodeCreateTempo(1, 48000, 0.5f);
    LND_STRETCH_CONFIG config = {0};
    config.backend = LND_StretchBackendFind("bungee");
    LND_NODE *stretch = LND_NodeCreateStretch(1, 48000, &config);
    LND_NODE *bungee = LND_NodeCreateBungee(1, 48000, 0);
    LND_NODE *soundtouch = LND_NodeCreateSoundTouch(1, 48000, 0);
    if (!pitch || !tempo || !stretch || !bungee || !soundtouch ||
        LND_NodeGetStretchBackend(pitch) != LND_StretchBackendFind("soundtouch") ||
        LND_NodeGetStretchBackend(tempo) != LND_StretchBackendFind("soundtouch") ||
        LND_NodeGetStretchBackend(stretch) != LND_StretchBackendFind("bungee") ||
        LND_NodeGetStretchBackend(bungee) != LND_StretchBackendFind("bungee") ||
        LND_NodeGetStretchBackend(soundtouch) != LND_StretchBackendFind("soundtouch"))
        return 2;
    LND_BUNGEE_INFO info;
    LND_STRETCH_INFO common;
    if (LND_NodeGetBungeeInfo(bungee, &info) != LND_OK || !info.grain_frames ||
        !LND_BungeeGetVersion()[0] ||
        LND_NodeGetStretchInfo(stretch, &common) != LND_OK ||
        common.backend != LND_StretchBackendFind("bungee") ||
        LND_NodeSetStretchPitchSemitones(stretch, 12) != LND_OK ||
        LND_NodeGetStretchPitchSemitones(stretch) != 12 ||
        LND_NodeSetStretchTempoChangePercent(stretch, -50) != LND_OK ||
        LND_NodeSetStretchRateChangePercent(stretch, 100) != LND_OK ||
        LND_NodeEndBungeeInput(bungee) != LND_OK || LND_NodeResetBungee(bungee) != LND_OK ||
        LND_NodeEndStretchInput(stretch) != LND_OK ||
        LND_NodeResetStretch(stretch) != LND_OK)
        return 3;
    LND_LibraryFree();
    return 0;
}
#endif

#if LND_MODULE_GRAPH && LND_MODULE_OUTPUT
#include "lindar_graph.h"
#include "lindar_output.h"

static int32_t process(void *user, const LND_PCM *pcm, size_t offset, uint32_t frames, uint32_t rate) {
    (void)user;
    (void)rate;
    if (pcm->format != LND_FORMAT_F32 || pcm->layout != LND_LAYOUT_PLANAR) return LND_ERR_FORMAT;
    float *data = (float *)pcm->planes[0];
    for (size_t i = offset; i < offset + frames; i++)
        data[i] = 0.25f;
    return LND_OK;
}

static int32_t open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state) {
    (void)params;
    (void)extension;
    *state = io;
    return LND_OK;
}
static int32_t write(void *state, const float *pcm, uint64_t frames) {
    (void)state;
    return frames == 1 && pcm[0] == 0.25f ? LND_OK : LND_ERR_FORMAT;
}
static int32_t close(void *state) {
    (void)state;
    return LND_OK;
}
static size_t discard(void *user, const void *data, size_t bytes) {
    (void)user;
    (void)data;
    return bytes;
}

static int test_pcm(void) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_LibraryInit() != LND_OK) return 1;
    LND_PROCESSOR_PROCS procs = {0};
    procs.process_pcm = process;
    procs.process_format = LND_FORMAT_F32;
    procs.process_layouts = LND_LAYOUT_MASK_PLANAR;
    LND_NODE *node = LND_NodeCreateProcessor(&procs, 0, 1, 48000);
    LND_RENDERER *renderer = LND_RendererCreateNode(node);
    float sample = 0;
    void *planes[] = {&sample};
    LND_PCM pcm = {0};
    pcm.planes = planes;
    pcm.frames = 1;
    pcm.channels = 1;
    pcm.format = LND_FORMAT_F32;
    pcm.layout = LND_LAYOUT_PLANAR;
    if (!node || !renderer || LND_RendererReadPcm(renderer, &pcm, 0, 1) != 1 || sample != 0.25f) return 2;
    LND_ENCODER encoder = {0};
    encoder.name = "public-pcm";
    encoder.open = open;
    encoder.write = write;
    encoder.close = close;
    if (LND_EncoderRegister(&encoder) != LND_OK) return 3;
    LND_IO_OUTPUT_PROCS callbacks = {0};
    callbacks.write = discard;
    LND_ENCODER_PARAMS params = {0};
    params.encoder_name = encoder.name;
    params.channels = 1;
    params.sample_rate_hz = 48000;
    LND_OUTPUT *output = LND_OutputCreateProc(&callbacks, 0, &params);
    if (!output || LND_OutputWritePcm(output, &pcm, 0, 1) != LND_OK || LND_OutputFree(output) != LND_OK) return 4;
    LND_LibraryFree();
    return 0;
}
#endif

#if LND_MODULE_NOTIFY && LND_MODULE_DEVICES && LND_MODULE_GRAPH
#include "lindar_notify.h"
#include "lindar_devices.h"
#include "lindar_graph.h"

static void notify_process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t rate) {
    (void)user;
    (void)pcm;
    (void)frames;
    (void)channels;
    (void)rate;
}

static int test_notify(void) {
    if (LND_DeviceGetType(LND_DEVICE_DEFAULT_INPUT) != LND_DEVICE_INPUT ||
        LND_DeviceGetType(LND_DEVICE_DEFAULT_OUTPUT) != LND_DEVICE_OUTPUT) return 1;
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK ||
        LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) != LND_OK || LND_LibraryInit() != LND_OK) return 2;
    int16_t data[4] = {0};
    LND_PCM pcm = {0};
    pcm.data = data;
    pcm.frames = 4;
    pcm.channels = 1;
    pcm.format = LND_FORMAT_S16;
    LND_SOURCE_CONFIG config = {0};
    config.pcm = &pcm;
    config.channels = 1;
    config.sample_rate_hz = 8000;
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_SUBSCRIPTION_CONFIG watch = {0};
    watch.type = LND_NOTIFY_END;
    watch.flags = LND_NOTIFY_MANUAL;
    LND_SUBSCRIPTION *subscription = LND_SoundSubscribe(sound, &watch);
    LND_NOTIFICATION event = {0};
    if (!subscription || LND_SubscriptionIsAutomatic(subscription) || LND_SoundPlay(sound) != LND_OK || LND_SoundRenderPcm(sound, &pcm, 0, 4) != 4 ||
        LND_SubscriptionRead(subscription, &event) != 1 || event.type != LND_NOTIFY_END) return 3;
    LND_NODE *node = LND_NodeCreateProcessorProc(notify_process, 0, 1, 8000, 0);
    if (!node || LND_SubscriptionFree(subscription) != LND_OK) return 4;
    LND_LibraryFree();
    return 0;
}
#endif

int main(void) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_LibraryInit() != LND_OK) return 1;
    LND_LibraryFree();
#if LND_MODULE_DSP
    if (test_dsp()) {
        fprintf(stderr, "Public API: dsp failed\n");
        return 1;
    }
#endif
#if LND_MODULE_ASIO
    if (test_asio()) {
        fprintf(stderr, "Public API: asio failed\n");
        return 1;
    }
#endif
#if LND_MODULE_EFFECTS
    if (test_effects()) {
        fprintf(stderr, "Public API: effects failed\n");
        return 1;
    }
#endif
#if LND_MODULE_BUNGEE && LND_MODULE_SOUNDTOUCH
    if (test_stretch()) {
        fprintf(stderr, "Public API: stretch failed\n");
        return 1;
    }
#endif
#if LND_MODULE_GRAPH && LND_MODULE_OUTPUT
    if (test_pcm()) {
        fprintf(stderr, "Public API: pcm failed\n");
        return 1;
    }
#endif
#if LND_MODULE_NOTIFY && LND_MODULE_DEVICES && LND_MODULE_GRAPH
    if (test_notify()) {
        fprintf(stderr, "Public API: notify failed\n");
        return 1;
    }
#endif
    return 0;
}
