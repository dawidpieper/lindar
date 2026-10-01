#include "stream_test.h"
#include "lindar_dsp.h"
#if LND_MODULE_SLIDE
#include "lindar_slide.h"
#endif

#include <math.h>

typedef struct input {
    const float *data;
    uint64_t length, position, available;
    uint32_t channels;
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
    uint64_t got = end > in->position ? end - in->position : 0;
    if (got > frames) got = frames;
    if (!got) return in->live && !in->ended ? 0 : LND_READ_EOF;
    memcpy(dst, in->data + in->position * in->channels, (size_t)got * in->channels * sizeof(float));
    in->position += got;
    return (int64_t)got;
}

static int32_t seek_input(void *user, uint64_t frame) {
    input *in = user;
    if (frame > in->length) return LND_ERR_INVALID_ARG;
    in->position = frame;
    return LND_OK;
}

static playback create(input *in, float ratio, uint32_t quality) {
    LND_SOURCE_PROCS procs = {.read = read_input, .seek = seek_input, .length_frames = in->live ? 0 : in->length};
    playback p = {0};
    p.source = LND_SourceCreateProc(&procs, in, LND_FORMAT_F32, in->channels, 8000, LND_GRAPH_SOURCE_DIRECT | (in->live ? LND_SOURCE_LIVE : 0));
    p.sound = LND_SourceEnsureSound(p.source, nullptr);
    p.node = LND_NodeCreateVarispeed(in->channels, 8000, ratio, quality);
    CHECK(p.source && p.sound && p.node);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(p.source), p.node) == LND_OK);
    p.renderer = LND_RendererCreateNode(p.node);
    CHECK(p.renderer != nullptr);
    CHECK(LND_SoundPlay(p.sound) == LND_OK);
    return p;
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

static void tone(float *data, size_t frames, uint32_t channels, float hz) {
    for (size_t f = 0; f < frames; f++)
        for (uint32_t c = 0; c < channels; c++)
            data[f * channels + c] = 0.5f * sinf((float)(6.283185307179586 * hz * f / 8000)) * (c & 1 ? -1 : 1);
}

static double measured(const float *data, size_t count, uint32_t channels) {
    unsigned crossings = 0;
    double first = 0, last = 0;
    for (size_t i = 65; i + 64 < count; i++) {
        float a = data[(i - 1) * channels], b = data[i * channels];
        if (a > 0 || b <= 0) continue;
        double at = i - 1 + (double)-a / (b - a);
        if (!crossings++) first = at;
        last = at;
    }
    return crossings > 1 ? (crossings - 1) * 8000 / (last - first) : 0;
}

static void test_rates(int32_t format, int32_t layout) {
    begin(format, layout);
    float data[4097 * 2], out[16389 * 2];
    tone(data, 4097, 2, 437);
    const float ratios[] = {0.25f, 0.5f, 1, 1.25f, 2, 4};
    for (uint32_t q = LND_RESAMPLE_LINEAR; q <= LND_RESAMPLE_SINC32; q++) {
        for (size_t r = 0; r < sizeof ratios / sizeof *ratios; r++) {
            input in = {.data = data, .length = 4097, .channels = 2};
            playback p = create(&in, ratios[r], q);
            CHECK(LND_NodeGetVarispeedBaseSampleRateHz(p.node) == 8000);
            CHECK(LND_NodeGetSampleRateHz(p.node) == 8000 && LND_RendererGetSampleRateHz(p.renderer) == 8000);
            CHECK(LND_NodeGetParam(p.node, LND_DSP_PARAM_RATE_RATIO) == ratios[r]);
            deny_alloc = true;
            size_t expected = (size_t)ceil(4097 / (double)ratios[r]);
            size_t count = read_all(&p, out, expected, 113, 2);
            CHECK(count == expected);
            CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
            CHECK(fabs(measured(out, count, 2) - 437 * ratios[r]) < 0.6);
            for (size_t f = 0; f < count; f++)
                CHECK(isfinite(out[2 * f]) && fabsf(out[2 * f] + out[2 * f + 1]) < 0.00008f);
            if (ratios[r] == 1) {
                for (size_t f = 0; f < count * 2; f++)
                    CHECK(fabsf(out[f] - data[f]) < (format == LND_FORMAT_F32 ? 0.000001f : 0.00008f));
            }
            deny_alloc = false;
            CHECK(LND_RendererFree(p.renderer) == LND_OK);
            CHECK(LND_NodeFree(p.node) == LND_OK);
            CHECK(LND_SourceFree(p.source) == LND_OK);
        }
    }
    finish();
}

