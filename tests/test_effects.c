#include "lindar_effects.h"
#include "stream_test.h"
#if LND_MODULE_SLIDE
#include "lindar_slide.h"
#endif

#if LND_MODULE_NOTIFY
#include "lindar_notify.h"
#endif

#include <limits.h>
#include <math.h>

typedef struct input {
    const float *data;
    uint64_t length, position, available;
    uint32_t channels, rate;
    bool live, ended;
    int32_t error;
} input;

typedef struct playback {
    LND_SOURCE *source;
    LND_SOUND *sound;
    LND_NODE *node;
    LND_RENDERER *renderer;
} playback;

static int64_t read_input(void *user, void *dst, uint64_t frames) {
    input *in = user;
    if (in->error) return in->error;
    uint64_t end = in->live ? in->available : in->length;
    uint64_t count = end > in->position ? end - in->position : 0;
    if (count > frames) count = frames;
    if (!count) return in->live && !in->ended ? 0 : LND_READ_EOF;
    memcpy(dst, in->data + in->position * in->channels, (size_t)count * in->channels * sizeof(float));
    in->position += count;
    return (int64_t)count;
}

static int32_t seek_input(void *user, uint64_t frame) {
    input *in = user;
    if (frame > in->length) return LND_ERR_INVALID_ARG;
    in->position = frame;
    return LND_OK;
}

static playback create(input *in, int32_t type, const LND_EFFECT_CONFIG *config) {
    LND_SOURCE_PROCS procs = {.read = read_input, .seek = seek_input, .length_frames = in->length};
    playback p = {0};
    p.source = LND_SourceCreateProc(&procs, in, LND_FORMAT_F32, in->channels, in->rate, LND_GRAPH_SOURCE_DIRECT | (in->live ? LND_SOURCE_LIVE : 0));
    p.sound = LND_SourceEnsureSound(p.source, nullptr);
    p.node = LND_NodeCreateEffect(in->channels, in->rate, type, config);
    CHECK(p.source && p.sound && p.node);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(p.source), p.node) == LND_OK);
    p.renderer = LND_RendererCreateNode(p.node);
    CHECK(p.renderer && LND_SoundPlay(p.sound) == LND_OK);
    return p;
}

static void destroy(playback *p) {
    CHECK(LND_RendererFree(p->renderer) == LND_OK);
    CHECK(LND_NodeFree(p->node) == LND_OK);
    CHECK(LND_SourceFree(p->source) == LND_OK);
}

static size_t read_all(playback *p, float *out, size_t capacity, size_t chunk, uint32_t channels) {
    LND_PCM pcm = {.data = out, .frames = capacity, .channels = channels, .format = LND_FORMAT_F32};
    size_t total = 0;
    while (total < capacity) {
        size_t count = capacity - total < chunk ? capacity - total : chunk;
        int64_t got = LND_RendererReadPcm(p->renderer, &pcm, total, count);
        CHECK(got >= 0 && (uint64_t)got <= count);
        if (got <= 0) break;
        total += (size_t)got;
    }
    return total;
}

static void tone(float *out, size_t frames, uint32_t channels, uint32_t rate, float hz) {
    for (size_t f = 0; f < frames; f++)
        for (uint32_t c = 0; c < channels; c++)
            out[f * channels + c] = 0.4f * sinf((float)(6.283185307179586 * hz * f / rate)) * (1 - 0.02f * c);
}

