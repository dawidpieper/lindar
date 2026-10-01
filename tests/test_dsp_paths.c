#include "stream_test.h"
#include "lindar_dsp.h"
#if LND_MODULE_SLIDE
#include "lindar_slide.h"
#endif
#include "playback/graph/node.h"
#include "pcm/audio/simd.h"
#if LND_MODULE_EFFECTS
#include "processing/effects/effects.h"
#endif

static LND_NODE *create(unsigned kind, unsigned channels) {
    switch (kind) {
    case 0: return LND_NodeCreateBiquad(channels, 48000, &(LND_BIQUAD_CONFIG){.frequency_hz = 1300, .q = 0.707f});
    case 1: return LND_NodeCreateDelay(channels, 48000, &(LND_DELAY_CONFIG){.max_delay_ms = 20, .delay_ms = 3, .feedback = 0.4f, .mix = 0.6f});
    case 2: return LND_NodeCreatePanner(48000, 0.3f, LND_PAN_STEREO);
    default: return LND_NodeCreateMeter(channels, 48000, 20);
    }
}

static void fill(float *pcm, size_t count, unsigned seed) {
    for (size_t i = 0; i < count; i++) {
        seed = seed * 1664525u + 1013904223u;
        pcm[i] = ((int32_t)(seed >> 8) - 8388608) / 8388608.0f;
    }
}

static void constructors(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    const int32_t params[] = {LND_DSP_PARAM_FREQUENCY_HZ, LND_DSP_PARAM_DELAY_MS, LND_DSP_PARAM_PAN, LND_DSP_PARAM_DECAY_MS};
    const float values[] = {1300, 3, 0.3f, 20};
    for (unsigned kind = 0; kind < 4; kind++) {
        LND_NODE *n = create(kind, 2);
        CHECK(n && !n->ring.items);
        CHECK(LND_NodeGetParam(n, params[kind]) == values[kind]);
        CHECK(LND_NodeSetParam(n, params[kind], values[kind]) == LND_OK);
        CHECK(n->ring.items);
        CHECK(LND_NodeFree(n) == LND_OK);
        bool success = false;
        for (int fault = 0; fault < 16 && !success; fault++) {
            unsigned live = allocations - frees;
            fail_after = fault;
            n = create(kind, 2);
            fail_after = -1;
            success = n != nullptr;
            if (n) CHECK(LND_NodeFree(n) == LND_OK);
            CHECK(allocations - frees == live);
        }
        CHECK(success);
    }
    finish();
}

static void panner_paths(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    const float pans[] = {-1, -0.5f, -0.0f, 0, 0.25f, 1};
    const uint32_t blocks[] = {0, 1, 7, 16, 17, 255, 256, 1024};
    const uint32_t rates[] = {8000, 48000, 192000};
    float a[2050], b[2050];
    for (unsigned p = 0; p < 6; p++)
        for (int32_t mode = LND_PAN_STEREO; mode <= LND_PAN_MONO; mode++)
            for (unsigned r = 0; r < 3; r++) {
                LND_NODE *x = LND_NodeCreatePanner(rates[r], pans[p], mode);
                LND_NODE *y = LND_NodeCreatePanner(rates[r], pans[p], mode);
                CHECK(x && y);
                if (!x || !y) { finish(); return; }
                const LND_PROCESSOR_PROCS *procs = LND_NodeGetProcessorProcs(x);
                void *sx = LND_NodeGetProcessorUser(x), *sy = LND_NodeGetProcessorUser(y);
                procs->param(sy, LND_DSP_PARAM_PAN, pans[p]);
                for (unsigned run = 0; run < 24; run++) {
                    if (run == 8 || run == 16) {
                        float pan = run == 8 ? 0.7f : -0.3f;
                        procs->param(sx, LND_DSP_PARAM_PAN, pan);
                        procs->param(sy, LND_DSP_PARAM_PAN, pan);
                        procs->param(sx, LND_DSP_PARAM_PAN_MODE, (float)(1 - mode));
                        procs->param(sy, LND_DSP_PARAM_PAN_MODE, (float)(1 - mode));
                    }
                    fill(a, 2050, run + 17);
                    memcpy(b, a, sizeof a);
                    procs->process(sx, a + 1, blocks[run % 8], 2, rates[r]);
                    procs->process(sy, b + 1, blocks[run % 8], 2, rates[r]);
                    CHECK(memcmp(a, b, sizeof a) == 0);
                }
                CHECK(LND_NodeFree(x) == LND_OK && LND_NodeFree(y) == LND_OK);
            }
    finish();
}

