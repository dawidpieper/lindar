#include "lindar_graph.h"
#include "pcm_fixture.h"
#if LND_MODULE_EFFECTS
#include "lindar_effects.h"
#endif
#if LND_MODULE_DSP
#include "lindar_dsp.h"
#endif
#if LND_MODULE_SOUNDTOUCH
#include "lindar_soundtouch.h"
#endif
#if LND_MODULE_BUNGEE
#include "lindar_bungee.h"
#endif
#if LND_MODULE_SLIDE
#include "lindar_slide.h"
#endif

#include <math.h>

enum { INPUT_FRAMES = 2051, OUTPUT_FRAMES = 8192 };

typedef struct feed {
    const LND_PCM *pcm;
    size_t position, available;
    bool live;
} feed;

typedef struct processor {
    int32_t format, error;
    uint32_t layouts, calls;
} processor;

static int64_t read_feed(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    feed *f = user;
    size_t left = f->available - f->position;
    if (!left) return f->available == INPUT_FRAMES ? LND_READ_EOF : 0;
    size_t count = frames < left ? frames : left;
    int32_t result = LND_PcmConvert(pcm, offset, f->pcm, f->position + 1, count);
    if (result != LND_OK) return result;
    f->position += count;
    return (int64_t)count;
}

static int32_t process_pcm(void *user, const LND_PCM *pcm, size_t offset, uint32_t frames, uint32_t rate) {
    processor *p = user;
    CHECK(pcm->format == p->format && (p->layouts & (1u << pcm->layout)) && rate == 8000);
    p->calls++;
    if (p->error) return p->error;
    for (uint32_t c = 0; c < pcm->channels; c++) {
        size_t bytes = LND_PcmGetSampleBytes(pcm->format);
        size_t stride = pcm->stride_bytes ? pcm->stride_bytes : bytes * (pcm->layout ? 1 : pcm->channels);
        unsigned char *data = pcm->layout ? pcm->planes[c] : (unsigned char *)pcm->data + c * bytes;
        for (uint32_t f = 0; f < frames; f++) {
            unsigned char *at = data + (offset + f) * stride;
            if (pcm->format == LND_FORMAT_F32) {
                float sample;
                memcpy(&sample, at, sizeof sample);
                sample *= 0.5f;
                memcpy(at, &sample, sizeof sample);
            } else {
                int16_t sample;
                memcpy(&sample, at, sizeof sample);
                sample /= 2;
                memcpy(at, &sample, sizeof sample);
            }
        }
    }
    return LND_OK;
}

static void process_flat(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t rate) {
    (void)user;
    CHECK(channels == 2 && rate == 8000);
    for (size_t i = 0; i < (size_t)frames * channels; i++)
        pcm[i] *= 0.5f;
}

static LND_NODE *create_node(int type, processor *proc, uint32_t rate) {
    switch (type) {
    case 0:
        return LND_NodeCreateMixer(2, rate, LND_MIX_AVAILABLE | LND_MIX_END);
    case 12:
        return LND_NodeCreateMixer(2, rate * 13 / 10, LND_MIX_AVAILABLE | LND_MIX_END);
    case 1:
        return LND_NodeCreateProcessorProc(process_flat, nullptr, 2, rate, LND_PROCESSOR_BOUNDED);
    case 2: {
        LND_PROCESSOR_PROCS procs = {
            .flags = LND_PROCESSOR_BOUNDED, .process_pcm = process_pcm, .process_format = proc->format, .process_layouts = proc->layouts};
        return LND_NodeCreateProcessor(&procs, proc, 2, rate);
    }
#if LND_MODULE_DSP
    case 4:
        return LND_NodeCreateVarispeed(2, rate, 1.3f, LND_RESAMPLE_SINC32);
    case 5:
        return LND_NodeCreateBiquad(2, rate, &(LND_BIQUAD_CONFIG){.type = LND_BIQUAD_LOWPASS, .frequency_hz = 1100, .q = 0.707f, .gain_db = 0});
    case 6:
        return LND_NodeCreateDelay(2, rate, &(LND_DELAY_CONFIG){.max_delay_ms = 20, .delay_ms = 7, .feedback = 0.4f, .mix = 0.5f});
    case 7:
        return LND_NodeCreatePanner(rate, 0.3f, LND_PAN_STEREO);
    case 8:
        return LND_NodeCreateMeter(2, rate, 20);
    case 9:
        return LND_NodeCreateVarispeed(2, rate, 0.7f, 0);
    case 10:
        return LND_NodeCreateVarispeed(2, rate, 1.1f, LND_RESAMPLE_SINC8);
    case 11:
        return LND_NodeCreateVarispeed(2, rate, 0.9f, LND_RESAMPLE_SINC16);
#endif
#if LND_MODULE_EFFECTS
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
        return LND_NodeCreateEffect(2, rate, type - 16, &(LND_EFFECT_CONFIG){.tail_ms = 20});
#endif
#if LND_MODULE_SOUNDTOUCH
    case 32:
        return LND_NodeCreateSoundTouch(2, rate, nullptr);
#endif
#if LND_MODULE_BUNGEE
    case 33:
        return LND_NodeCreateBungee(2, rate, nullptr);
#endif
    default:
        return nullptr;
    }
}