static void equivalent(int32_t format, int32_t layout) {
    begin(format, layout);
    static float data[8192], first[8192], second[8192];
    tone(data, 4096, 2, 16000, 317);
    LND_EFFECT_CONFIG config = {.flags = LND_EFFECT_NO_TAIL};
    for (int32_t type = 0; type < LND_EFFECT_COUNT; type++) {
        input a = {.data = data, .length = 4096, .channels = 2, .rate = 16000}, b = a;
        playback pa = create(&a, type, &config), pb = create(&b, type, &config);
        deny_alloc = true;
        CHECK(read_all(&pa, first, 4096, 127, 2) == 4096);
        CHECK(read_all(&pb, second, 4096, 997, 2) == 4096);
        for (size_t i = 0; i < 8192; i++) {
            CHECK(isfinite(first[i]) && isfinite(second[i]));
            CHECK(fabsf(first[i] - second[i]) < 2e-5f);
        }
        CHECK(LND_NodeGetStatus(pa.node) == LND_SOURCE_EOF);
        LND_EFFECT_INFO info;
        CHECK(LND_NodeGetEffectInfo(pa.node, &info) == LND_OK && info.input_frames == 4096 && info.output_frames == 4096 && !info.error);
        CHECK(LND_SoundSeekFrames(pa.sound, 0) == LND_OK && LND_NodeResetEffect(pa.node) == LND_OK && LND_SoundPlay(pa.sound) == LND_OK);
        CHECK(read_all(&pa, second, 4096, 53, 2) == 4096);
        for (size_t i = 0; i < 8192; i++)
            CHECK(fabsf(first[i] - second[i]) < 2e-5f);
        deny_alloc = false;
        destroy(&pa);
        destroy(&pb);
    }
    finish();
}

static void echo(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[2] = {1, 0}, out[2000] = {0};
    LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_DRY, 0}, {LND_EFFECT_PARAM_WET, 1}, {LND_EFFECT_PARAM_DELAY_MS, 10}, {LND_EFFECT_PARAM_FEEDBACK, -0.5f}};
    LND_EFFECT_CONFIG config = {.params = params, .param_count = 4, .max_delay_ms = 10, .tail_ms = 100};
    input in = {.data = data, .length = 1, .channels = 2, .rate = 8000};
    playback p = create(&in, LND_EFFECT_ECHO, &config);
    deny_alloc = true;
    size_t n = read_all(&p, out, 1000, 37, 2);
    CHECK(n == 801 && LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
    CHECK(out[80 * 2] == 0 && out[80 * 2 + 1] == 1);
    CHECK(out[160 * 2] == -0.5f && out[160 * 2 + 1] == 0);
    CHECK(out[240 * 2 + 1] == 0.25f);
    for (size_t i = 0; i < n; i++)
        if (i % 80) CHECK(out[i * 2] == 0 && out[i * 2 + 1] == 0);
    deny_alloc = false;
    destroy(&p);
    params[3].value = 0;
    in.position = 0;
    p = create(&in, LND_EFFECT_ECHO, &config);
    CHECK(read_all(&p, out, 1000, 11, 2) == 81);
    destroy(&p);
    in = (input){.data = data, .length = 1, .channels = 1, .rate = 8000};
    params[2].value = 0.001f;
    p = create(&in, LND_EFFECT_ECHO, &config);
    CHECK(read_all(&p, out, 1000, 1, 1) == 2 && out[0] == 0 && out[1] == 1);
    destroy(&p);
    finish();
}

static void masks_and_bypass(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[1024], out[1024];
    tone(data, 256, 4, 16000, 511);
    LND_EFFECT_CONFIG config = {.flags = LND_EFFECT_SELECT_CHANNELS | LND_EFFECT_NO_TAIL, .channel_mask = 5};
    for (int32_t type = 0; type < LND_EFFECT_COUNT; type++) {
        input in = {.data = data, .length = 256, .channels = 4, .rate = 16000};
        playback p = create(&in, type, &config);
        CHECK(read_all(&p, out, 256, 17, 4) == 256);
        for (size_t f = 0; f < 256; f++)
            CHECK(out[f * 4 + 1] == data[f * 4 + 1] && out[f * 4 + 3] == data[f * 4 + 3]);
        CHECK(LND_NodeSetEffectChannelMask(p.node, UINT32_MAX) == LND_ERR_INVALID_ARG);
        CHECK(LND_SoundSeekFrames(p.sound, 0) == LND_OK && LND_NodeResetEffect(p.node) == LND_OK && LND_SoundPlay(p.sound) == LND_OK);
        CHECK(LND_NodeSetParam(p.node, LND_EFFECT_PARAM_BYPASS, 1) == LND_OK);
        CHECK(read_all(&p, out, 256, 101, 4) == 256 && memcmp(out, data, sizeof data) == 0);
        CHECK(LND_NodeSetParam(p.node, LND_EFFECT_PARAM_BYPASS, 0) == LND_OK && LND_NodeSetEffectChannelMask(p.node, 0) == LND_OK);
        CHECK(LND_SoundSeekFrames(p.sound, 0) == LND_OK && LND_NodeResetEffect(p.node) == LND_OK && LND_SoundPlay(p.sound) == LND_OK);
        CHECK(read_all(&p, out, 256, 32, 4) == 256 && memcmp(out, data, sizeof data) == 0);
        destroy(&p);
    }
    finish();
}