static void kernels(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    lnd_simd_ops vector = lnd_simd;
    static float a[1024 * 32], b[1024 * 32];
    const uint32_t counts[] = {1, 2, 3, 4, 6, 8, 16, 32};
    const uint32_t blocks[] = {0, 1, 15, 16, 17, 63, 127, 128, 129, 1024};
    const uint32_t rates[] = {8000, 48000, 192000};
    for (unsigned c = 0; c < sizeof counts / sizeof *counts; c++)
        for (unsigned type = 0; type < 8; type++)
            for (unsigned r = 0; r < 3; r++) {
                LND_BIQUAD_CONFIG cfg = {.type = type, .frequency_hz = r == 2 ? 10 : 1300, .q = r == 2 ? 30 : 0.707f, .gain_db = 9};
                LND_NODE *x = LND_NodeCreateBiquad(counts[c], rates[r], &cfg);
                LND_NODE *y = LND_NodeCreateBiquad(counts[c], rates[r], &cfg);
                CHECK(x && y);
                const LND_PROCESSOR_PROCS *procs = LND_NodeGetProcessorProcs(x);
                for (unsigned run = 0; run < 40; run++) {
                    uint32_t frames = blocks[run % 10];
                    size_t samples = (size_t)frames * counts[c];
                    fill(a, samples, run + 91);
                    if (run >= 30) memset(a, 0, samples * sizeof(float));
                    memcpy(b, a, samples * sizeof(float));
                    lnd_simd.biquad_f32 = nullptr;
                    procs->process(LND_NodeGetProcessorUser(x), a, frames, counts[c], rates[r]);
                    lnd_simd = vector;
                    procs->process(LND_NodeGetProcessorUser(y), b, frames, counts[c], rates[r]);
                    CHECK(memcmp(a, b, samples * sizeof(float)) == 0);
                }
                CHECK(LND_NodeFree(x) == LND_OK && LND_NodeFree(y) == LND_OK);
            }
    finish();
}

#if LND_MODULE_EFFECTS
static void effect_kernels(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    lnd_simd_ops vector = lnd_simd;
    static float a[1024 * 32], b[1024 * 32];
    for (unsigned channels = 1; channels <= 32; channels++)
        for (unsigned type = 0; type < 9; type++)
            for (unsigned mask = 0; mask < 3; mask++) {
                uint32_t all = UINT32_MAX >> (32 - channels);
                LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_FILTER_TYPE, (float)type}, {LND_EFFECT_PARAM_GAIN_DB, 9}};
                LND_EFFECT_CONFIG cfg = {.params = params, .param_count = 2, .flags = LND_EFFECT_SELECT_CHANNELS,
                                        .channel_mask = all & (mask == 0 ? all : mask == 1 ? 0x55555555u : 0xeeeeeeeeu)};
                LND_NODE *x = LND_NodeCreateBiquadEffect(channels, 48000, &cfg);
                LND_NODE *y = LND_NodeCreateBiquadEffect(channels, 48000, &cfg);
                CHECK(x && y);
                lnd_effect *sx = lnd_processor_user(x), *sy = lnd_processor_user(y);
                for (unsigned run = 0; run < 12; run++) {
                    unsigned frames = (unsigned[]){1, 16, 127, 128, 129, 1024}[run % 6];
                    size_t samples = (size_t)frames * channels;
                    fill(a, samples, run + 791);
                    if (run >= 6) memset(a, 0, samples * sizeof(float));
                    memcpy(b, a, samples * sizeof(float));
                    lnd_simd.biquad_f64 = nullptr;
                    sx->ops->process(sx, a, frames);
                    lnd_simd = vector;
                    sy->ops->process(sy, b, frames);
                    CHECK(memcmp(a, b, samples * sizeof(float)) == 0);
                    CHECK(sx->ops->tail(sx) == sy->ops->tail(sy));
                }
                CHECK(LND_NodeFree(x) == LND_OK && LND_NodeFree(y) == LND_OK);
            }
    finish();
}
#endif

static unsigned planar_calls;
static void (*render_planar)(lnd_node *, const LND_PCM *, size_t, uint32_t);
static void count_planar(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    planar_calls++;
    render_planar(n, pcm, offset, frames);
}