static size_t run(int32_t format, int il, int in_layout, int out_layout, int type, float *result) {
    begin(format, il);
    pcm_fixture input, output;
    fixture_init(&input, 2, INPUT_FRAMES + 2, LND_FORMAT_F32, in_layout, 3);
    fixture_init(&output, 2, 133, LND_FORMAT_F32, out_layout, 5);
    float data[INPUT_FRAMES * 2];
    for (size_t f = 0; f < INPUT_FRAMES; f++) {
        data[f * 2] = (float)(0.3 * sin(f * 0.21));
        data[f * 2 + 1] = (float)(0.2 * cos(f * 0.13));
    }
    LND_PCM source_pcm = {.data = data, .frames = INPUT_FRAMES, .channels = 2, .format = LND_FORMAT_F32};
    CHECK(LND_PcmConvert(&input.pcm, 1, &source_pcm, 0, INPUT_FRAMES) == LND_OK);
    bool bounded = !(type >= 5 && type <= 8);
    bool live = out_layout && bounded;
    feed feed = {.pcm = &input.pcm, .available = live ? 0 : INPUT_FRAMES, .live = live};
    LND_SOURCE_CONFIG config = {
        .read = read_feed, .user = &feed, .channels = 2, .sample_rate_hz = 8000, .block_frames = 128, .flags = feed.live ? LND_SOURCE_LIVE : 0};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = source ? LND_SourceEnsureSound(source, nullptr) : nullptr;
    processor proc = {.format = LND_FORMAT_F32, .layouts = LND_LAYOUT_MASK_ALL};
    LND_NODE *node = create_node(type, &proc, 8000);
    CHECK(source && sound && node);
    if (!node) {
        fixture_free(&input);
        fixture_free(&output);
        finish();
        return 0;
    }
    LND_NODE *input_node = LND_SourceEnsureNode(source);
    CHECK(LND_NodeConnect(input_node, node) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(node);
    CHECK(renderer && LND_SoundPlay(sound) == LND_OK);
    size_t total = 0;
    LND_PCM destination = {.data = result, .frames = OUTPUT_FRAMES, .channels = 2, .format = LND_FORMAT_F32};
    deny_alloc = true;
    for (unsigned iteration = 0; iteration < 1000 && total < OUTPUT_FRAMES; iteration++) {
        fixture_clear(&output);
        uint32_t want = out_layout ? 127 : 97;
        if (feed.live && feed.available < INPUT_FRAMES) feed.available = feed.available + 37 > INPUT_FRAMES ? INPUT_FRAMES : feed.available + 37;
        if (!bounded && want > INPUT_FRAMES - total) want = (uint32_t)(INPUT_FRAMES - total);
        int64_t got = LND_RendererReadPcm(renderer, &output.pcm, 1, want);
        CHECK(got >= 0 && got <= want);
        if (got < 0) break;
        fixture_guard(&output, 1, want);
        CHECK(LND_PcmConvert(&destination, total, &output.pcm, 1, (size_t)got) == LND_OK);
        total += (size_t)got;
        if ((!bounded && total == INPUT_FRAMES) || LND_NodeGetStatus(node) == LND_SOURCE_EOF) break;
    }
    if (bounded) CHECK(LND_NodeGetStatus(node) == LND_SOURCE_EOF);
    if (type == 2) CHECK(proc.calls > 0);
    deny_alloc = false;
    fixture_guard(&input, 1, INPUT_FRAMES);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    CHECK(LND_NodeFree(node) == LND_OK);
    CHECK(LND_NodeFree(input_node) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    fixture_free(&input);
    fixture_free(&output);
    finish();
    return total;
}

static void matrix(void) {
    int types[] = {
        0,  1,  2,  12,
#if LND_MODULE_DSP
        4,  5,  6,  7,  8,  9,  10, 11,
#endif
#if LND_MODULE_EFFECTS
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
#endif
#if LND_MODULE_SOUNDTOUCH
        32,
#endif
#if LND_MODULE_BUNGEE
        33,
#endif
    };
    static float reference[OUTPUT_FRAMES * 2], actual[OUTPUT_FRAMES * 2];
    for (int format = 0; format < 2; format++)
        for (size_t t = 0; t < sizeof types / sizeof *types; t++) {
            int32_t native = format ? LND_FORMAT_S16 : LND_FORMAT_F32;
            size_t expected = run(native, 0, 0, 0, types[t], reference);
            for (int bits = 1; bits < 8; bits++) {
                size_t count = run(native, bits & 1, (bits >> 1) & 1, (bits >> 2) & 1, types[t], actual);
                CHECK(count == expected);
                float tolerance = format ? 0.0003f : 0.00003f;
                unsigned bad = 0;
                for (size_t i = 0; i < count * 2 && i < expected * 2; i++)
                    if (!isfinite(actual[i]) || fabsf(actual[i] - reference[i]) > tolerance) bad++;
                if (bad) printf("planar matrix: format %d, type %d, layouts %d, mismatch %u\n", native, types[t], bits, bad);
                CHECK(bad == 0);
            }
        }
}

static int32_t inspect_pcm(void *user, const LND_PCM *pcm, size_t offset, uint32_t frames, uint32_t rate) {
    processor *p = user;
    CHECK(pcm->format == p->format && (p->layouts & (1u << pcm->layout)) && rate == 8000);
    CHECK(offset <= pcm->frames && frames <= pcm->frames - offset);
    p->calls++;
    return p->error;
}

static void processor_contracts(void) {
    for (int layout = 0; layout < 2; layout++)
        for (int format = LND_FORMAT_U8; format <= LND_FORMAT_F64; format++) {
            begin(format, layout);
            for (int target = LND_FORMAT_U8; target <= LND_FORMAT_F64; target++)
                for (unsigned mask = 1; mask <= LND_LAYOUT_MASK_ALL; mask++) {
                    float input[] = {0.25f, -0.5f};
                    LND_PCM input_pcm = {.data = input, .frames = 1, .channels = 2, .format = LND_FORMAT_F32};
                    LND_SOURCE_CONFIG config = {.pcm = &input_pcm, .channels = 2, .sample_rate_hz = 8000, .block_frames = 128};
                    LND_SOURCE *source = LND_SourceCreate(&config);
                    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
                    LND_NODE *input_node = LND_SourceEnsureNode(source);
                    processor proc = {.format = target, .layouts = mask};
                    LND_PROCESSOR_PROCS procs = {.process_pcm = inspect_pcm, .process_format = target, .process_layouts = mask, .flags = LND_PROCESSOR_BOUNDED};
                    LND_NODE *node = LND_NodeCreateProcessor(&procs, &proc, 2, 8000);
                    CHECK(source && sound && input_node && node && LND_NodeConnect(input_node, node) == LND_OK);
                    LND_RENDERER *renderer = LND_RendererCreateNode(node);
                    CHECK(renderer && LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundPlay(sound) == LND_OK);
                    for (int external = 0; external < 2; external++) {
                        pcm_fixture output;
                        fixture_init(&output, 2, 65, format, external, 3);
                        deny_alloc = true;
                        LND_PCM invalid = output.pcm;
                        invalid.data = nullptr;
                        invalid.planes = nullptr;
                        unsigned before = proc.calls;
                        CHECK(LND_RendererReadPcm(renderer, &invalid, 1, 61) == LND_ERR_INVALID_ARG && proc.calls == before);
                        CHECK(LND_RendererReadPcm(renderer, &output.pcm, 1, 61) == 61);
                        float samples[122];
                        LND_PCM result = {.data = samples, .frames = 61, .channels = 2, .format = LND_FORMAT_F32};
                        CHECK(LND_PcmConvert(&result, 0, &output.pcm, 1, 61) == LND_OK);
                        for (size_t i = 0; i < 122; i++)
                            CHECK(samples[i] == input[i % 2]);
                        fixture_guard(&output, 1, 61);
                        deny_alloc = false;
                        fixture_free(&output);
                    }
                    proc.error = LND_ERR_FORMAT;
                    float samples[34];
                    for (size_t i = 0; i < 34; i++)
                        samples[i] = 0.5f;
                    void *planes[] = {samples, samples + 17};
                    LND_PCM output = {.planes = planes, .frames = 17, .channels = 2, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_PLANAR};
                    deny_alloc = true;
                    CHECK(LND_RendererReadPcm(renderer, &output, 0, 17) == LND_ERR_FORMAT);
                    CHECK(LND_NodeGetStatus(node) == LND_ERR_FORMAT);
                    for (size_t i = 0; i < 34; i++)
                        CHECK(samples[i] == 0);
                    deny_alloc = false;
                    CHECK(LND_RendererFree(renderer) == LND_OK);
                    CHECK(LND_NodeFree(node) == LND_OK);
                    CHECK(LND_NodeFree(input_node) == LND_OK);
                    CHECK(LND_SourceFree(source) == LND_OK);
                }
            finish();
        }
}

static size_t resample_channels(uint32_t channels, uint32_t rate, uint32_t quality, int layout, float *samples) {
    CHECK(LND_ConfigSet(LND_CFG_AUDIO_RESAMPLE_QUALITY, quality) == LND_OK);
    begin(LND_FORMAT_F32, layout);
    pcm_fixture input, output;
    fixture_init(&input, channels, 4099, LND_FORMAT_F32, layout, 1);
    fixture_init(&output, channels, 71, LND_FORMAT_F32, layout, 3);
    float frame[32];
    LND_PCM packed = {.data = frame, .frames = 1, .channels = channels, .format = LND_FORMAT_F32};
    for (size_t f = 0; f < 4097; f++) {
        for (uint32_t c = 0; c < channels; c++)
            frame[c] = (float)(0.25 * sin(f * (0.11 + c * 0.01)));
        CHECK(LND_PcmConvert(&input.pcm, f + 1, &packed, 0, 1) == LND_OK);
    }
    LND_PCM data = input.pcm;
    void *planes[32];
    if (layout) {
        for (uint32_t c = 0; c < channels; c++)
            planes[c] = (unsigned char *)data.planes[c] + data.stride_bytes;
        data.planes = planes;
    } else
        data.data = (unsigned char *)data.data + data.stride_bytes;
    data.frames = 4097;
    LND_SOURCE_CONFIG config = {.pcm = &data, .channels = channels, .sample_rate_hz = 48000, .block_frames = 128};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_NODE *node = LND_NodeCreateMixer(channels, rate, LND_MIX_AVAILABLE | LND_MIX_END);
    LND_NODE *input_node = LND_SourceEnsureNode(source);
    CHECK(node && input_node && sound && LND_NodeConnect(input_node, node) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(node);
    CHECK(renderer != nullptr);
    size_t total = 0;
    LND_PCM result = {.data = samples, .frames = 16384, .channels = channels, .format = LND_FORMAT_F32};
    deny_alloc = true;
    for (unsigned iteration = 0; iteration < 400 && total < result.frames - 67; iteration++) {
        size_t count = layout ? 37 : 67;
        fixture_clear(&output);
        int64_t got = LND_RendererReadPcm(renderer, &output.pcm, 1, count);
        CHECK(got >= 0 && got <= (int64_t)count);
        if (got < 0) break;
        CHECK(LND_PcmConvert(&result, total, &output.pcm, 1, (size_t)got) == LND_OK);
        total += (size_t)got;
        fixture_guard(&output, 1, count);
        if (LND_NodeGetStatus(node) == LND_SOURCE_EOF) break;
    }
    CHECK(LND_NodeGetStatus(node) == LND_SOURCE_EOF);
    deny_alloc = false;
    CHECK(LND_RendererFree(renderer) == LND_OK && LND_NodeFree(node) == LND_OK && LND_NodeFree(input_node) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    fixture_free(&input);
    fixture_free(&output);
    finish();
    return total;
}

static void resampling(void) {
    static float reference[16384 * 32], actual[16384 * 32];
    const uint32_t channels[] = {1, 3, 32}, rates[] = {750, 32000, 96000};
    for (unsigned quality = 0; quality < 4; quality++)
        for (unsigned c = 0; c < 3; c++)
            for (unsigned r = 0; r < 3; r++) {
                size_t expected = resample_channels(channels[c], rates[r], quality, 0, reference);
                size_t count = resample_channels(channels[c], rates[r], quality, 1, actual);
                CHECK(count == expected);
                unsigned bad = 0;
                for (size_t i = 0; i < count * channels[c] && i < expected * channels[c]; i++)
                    if (!isfinite(actual[i]) || fabsf(actual[i] - reference[i]) > 0.000003f) bad++;
                if (bad) printf("resample channels: %u, rate: %u, quality: %u, differences: %u\n", channels[c], rates[r], quality, bad);
                CHECK(!bad);
            }
}

static void contracts(void) {
    begin(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    LND_PROCESSOR_PROCS procs = {.process_pcm = process_pcm, .process_format = LND_FORMAT_F32, .process_layouts = LND_LAYOUT_MASK_PLANAR};
    processor proc = {.format = LND_FORMAT_F32, .layouts = LND_LAYOUT_MASK_PLANAR};
    unsigned live = allocations - frees;
    for (int failure = 0; failure < 32; failure++) {
        fail_after = failure;
        LND_NODE *node = LND_NodeCreateProcessor(&procs, &proc, 2, 8000);
        fail_after = -1;
        CHECK(node || LND_ErrorGetLast() == LND_ERR_OUT_OF_MEMORY);
        if (node) CHECK(LND_NodeFree(node) == LND_OK);
        CHECK(allocations - frees == live);
        if (node) break;
    }
    procs.process_layouts = 4;
    CHECK(!LND_NodeCreateProcessor(&procs, &proc, 2, 8000));
    procs.process_layouts = 0;
    procs.process = process_flat;
    CHECK(!LND_NodeCreateProcessor(&procs, &proc, 2, 8000));
    float data[8] = {0.25f, -0.25f};
    void *planes[] = {data, data};
    LND_PCM pcm = {.planes = planes, .frames = 4, .channels = 2, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_PLANAR};
    CHECK(LND_PcmSilence(&pcm, 0, 4) == LND_ERR_INVALID_ARG && data[0] == 0.25f);
    planes[1] = (unsigned char *)data + 2;
    CHECK(LND_PcmSilence(&pcm, 0, 4) == LND_ERR_INVALID_ARG);
    planes[1] = data + 1;
    pcm.stride_bytes = 2 * sizeof(float);
    CHECK(LND_PcmSilence(&pcm, 0, 4) == LND_OK);
    for (size_t i = 0; i < 8; i++)
        CHECK(data[i] == 0);
    pcm.frames = 0;
    pcm.planes = nullptr;
    CHECK(LND_PcmSilence(&pcm, 0, 0) == LND_OK);
    finish();
}

int main(void) {
    contracts();
    processor_contracts();
    matrix();
    resampling();
    return report();
}