static void waiting(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[1024], full[4096], streamed[4096];
    tone(data, 1024, 1, 8000, 437);
    const int32_t types[] = {LND_EFFECT_ECHO, LND_EFFECT_REVERB, LND_EFFECT_FLANGER, LND_EFFECT_PHASER, LND_EFFECT_COMPRESSOR, LND_EFFECT_DYNAMIC_GAIN};
    for (size_t t = 0; t < sizeof types / sizeof *types; t++) {
        LND_EFFECT_CONFIG config = {.tail_ms = 100};
        input a = {.data = data, .length = 1024, .channels = 1, .rate = 8000}, b = a;
        b.live = true;
        playback pa = create(&a, types[t], &config), pb = create(&b, types[t], &config);
        LND_PCM pcm = {.data = streamed, .frames = 4096, .channels = 1, .format = LND_FORMAT_F32};
        deny_alloc = true;
        size_t count = read_all(&pa, full, 4096, 113, 1), total = 0;
        CHECK(LND_RendererReadPcm(pb.renderer, &pcm, 0, 91) == 0 && LND_NodeGetStatus(pb.node) == LND_SOURCE_WAITING);
        for (unsigned available = 31; available < 1024; available += 31) {
            b.available = available;
            int64_t got = LND_RendererReadPcm(pb.renderer, &pcm, total, 64);
            CHECK(got >= 0);
            if (got > 0) total += (size_t)got;
        }
        b.available = 1024;
        b.ended = true;
        while (total < 4096) {
            int64_t got = LND_RendererReadPcm(pb.renderer, &pcm, total, 97);
            CHECK(got >= 0);
            if (got <= 0) break;
            total += (size_t)got;
        }
        CHECK(total == count && LND_NodeGetStatus(pb.node) == LND_SOURCE_EOF);
        for (size_t i = 0; i < count; i++)
            CHECK(fabsf(full[i] - streamed[i]) < 1e-5f);
        deny_alloc = false;
        destroy(&pa);
        destroy(&pb);
    }
    finish();
}

static void responses(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    static float data[32000], out[32000];
    for (size_t i = 0; i < 32000; i++)
        data[i] = 0.8f;
    input in = {.data = data, .length = 16000, .channels = 2, .rate = 8000};
    LND_EFFECT_CONFIG config = {.flags = LND_EFFECT_NO_TAIL};
    playback p = create(&in, LND_EFFECT_COMPRESSOR, &config);
    CHECK(read_all(&p, out, 16000, 97, 2) == 16000);
    double expected = 0.8 * pow(pow(10, -12.0 / 20) / 0.8, 0.75);
    CHECK(fabs(out[30000] - expected) < 0.001 && out[30000] == out[30001]);
    destroy(&p);
    in.position = 0;
    p = create(&in, LND_EFFECT_ROTATION, &config);
    CHECK(LND_NodeSetParam(p.node, LND_EFFECT_PARAM_RATE_HZ, 1) == LND_OK);
    CHECK(read_all(&p, out, 16000, 97, 2) == 16000);
    CHECK(fabsf(out[4000] - 0.8f) < 0.001f && fabsf(out[4001]) < 0.001f);
    CHECK(fabsf(out[12000]) < 0.001f && fabsf(out[12001] - 0.8f) < 0.001f);
    destroy(&p);
    tone(data, 16000, 1, 8000, 1000);
    in = (input){.data = data, .length = 16000, .channels = 1, .rate = 8000};
    p = create(&in, LND_EFFECT_PEAK_EQ, &config);
    CHECK(LND_NodeSetParam(p.node, LND_EFFECT_PARAM_GAIN_DB, 6) == LND_OK);
    CHECK(read_all(&p, out, 16000, 37, 1) == 16000);
    double input_energy = 0, output_energy = 0;
    for (size_t i = 8000; i < 16000; i++) {
        input_energy += (double)data[i] * data[i];
        output_energy += (double)out[i] * out[i];
    }
    CHECK(fabs(10 * log10(output_energy / input_energy) - 6) < 0.01);
    destroy(&p);
    in.position = 0;
    p = create(&in, LND_EFFECT_BIQUAD, &config);
    CHECK(LND_NodeSetParam(p.node, LND_EFFECT_PARAM_FILTER_TYPE, LND_EFFECT_FILTER_NOTCH) == LND_OK);
    CHECK(read_all(&p, out, 16000, 63, 1) == 16000);
    output_energy = 0;
    for (size_t i = 8000; i < 16000; i++)
        output_energy += (double)out[i] * out[i];
    CHECK(output_energy / input_energy < 0.0001);
    destroy(&p);
    finish();
}