static void test_edges(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[] = {0.25f, 0.5f, 0.125f, -0.25f, 0.375f}, out[48];
    for (uint32_t q = LND_RESAMPLE_LINEAR; q <= LND_RESAMPLE_SINC32; q++) {
        for (unsigned length = 0; length <= 5; length++) {
            input in = {.data = data, .length = length, .channels = 1};
            playback p = create(&in, 0.5f, q);
            deny_alloc = true;
            CHECK(read_all(&p, out, 48, 1, 1) == length * 2);
            CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
            if (length && q == LND_RESAMPLE_LINEAR) CHECK(out[length * 2 - 1] == data[length - 1]);
            deny_alloc = false;
            CHECK(LND_RendererFree(p.renderer) == LND_OK);
            CHECK(LND_NodeFree(p.node) == LND_OK);
            CHECK(LND_SourceFree(p.source) == LND_OK);
        }
    }
    finish();
}

static void test_chunks(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[1003], a[2100], b[2100];
    tone(data, 1003, 1, 313);
    for (uint32_t q = LND_RESAMPLE_LINEAR; q <= LND_RESAMPLE_SINC32; q++) {
        input ia = {.data = data, .length = 1003, .channels = 1}, ib = ia;
        playback pa = create(&ia, 0.73f, q), pb = create(&ib, 0.73f, q);
        CHECK(LND_NodeResetVarispeed(pa.node) == LND_OK);
        deny_alloc = true;
        size_t na = read_all(&pa, a, 2100, 4096, 1), nb = read_all(&pb, b, 2100, 3, 1);
        CHECK(na == nb && na == (size_t)ceil(1003 / 0.73));
        CHECK(memcmp(a, b, na * sizeof(float)) == 0);
        CHECK(LND_SoundSeekFrames(pa.sound, 0) == LND_OK);
        CHECK(LND_SoundPlay(pa.sound) == LND_OK);
        CHECK(LND_NodeResetVarispeed(pa.node) == LND_OK);
        CHECK(read_all(&pa, b, 2100, 7, 1) == na);
        CHECK(memcmp(a, b, na * sizeof(float)) == 0);
        deny_alloc = false;
        CHECK(LND_RendererFree(pa.renderer) == LND_OK);
        CHECK(LND_RendererFree(pb.renderer) == LND_OK);
        CHECK(LND_NodeFree(pa.node) == LND_OK);
        CHECK(LND_NodeFree(pb.node) == LND_OK);
        CHECK(LND_SourceFree(pa.source) == LND_OK);
        CHECK(LND_SourceFree(pb.source) == LND_OK);
    }
    finish();
}

static void test_live(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[301], reference[700], streamed[700];
    tone(data, 301, 1, 319);
    for (uint32_t q = LND_RESAMPLE_LINEAR; q <= LND_RESAMPLE_SINC32; q++) {
        input ia = {.data = data, .length = 301, .channels = 1}, ib = ia;
        ib.live = true;
        playback pa = create(&ia, 0.5f, q), pb = create(&ib, 0.5f, q);
        LND_PCM pcm = {.data = streamed, .frames = 700, .channels = 1, .format = LND_FORMAT_F32};
        deny_alloc = true;
        size_t expected = read_all(&pa, reference, 700, 113, 1), total = 0;
        CHECK(LND_RendererReadPcm(pb.renderer, &pcm, 0, 7) == 0);
        CHECK(LND_NodeGetStatus(pb.node) == LND_SOURCE_WAITING);
        for (unsigned end = 7; end <= 301; end += 7) {
            ib.available = end;
            int64_t got = LND_RendererReadPcm(pb.renderer, &pcm, total, 23);
            CHECK(got >= 0 && got <= 23);
            if (got > 0) total += (size_t)got;
        }
        ib.available = 301;
        ib.ended = true;
        while (total < 700) {
            int64_t got = LND_RendererReadPcm(pb.renderer, &pcm, total, 700 - total);
            CHECK(got >= 0);
            if (got <= 0) break;
            total += (size_t)got;
        }
        CHECK(total == expected && total == 602);
        CHECK(memcmp(reference, streamed, total * sizeof(float)) == 0);
        CHECK(LND_NodeGetStatus(pb.node) == LND_SOURCE_EOF);
        deny_alloc = false;
        CHECK(LND_RendererFree(pa.renderer) == LND_OK);
        CHECK(LND_RendererFree(pb.renderer) == LND_OK);
        CHECK(LND_NodeFree(pa.node) == LND_OK);
        CHECK(LND_NodeFree(pb.node) == LND_OK);
        CHECK(LND_SourceFree(pa.source) == LND_OK);
        CHECK(LND_SourceFree(pb.source) == LND_OK);
    }
    input in = {.data = data, .length = 301, .channels = 1, .live = true, .error = LND_ERR_IO};
    playback p = create(&in, 1, LND_RESAMPLE_SINC16);
    LND_PCM pcm = {.data = streamed, .frames = 700, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(p.renderer, &pcm, 0, 16) == LND_ERR_IO);
    finish();
}