static void planar(unsigned kind, unsigned channels, unsigned block, int32_t format, unsigned scenario) {
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_MIX_BLOCK_FRAMES, block) == LND_OK);
    begin(format, LND_LAYOUT_PLANAR);
    if (scenario == 6) CHECK(LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 64) == LND_OK);
    float input[1024 * 8], a[2060 * 8], b[2060 * 8];
    fill(input, 1024 * channels, 913);
    LND_PCM in = {.data = input, .frames = 1024, .channels = channels, .format = LND_FORMAT_F32};
    LND_NODE *nodes[2];
    LND_RENDERER *renderers[2];
    lnd_node_vt scalar, fast;
    planar_calls = 0;
    for (unsigned i = 0; i < 2; i++) {
        nodes[i] = create(kind, channels);
        CHECK(nodes[i]);
        if (!i) {
            scalar = *nodes[i]->vt;
            scalar.render_planar = nullptr;
            nodes[i]->vt = &scalar;
        } else if (nodes[i]->vt->render_planar) {
            fast = *nodes[i]->vt;
            render_planar = fast.render_planar;
            fast.render_planar = count_planar;
            nodes[i]->vt = &fast;
        }
        for (unsigned j = 0; j < (scenario == 2 ? 2 : 1); j++) {
            LND_SOURCE *src = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &in, .channels = channels, .sample_rate_hz = 48000});
            LND_SOUND *sound = LND_SourceEnsureSound(src, nullptr);
            CHECK(src && sound);
            CHECK(LND_SoundSetOutput(sound, nodes[i]) == LND_OK);
            CHECK(LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundPlay(sound) == LND_OK);
        }
        if (scenario == 3 || scenario == 6) {
            CHECK(LND_NodeSetGain(nodes[i], 1.4f) == LND_OK);
            CHECK(LND_NodeSetClipMode(nodes[i], LND_CLIP_SOFT) == LND_OK);
        }
#if LND_MODULE_SLIDE
        if (scenario == 7) CHECK(LND_NodeSlideParam(nodes[i], LND_PARAM_GAIN, 0.4f, &(LND_SLIDE_CONFIG){.duration_frames = 257}) == LND_OK);
#endif
        if (scenario == 8) CHECK(LND_RendererCreateNode(nodes[i]));
        LND_NODE *root = nodes[i];
        if (scenario == 1) {
            root = LND_NodeCreateBus(channels, 48000);
            CHECK(root && LND_NodeConnect(nodes[i], root) == LND_OK);
        }
        renderers[i] = LND_RendererCreateNode(root);
        CHECK(renderers[i]);
    }
    void *pa[8], *pb[8];
    for (unsigned c = 0; c < channels; c++) {
        pa[c] = (unsigned char *)(a + c * 2060) + (scenario == 5);
        pb[c] = (unsigned char *)(b + c * 2060) + (scenario == 5);
    }
    LND_PCM outa = {.planes = pa, .frames = 1030, .channels = channels, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_PLANAR};
    if (scenario == 4) outa.stride_bytes = 8;
    LND_PCM outb = outa;
    outb.planes = pb;
    for (unsigned run = 0; run < 12; run++) {
        size_t count = run % 3 ? block : block - 1;
        memset(a, 0x55, sizeof a);
        memset(b, 0x55, sizeof b);
        CHECK(LND_RendererReadPcm(renderers[0], &outa, 2, count) == LND_RendererReadPcm(renderers[1], &outb, 2, count));
        CHECK(memcmp(a, b, sizeof a) == 0);
        if (kind == 3)
            for (unsigned c = 0; c < channels; c++) {
                CHECK(LND_NodeGetMeterPeak(nodes[0], c) == LND_NodeGetMeterPeak(nodes[1], c));
                CHECK(LND_NodeGetMeterRms(nodes[0], c) == LND_NodeGetMeterRms(nodes[1], c));
            }
    }
    if ((kind == 1 || kind == 3) && channels >= 4 && format == LND_FORMAT_F32 && scenario == 0) CHECK(planar_calls > 0);
    if (scenario == 2 || (scenario >= 4 && scenario <= 5) || scenario == 8) CHECK(planar_calls == 0);
    finish();
}

int main(void) {
    constructors();
    panner_paths();
    kernels();
#if LND_MODULE_EFFECTS
    effect_kernels();
#endif
    for (unsigned kind = 0; kind < 4; kind++)
        for (unsigned ch = 2; ch <= (kind == 2 ? 2 : 8); ch *= 2)
            for (unsigned block = 16; block <= 1024; block *= 8)
                for (unsigned scenario = 0; scenario < 9; scenario++)
                    for (int32_t format = LND_FORMAT_S16; format <= LND_FORMAT_F64; format++) planar(kind, ch, block, format, scenario);
    return report();
}