static void reverb(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[2] = {1, 0};
    static float out[32000];
    LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_DRY, 0}, {LND_EFFECT_PARAM_WET, 1}, {LND_EFFECT_PARAM_WIDTH, 0}};
    LND_EFFECT_CONFIG config = {.params = params, .param_count = 3, .tail_ms = 1000};
    input in = {.data = data, .length = 1, .channels = 2, .rate = 8000};
    playback p = create(&in, LND_EFFECT_REVERB, &config);
    size_t n = read_all(&p, out, 16000, 127, 2);
    CHECK(n == 8001);
    double energy = 0;
    for (size_t i = 0; i < n; i++) {
        CHECK(out[i * 2] == out[i * 2 + 1] && isfinite(out[i * 2]));
        energy += (double)out[i * 2] * out[i * 2];
    }
    CHECK(energy > 0.001 && out[0] == 0);
    destroy(&p);
    in.position = 0;
    params[2] = (LND_EFFECT_PARAM){LND_EFFECT_PARAM_FREEZE, 1};
    p = create(&in, LND_EFFECT_REVERB, &config);
    n = read_all(&p, out, 16000, 257, 2);
    for (size_t i = 0; i < n * 2; i++)
        CHECK(out[i] == 0);
    destroy(&p);
    finish();
}

static void validation(void) {
    CHECK(!LND_NodeCreateFlanger(2, 48000, nullptr) && LND_ErrorGetLast() == LND_ERR_STATE);
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    CHECK(!LND_NodeCreateEffect(1, 8000, -1, nullptr));
    CHECK(!LND_NodeCreateEffect(33, 8000, 0, nullptr));
    CHECK(!LND_NodeCreateEffect(1, 7999, 0, nullptr));
    CHECK(!LND_NodeCreateEffect(1, 8000, 0, &(LND_EFFECT_CONFIG){.param_count = 1}));
    CHECK(!LND_NodeCreateEffect(1, 8000, 0, &(LND_EFFECT_CONFIG){.max_delay_ms = INFINITY}));
    const int32_t invalid[] = {INT_MIN, -1, 255, LND_EFFECT_PARAM_END, INT_MAX};
    for (int32_t type = 0; type < LND_EFFECT_COUNT; type++) {
        LND_NODE *node = LND_NodeCreateEffect(2, 16000, type, nullptr);
        CHECK(node);
        for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++)
            CHECK(LND_NodeSetParam(node, invalid[i], 1) == LND_ERR_INVALID_ARG);
        CHECK(LND_NodeSetParam(node, LND_EFFECT_PARAM_BYPASS, 0.5f) == LND_ERR_INVALID_ARG);
        CHECK(LND_NodeSetParam(node, LND_EFFECT_PARAM_BYPASS, NAN) == LND_ERR_INVALID_ARG);
        CHECK(LND_NodeResetEffect(node) == LND_OK);
        CHECK(LND_NodeFree(node) == LND_OK);
        unsigned live = allocations - frees;
        bool success = false;
        for (int failure = 0; failure < 30 && !success; failure++) {
            fail_after = failure;
            node = LND_NodeCreateEffect(1, 8000, type, nullptr);
            fail_after = -1;
            success = node != nullptr;
            CHECK(success || LND_ErrorGetLast() == LND_ERR_OUT_OF_MEMORY);
            if (node) CHECK(LND_NodeFree(node) == LND_OK);
            CHECK(allocations - frees == live);
        }
        CHECK(success);
    }
    CHECK(LND_NodeResetEffect(nullptr) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetEffectChannelMask(nullptr, 1) == LND_ERR_INVALID_ARG);
    finish();
}