static void test_fractional_length(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[44100] = {0}, output[48004];
    const struct { uint64_t length; float frequency; } cases[] = {{44100, 7350}, {301, 5600}, {441, 7350}};
    for (uint32_t q = LND_RESAMPLE_LINEAR; q <= LND_RESAMPLE_SINC32; q++) {
        for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
            input in = {.data = data, .length = cases[i].length, .channels = 1};
            playback p = create(&in, 1, q);
            CHECK(LND_NodeSetParam(p.node, LND_DSP_PARAM_RATE_RATIO, cases[i].frequency / 8000) == LND_OK);
            deny_alloc = true;
            size_t expected = (size_t)ceil((double)in.length / (double)(cases[i].frequency / 8000));
            size_t count = read_all(&p, output, expected, 127, 1);
            CHECK(count == expected);
            CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
            CHECK(read_all(&p, output, 1, 1, 1) == 0);
            deny_alloc = false;
            CHECK(LND_RendererFree(p.renderer) == LND_OK);
            CHECK(LND_NodeFree(p.node) == LND_OK);
            CHECK(LND_SourceFree(p.source) == LND_OK);
        }
    }
    finish();
}

static void test_filter(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[4096], out[8192];
    tone(data, 4096, 1, 3000);
    input in = {.data = data, .length = 4096, .channels = 1};
    playback p = create(&in, 2, LND_RESAMPLE_SINC32);
    deny_alloc = true;
    size_t count = read_all(&p, out, 8192, 97, 1);
    CHECK(count == 2048);
    double power = 0;
    for (size_t i = 32; i + 32 < count; i++)
        power += (double)out[i] * out[i];
    CHECK(sqrt(power / (count - 64)) < 0.002);
    finish();
}