#if LND_MODULE_SLIDE
static void slides(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    static float data[4096], out[4096], other[4096];
    tone(data, 4096, 1, 8000, 350);
    const struct {
        int32_t type, param;
        float value;
    } cases[] = {{LND_EFFECT_FLANGER, LND_EFFECT_PARAM_MAX_DELAY_MS, 15},     {LND_EFFECT_ECHO, LND_EFFECT_PARAM_FEEDBACK, -0.5f},
                 {LND_EFFECT_REVERB, LND_EFFECT_PARAM_DAMPING, 0.8f},         {LND_EFFECT_PHASER, LND_EFFECT_PARAM_RANGE_OCTAVES, 7},
                 {LND_EFFECT_COMPRESSOR, LND_EFFECT_PARAM_THRESHOLD_DB, -24}, {LND_EFFECT_BIQUAD, LND_EFFECT_PARAM_FREQUENCY_HZ, 2500}};
    LND_EFFECT_CONFIG config = {.flags = LND_EFFECT_NO_TAIL};
    LND_SLIDE_CONFIG slide = {.duration_frames = 3000, .step_frames = 16, .curve = LND_SLIDE_LINEAR};
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        input a = {.data = data, .length = 4096, .channels = 1, .rate = 8000}, b = a;
        playback pa = create(&a, cases[i].type, &config), pb = create(&b, cases[i].type, &config);
        CHECK(LND_NodeSlideParam(pa.node, cases[i].param, cases[i].value, &slide) == LND_OK);
        CHECK(LND_NodeSlideParam(pb.node, cases[i].param, cases[i].value, &slide) == LND_OK);
        CHECK(LND_NodeSlideParam(pa.node, LND_EFFECT_PARAM_BYPASS, 1, &slide) != LND_OK);
        deny_alloc = true;
        CHECK(read_all(&pa, out, 4096, 31, 1) == 4096);
        CHECK(read_all(&pb, other, 4096, 997, 1) == 4096);
        for (size_t f = 0; f < 4096; f++)
            CHECK(fabsf(out[f] - other[f]) < 0.0001f);
        CHECK(!LND_NodeIsSliding(pa.node, cases[i].param));
        CHECK(LND_NodeGetParam(pa.node, cases[i].param) == cases[i].value);
        deny_alloc = false;
        destroy(&pa);
        destroy(&pb);
    }
    finish();
}
#endif

static void formats_and_edges(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    static float data[32 * 513], out[32 * 513];
    const struct {
        uint32_t channels, rate;
    } modes[] = {{1, 8000}, {3, 44100}, {8, 192000}, {32, 384000}};
    const uint32_t lengths[] = {0, 1, 2, 127, 128, 129, 513};
    LND_EFFECT_CONFIG config = {.flags = LND_EFFECT_NO_TAIL};
    for (size_t m = 0; m < sizeof modes / sizeof *modes; m++) {
        tone(data, 513, modes[m].channels, modes[m].rate, 337);
        for (int32_t type = 0; type < LND_EFFECT_COUNT; type++) {
            input in = {.data = data, .length = 513, .channels = modes[m].channels, .rate = modes[m].rate};
            playback p = create(&in, type, &config);
            deny_alloc = true;
            CHECK(read_all(&p, out, 513, 17, in.channels) == 513);
            for (size_t i = 0; i < 513 * in.channels; i++)
                CHECK(isfinite(out[i]));
            deny_alloc = false;
            destroy(&p);
        }
    }
    for (size_t i = 0; i < sizeof lengths / sizeof *lengths; i++) {
        input in = {.data = data, .length = lengths[i], .channels = 1, .rate = 8000};
        playback p = create(&in, LND_EFFECT_FLANGER, &config);
        CHECK(read_all(&p, out, 513, 127, 1) == lengths[i]);
        CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
        destroy(&p);
    }
    finish();
}

static void dynamics(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    static float data[48000], out[48000];
    for (size_t i = 0; i < 48000; i++)
        data[i] = 0.1f;
    LND_EFFECT_CONFIG config = {.flags = LND_EFFECT_NO_TAIL};
    input in = {.data = data, .length = 48000, .channels = 1, .rate = 8000};
    playback p = create(&in, LND_EFFECT_DYNAMIC_GAIN, &config);
    CHECK(read_all(&p, out, 48000, 113, 1) == 48000);
    CHECK(out[0] < 0.11f && fabsf(out[47999] - 0.95f) < 0.001f);
    destroy(&p);
    in.position = 0;
    LND_EFFECT_PARAM parameter = {LND_EFFECT_PARAM_GAIN_DELAY_MS, 200};
    config.params = &parameter;
    config.param_count = 1;
    p = create(&in, LND_EFFECT_DYNAMIC_GAIN, &config);
    CHECK(read_all(&p, out, 48000, 97, 1) == 48000);
    CHECK(fabsf(out[799] - 0.1f) < 0.001f);
    destroy(&p);
    tone(data, 48000, 1, 8000, 125);
    in.position = 0;
    config.params = nullptr;
    config.param_count = 0;
    p = create(&in, LND_EFFECT_DISTORTION, &config);
    CHECK(LND_NodeSetParam(p.node, LND_EFFECT_PARAM_DRY, 0) == LND_OK);
    CHECK(LND_NodeSetParam(p.node, LND_EFFECT_PARAM_WET, 1) == LND_OK);
    CHECK(read_all(&p, out, 48000, 67, 1) == 48000);
    double harmonic = 0, dc = 0;
    for (size_t i = 8000; i < 48000; i++) {
        dc += out[i];
        harmonic += out[i] * sin(6.283185307179586 * 375 * i / 8000);
    }
    CHECK(fabs(harmonic) > 100 && fabs(dc) < 0.1);
    destroy(&p);
    finish();
}

static void errors(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[8] = {NAN}, out[256];
    input in = {.data = data, .length = 8, .channels = 1, .rate = 8000};
    playback p = create(&in, LND_EFFECT_CHORUS, nullptr);
    LND_PCM pcm = {.data = out, .frames = 256, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_RendererReadPcm(p.renderer, &pcm, 0, 256) == LND_ERR_FORMAT);
    LND_EFFECT_INFO info;
    CHECK(LND_NodeGetEffectInfo(p.node, &info) == LND_OK && info.error == LND_ERR_FORMAT);
    data[0] = 0.5f;
    CHECK(LND_SoundSeekFrames(p.sound, 0) == LND_OK && LND_NodeResetEffect(p.node) == LND_OK && LND_SoundPlay(p.sound) == LND_OK);
    CHECK(LND_RendererReadPcm(p.renderer, &pcm, 0, 256) > 0);
    destroy(&p);
    in.position = 0;
    in.error = LND_ERR_IO;
    p = create(&in, LND_EFFECT_REVERB, nullptr);
    CHECK(LND_RendererReadPcm(p.renderer, &pcm, 0, 256) == LND_ERR_IO);
    destroy(&p);
    LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_DRY, 1}, {LND_EFFECT_PARAM_DRY, 0.5f}};
    CHECK(!LND_NodeCreateEcho(1, 8000, &(LND_EFFECT_CONFIG){.params = params, .param_count = 2}));
    params[1] = (LND_EFFECT_PARAM){LND_EFFECT_PARAM_DELAY_MS, 2000};
    CHECK(!LND_NodeCreateEcho(1, 8000, &(LND_EFFECT_CONFIG){.params = params, .param_count = 2}));
    LND_NODE *node = LND_NodeCreateEcho(1, 8000, &(LND_EFFECT_CONFIG){.params = params, .param_count = 2, .max_delay_ms = 2000});
    CHECK(node);
    CHECK(LND_NodeFree(node) == LND_OK);
    finish();
}

static double energy(const float *pcm, size_t start, size_t end) {
    double sum = 0;
    for (size_t i = start; i < end; i++)
        sum += (double)pcm[i] * pcm[i];
    return sum;
}