static void test_native(void) {
    begin(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    float data[] = {0.25f, -0.5f, 0.125f, 0.375f}, out[17];
    LND_PCM input_pcm = {.data = data, .frames = 4, .channels = 1, .format = LND_FORMAT_F32};
    LND_SOURCE_CONFIG config = {.pcm = &input_pcm, .channels = 1, .sample_rate_hz = 8000};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_NODE *node = LND_NodeCreateVarispeed(1, 8000, 0.5f, LND_RESAMPLE_LINEAR);
    CHECK(source && sound && node);
    CHECK(LND_SoundSetOutput(sound, node) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(node);
    CHECK(renderer && LND_SoundPlay(sound) == LND_OK);
    playback p = {.source = source, .sound = sound, .node = node, .renderer = renderer};
    CHECK(LND_NodeResetVarispeed(node) == LND_OK);
    deny_alloc = true;
    CHECK(read_all(&p, out, 17, 17, 1) == 8);
    CHECK(out[0] == data[0] && out[6] == data[3] && out[7] == data[3]);
    CHECK(LND_SoundSeekFrames(sound, 0) == LND_OK);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_NodeResetVarispeed(node) == LND_OK);
    CHECK(read_all(&p, out, 17, 3, 1) == 17);
    CHECK(out[0] == data[0] && out[8] == data[0] && out[16] == data[0]);
    CHECK(LND_NodeGetStatus(node) == LND_SOURCE_READY);
    finish();
}

static void test_params(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    CHECK(!LND_NodeCreateVarispeed(1, 8000, NAN, 0));
    CHECK(!LND_NodeCreateVarispeed(1, 8000, -1, 0));
    CHECK(!LND_NodeCreateVarispeed(1, 8000, 0.1f, 0));
    CHECK(!LND_NodeCreateVarispeed(1, 8000, 1, 9));
    CHECK(LND_NodeGetVarispeedBaseSampleRateHz(nullptr) == 0);
    CHECK(LND_NodeResetVarispeed(nullptr) == LND_ERR_INVALID_ARG);
    LND_NODE *node = LND_NodeCreateVarispeed(1, 8000, 0, 0);
    CHECK(node && LND_NodeGetParam(node, LND_DSP_PARAM_RATE_RATIO) == 1.0f);
    CHECK(LND_NodeSetParam(node, LND_DSP_PARAM_RATE_RATIO, 0) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(node, LND_DSP_PARAM_RATE_RATIO, INFINITY) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(node, LND_DSP_PARAM_RATE_RATIO, (32001.0f / 8000)) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(node, LND_DSP_PARAM_Q, 1) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeGetParam(node, LND_DSP_PARAM_RATE_RATIO) == 1.0f);
#if LND_MODULE_SLIDE
    LND_SLIDE_CONFIG cfg = {.duration_frames = 100, .step_frames = 1};
    CHECK(LND_NodeSlideParam(node, LND_DSP_PARAM_RATE_RATIO, 0, &cfg) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSlideParam(node, LND_DSP_PARAM_RATE_RATIO, (32001.0f / 8000), &cfg) == LND_ERR_INVALID_ARG);
#endif
    finish();
    bool succeeded = false;
    for (int fail = 0; fail < 48 && !succeeded; fail++) {
        begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
        fail_after = fail;
        node = LND_NodeCreateVarispeed(2, 8000, 0.5f, LND_RESAMPLE_SINC32);
        succeeded = node != nullptr;
        fail_after = -1;
        finish();
    }
    CHECK(succeeded);
}

#if LND_MODULE_SLIDE
static void test_changes(uint32_t quality) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[6000], a[300], b[300];
    for (unsigned i = 0; i < 6000; i++)
        data[i] = i / 10000.0f;
    input ia = {.data = data, .length = 6000, .channels = 1}, ib = ia;
    playback pa = create(&ia, 1, quality), pb = create(&ib, 1, quality);
    CHECK(LND_NodeSetParam(pa.node, LND_DSP_PARAM_RATE_RATIO, 1.0f) == LND_OK);
    LND_SLIDE_CONFIG cfg = {.duration_frames = 100, .step_frames = 1};
    CHECK(LND_NodeSlideParam(pa.node, LND_DSP_PARAM_RATE_RATIO, 2.0f, &cfg) == LND_OK);
    CHECK(LND_NodeSlideParam(pb.node, LND_DSP_PARAM_RATE_RATIO, 2.0f, &cfg) == LND_OK);
    deny_alloc = true;
    CHECK(read_all(&pa, a, 300, 300, 1) == 300);
    CHECK(read_all(&pb, b, 300, 3, 1) == 300);
    CHECK(memcmp(a, b, sizeof a) == 0);
    CHECK(LND_NodeGetParam(pa.node, LND_DSP_PARAM_RATE_RATIO) == 2.0f);
    CHECK(!LND_NodeIsSliding(pa.node, LND_DSP_PARAM_RATE_RATIO));
    CHECK(fabsf(a[299] - a[298] - 0.0002f) < 0.000001f);
    CHECK(LND_NodeSetParam(pa.node, LND_DSP_PARAM_RATE_RATIO, 0.5f) == LND_OK);
    float previous = a[299];
    CHECK(read_all(&pa, a, 300, 13, 1) == 300);
    CHECK(fabsf(a[0] - previous - 0.0002f) < 0.000001f);
    CHECK(fabsf(a[299] - a[298] - 0.00005f) < 0.000001f);
    CHECK(LND_NodeGetVarispeedBaseSampleRateHz(pa.node) == 8000);
    finish();
}
#endif

int main(void) {
    test_rates(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    test_rates(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    test_edges();
    test_chunks();
    test_live();
    test_fractional_length();
    test_filter();
    test_native();
    test_params();
#if LND_MODULE_SLIDE
    for (uint32_t q = LND_RESAMPLE_LINEAR; q <= LND_RESAMPLE_SINC32; q++)
        test_changes(q);
#endif
    return report();
}