static void filter_responses(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    static float data[16000], out[16000];
    tone(data, 16000, 1, 8000, 1000);
    double reference = energy(data, 8000, 16000);
    const double gains[] = {0.70710678, 0.70710678, 1, 0, 1.99526231, 1.41253754, 1.41253754, 1, 0.70710678};
    for (int32_t type = LND_EFFECT_FILTER_LOWPASS; type <= LND_EFFECT_FILTER_BANDPASS_Q; type++) {
        LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_FILTER_TYPE, (float)type}, {LND_EFFECT_PARAM_GAIN_DB, 6}, {LND_EFFECT_PARAM_Q, 0.70710678f}};
        LND_EFFECT_CONFIG config = {.params = params, .param_count = 3, .flags = LND_EFFECT_NO_TAIL};
        input in = {.data = data, .length = 16000, .channels = 1, .rate = 8000};
        playback p = create(&in, LND_EFFECT_BIQUAD, &config);
        CHECK(read_all(&p, out, 16000, 101, 1) == 16000);
        CHECK(fabs(sqrt(energy(out, 8000, 16000) / reference) - gains[type]) < 0.001);
        destroy(&p);
    }
    const struct {
        float frequency, bandwidth, q, slope, gain;
    } limits[] = {{0.001f, 0, 0, 0, -120},    {0.001f, 10, 1000, 1, 120},    {3900, 10, 0, 0, -120},
                  {384000, 10, 1000, 1, 120}, {1000, 0, 1000, 0.0001f, 120}, {1000, 10, 0.0001f, 1, -120}};
    for (int32_t type = 0; type <= LND_EFFECT_FILTER_BANDPASS_Q; type++) {
        for (size_t i = 0; i < sizeof limits / sizeof *limits; i++) {
            LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_FILTER_TYPE, (float)type},
                                         {LND_EFFECT_PARAM_FREQUENCY_HZ, limits[i].frequency},
                                         {LND_EFFECT_PARAM_BANDWIDTH_OCTAVES, limits[i].bandwidth},
                                         {LND_EFFECT_PARAM_Q, limits[i].q},
                                         {LND_EFFECT_PARAM_SLOPE, limits[i].slope},
                                         {LND_EFFECT_PARAM_GAIN_DB, limits[i].gain}};
            LND_EFFECT_CONFIG config = {.params = params, .param_count = 6, .flags = LND_EFFECT_NO_TAIL};
            input in = {.data = data, .length = 4096, .channels = 1, .rate = 8000};
            playback p = create(&in, LND_EFFECT_BIQUAD, &config);
            CHECK(read_all(&p, out, 4096, 109, 1) == 4096);
            for (size_t f = 0; f < 4096; f++)
                CHECK(isfinite(out[f]));
            destroy(&p);
        }
    }
    finish();
}

static void modulation_responses(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    static float data[16000], out[16000];
    tone(data, 16000, 1, 8000, 1000);
    double reference = energy(data, 8000, 16000);
    const int32_t types[] = {LND_EFFECT_PHASER, LND_EFFECT_AUTOWAH};
    for (size_t t = 0; t < sizeof types / sizeof *types; t++) {
        LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_DRY, 0},     {LND_EFFECT_PARAM_WET, 1},           {LND_EFFECT_PARAM_FEEDBACK, 0},
                                     {LND_EFFECT_PARAM_RATE_HZ, 0}, {LND_EFFECT_PARAM_RANGE_OCTAVES, 0}, {LND_EFFECT_PARAM_FREQUENCY_HZ, 1000}};
        LND_EFFECT_CONFIG config = {.params = params, .param_count = 6, .flags = LND_EFFECT_NO_TAIL};
        input in = {.data = data, .length = 16000, .channels = 1, .rate = 8000};
        playback p = create(&in, types[t], &config);
        CHECK(read_all(&p, out, 16000, 71, 1) == 16000);
        CHECK(fabs(energy(out, 8000, 16000) / reference - 1) < 0.002);
        CHECK(LND_NodeSetParam(p.node, LND_EFFECT_PARAM_FREQUENCY_HZ, 100) == LND_OK);
        CHECK(LND_SoundSeekFrames(p.sound, 0) == LND_OK && LND_NodeResetEffect(p.node) == LND_OK && LND_SoundPlay(p.sound) == LND_OK);
        CHECK(read_all(&p, out, 16000, 43, 1) == 16000);
        double ratio = energy(out, 8000, 16000) / reference;
        CHECK(types[t] == LND_EFFECT_PHASER ? fabs(ratio - 1) < 0.002 : ratio < 0.03);
        destroy(&p);
    }
    for (int32_t type = LND_EFFECT_CHORUS; type <= LND_EFFECT_FLANGER; type++) {
        LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_DRY, 0},
                                     {LND_EFFECT_PARAM_WET, 1},
                                     {LND_EFFECT_PARAM_FEEDBACK, 0},
                                     {LND_EFFECT_PARAM_MIN_DELAY_MS, 1.0625f},
                                     {LND_EFFECT_PARAM_MAX_DELAY_MS, 1.0625f},
                                     {LND_EFFECT_PARAM_SWEEP_MS_PER_SECOND, 0}};
        LND_EFFECT_CONFIG config = {.params = params, .param_count = 6, .flags = LND_EFFECT_NO_TAIL};
        input in = {.data = data, .length = 16000, .channels = 1, .rate = 8000};
        playback p = create(&in, type, &config);
        CHECK(read_all(&p, out, 16000, 127, 1) == 16000);
        for (size_t f = 9; f < 16000; f++)
            CHECK(fabsf(out[f] - (data[f - 8] + data[f - 9]) * 0.5f) < 1e-6f);
        destroy(&p);
    }
    finish();
}

static void freeze_history(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    static float data[24000], out[24000];
    tone(data, 2000, 1, 8000, 337);
    LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_DRY, 0}, {LND_EFFECT_PARAM_WET, 1}};
    LND_EFFECT_CONFIG config = {.params = params, .param_count = 2, .flags = LND_EFFECT_NO_TAIL};
    input in = {.data = data, .length = 24000, .channels = 1, .rate = 8000};
    playback p = create(&in, LND_EFFECT_REVERB, &config);
    CHECK(read_all(&p, out, 2000, 125, 1) == 2000);
    CHECK(LND_NodeSetParam(p.node, LND_EFFECT_PARAM_FREEZE, 1) == LND_OK);
    CHECK(read_all(&p, out, 22000, 125, 1) == 22000);
    CHECK(energy(out, 14000, 22000) > 0.1);
    CHECK(LND_NodeResetEffect(p.node) == LND_OK);
    CHECK(LND_SoundSeekFrames(p.sound, 2000) == LND_OK && LND_SoundPlay(p.sound) == LND_OK);
    CHECK(read_all(&p, out, 22000, 125, 1) == 22000);
    CHECK(energy(out, 0, 22000) == 0);
    destroy(&p);
    finish();
}

#if LND_MODULE_NOTIFY
static void end_notification(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data = 1, out[100];
    input in = {.data = &data, .length = 1, .channels = 1, .rate = 8000};
    LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_DELAY_MS, 10}, {LND_EFFECT_PARAM_FEEDBACK, 0}};
    LND_EFFECT_CONFIG config = {.params = params, .param_count = 2};
    playback p = create(&in, LND_EFFECT_ECHO, &config);
    LND_SUBSCRIPTION_CONFIG subscription = {.type = LND_NOTIFY_END};
    LND_SUBSCRIPTION *end = LND_NodeSubscribe(p.node, &subscription);
    LND_NOTIFICATION event;
    CHECK(end);
    CHECK(read_all(&p, out, 80, 20, 1) == 80);
    CHECK(LND_SubscriptionRead(end, &event) == 0);
    CHECK(read_all(&p, out, 100, 20, 1) == 1);
    CHECK(LND_SubscriptionRead(end, &event) == 1 && event.type == LND_NOTIFY_END && event.position_frames == 81);
    CHECK(LND_SubscriptionRead(end, &event) == 0);
    CHECK(LND_SubscriptionFree(end) == LND_OK);
    destroy(&p);
    finish();
}
#endif

int main(void) {
    validation();
    formats_and_edges();
    dynamics();
    errors();
    equivalent(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    equivalent(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    echo();
    masks_and_bypass();
    waiting();
    responses();
    reverb();
    filter_responses();
    modulation_responses();
    freeze_history();
#if LND_MODULE_NOTIFY
    end_notification();
#endif
#if LND_MODULE_SLIDE
    slides();
#endif
    return report();
}
