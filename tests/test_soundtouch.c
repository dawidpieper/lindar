#include "stream_test.h"
#include "lindar_soundtouch.h"
#if LND_MODULE_WAV_DECODER
#include "lindar_codecs.h"
#endif
#if LND_MODULE_SLIDE
#include "lindar_slide.h"
#endif

#include <math.h>

typedef struct input {
    const float *data;
    uint64_t length, position, available;
    uint32_t channels, sample_rate_hz;
    bool live, eof, reenter;
    int32_t error;
    LND_NODE *node;
} input;

typedef struct playback {
    LND_SOURCE *source;
    LND_SOUND *sound;
    LND_NODE *node;
    LND_RENDERER *renderer;
} playback;

static int64_t read_input(void *user, void *dst, uint64_t frames) {
    input *in = user;
    if (in->reenter) {
        LND_SOUNDTOUCH_INFO info;
        CHECK(LND_NodeGetSoundTouchInfo(in->node, &info) == LND_ERR_BUSY);
        CHECK(LND_NodeSetSoundTouchSetting(in->node, LND_SOUNDTOUCH_QUICK_SEEK, 1) == LND_ERR_BUSY);
        CHECK(LND_NodeSetSoundTouchPitchSemitones(in->node, 1) == LND_ERR_BUSY);
        CHECK(LND_NodeResetSoundTouch(in->node) == LND_ERR_BUSY);
        in->reenter = false;
    }
    if (in->error) return in->error;
    uint64_t end = in->live ? in->available : in->length;
    uint64_t got = end > in->position ? end - in->position : 0;
    if (got > frames) got = frames;
    if (!got) return in->live && !in->eof ? 0 : LND_READ_EOF;
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

static playback create(input *in, const LND_SOUNDTOUCH_CONFIG *config) {
    LND_SOURCE_PROCS procs = {.read = read_input, .seek = seek_input, .length_frames = in->live ? 0 : in->length};
    playback p = {0};
    uint32_t sample_rate_hz = in->sample_rate_hz ? in->sample_rate_hz : 16000;
    p.source = LND_SourceCreateProc(&procs, in, LND_FORMAT_F32, in->channels, sample_rate_hz, LND_GRAPH_SOURCE_DIRECT | (in->live ? LND_SOURCE_LIVE : 0));
    p.sound = LND_SourceEnsureSound(p.source, nullptr);
    p.node = LND_NodeCreateSoundTouch(in->channels, sample_rate_hz, config);
    CHECK(p.source && p.sound && p.node);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(p.source), p.node) == LND_OK);
    p.renderer = LND_RendererCreateNode(p.node);
    CHECK(p.renderer && LND_SoundPlay(p.sound) == LND_OK);
    in->node = p.node;
    return p;
}

static void destroy(playback *p) {
    deny_alloc = false;
    CHECK(LND_RendererFree(p->renderer) == LND_OK);
    CHECK(LND_NodeFree(p->node) == LND_OK);
    CHECK(LND_SourceFree(p->source) == LND_OK);
}

static size_t read_all(playback *p, float *out, size_t frames, size_t chunk, uint32_t channels) {
    LND_PCM pcm = {.data = out, .frames = frames, .channels = channels, .format = LND_FORMAT_F32};
    size_t total = 0;
    while (total < frames) {
        size_t count = frames - total < chunk ? frames - total : chunk;
        int64_t got = LND_RendererReadPcm(p->renderer, &pcm, total, count);
        CHECK(got >= 0 && (uint64_t)got <= count);
        if (got <= 0) break;
        total += (size_t)got;
    }
    return total;
}

static void tone(float *data, size_t frames, uint32_t channels) {
    for (size_t f = 0; f < frames; f++)
        for (uint32_t c = 0; c < channels; c++)
            data[f * channels + c] = (float)(0.5 * sin(6.283185307179586 * 437 * f / 16000)) * (c & 1 ? -1 : 1);
}

static double frequency(const float *data, size_t frames, uint32_t channels) {
    unsigned crossings = 0;
    double first = 0, last = 0;
    for (size_t i = frames / 4 + 1; i < frames * 3 / 4; i++) {
        float a = data[(i - 1) * channels], b = data[i * channels];
        if (a > 0 || b <= 0) continue;
        double at = i - 1 + (double)-a / (b - a);
        if (!crossings++) first = at;
        last = at;
    }
    return crossings > 1 ? (crossings - 1) * 16000 / (last - first) : 0;
}

static void test_modes(int32_t format, int32_t layout) {
    begin(format, layout);
    static float data[32000], out[128010];
    tone(data, 16000, 2);
    const LND_SOUNDTOUCH_CONFIG configs[] = {
        {0},
        {.pitch_ratio = 0.5f},
        {.pitch_ratio = 2},
        {.tempo_ratio = 0.5f},
        {.tempo_ratio = 2},
        {.rate_ratio = 0.5f},
        {.rate_ratio = 2},
        {.tempo_ratio = 0.8f, .pitch_ratio = 1.5f, .rate_ratio = 1.25f},
        {.tempo_ratio = 0.5f, .rate_ratio = 0.5f},
        {.tempo_ratio = 2, .pitch_ratio = 0.5f, .rate_ratio = 2},
        {.pitch_ratio = 1.5f, .flags = LND_SOUNDTOUCH_QUICK, .sequence_ms = 32, .seekwindow_ms = 10, .overlap_ms = 4},
    };
    for (size_t c = 0; c < sizeof configs / sizeof *configs; c++) {
        input in = {.data = data, .length = 16000, .channels = 2};
        playback p = create(&in, &configs[c]);
        float tempo = configs[c].tempo_ratio ? configs[c].tempo_ratio : 1, rate_ratio = configs[c].rate_ratio ? configs[c].rate_ratio : 1;
        float pitch = configs[c].pitch_ratio ? configs[c].pitch_ratio : 1;
        size_t expected = (size_t)(16000 / ((double)tempo * rate_ratio) + 0.5);
        LND_SOUNDTOUCH_INFO info;
        CHECK(LND_NodeGetSoundTouchInfo(p.node, &info) == LND_OK);
        CHECK(info.initial_latency_frames > 0 && info.input_sequence_frames > 0 && info.output_sequence_frames > 0);
        CHECK(fabs(info.duration_ratio - 1 / ((double)tempo * rate_ratio)) < 1e-8);
        CHECK(LND_NodeGetSampleRateHz(p.node) == 16000);
        deny_alloc = true;
        size_t count = read_all(&p, out, expected, 127, 2);
        CHECK(count == expected);
        CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
        CHECK(fabs(frequency(out, count, 2) - 437 * pitch * rate_ratio) < 437 * pitch * rate_ratio * 0.025);
        double power = 0;
        for (size_t f = 0; f < count; f++) {
            CHECK(isfinite(out[f * 2]) && fabsf(out[f * 2] + out[f * 2 + 1]) < 0.0001f);
            power += (double)out[f * 2] * out[f * 2];
        }
        CHECK(power / count > 0.02 && power / count < 0.3);
        CHECK(read_all(&p, out, 1, 1, 2) == 0);
        CHECK(LND_NodeGetSoundTouchInfo(p.node, &info) == LND_OK && info.input_ended && info.error == LND_OK);
        CHECK(info.input_frames == 16000 && info.output_frames == expected && info.buffered_output_frames == 0);
        destroy(&p);
    }
    finish();
}

static void test_short_and_channels(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_PLANAR);
    static float data[257 * 32], out[2048 * 32];
    tone(data, 257, 32);
    const uint32_t lengths[] = {0, 1, 7, 257};
    const uint32_t counts[] = {1, 2, 3, 4, 5, 6, 8, 32};
    for (size_t c = 0; c < sizeof counts / sizeof *counts; c++) {
        uint32_t channels = counts[c];
        for (size_t i = 0; i < sizeof lengths / sizeof *lengths; i++) {
            input in = {.data = data, .length = lengths[i], .channels = channels};
            playback p = create(&in, &(LND_SOUNDTOUCH_CONFIG){.tempo_ratio = 0.75f, .pitch_ratio = 1.5f});
            deny_alloc = true;
            size_t expected = (size_t)(lengths[i] / 0.75 + 0.5);
            CHECK(read_all(&p, out, 2048, 3, channels) == expected);
            CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
            for (size_t f = 0; f < expected * channels; f++)
                CHECK(isfinite(out[f]));
            destroy(&p);
        }
    }
    finish();
}

static void test_stream(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[6000], a[8100], b[8100];
    tone(data, 6000, 1);
    input ia = {.data = data, .length = 6000, .channels = 1}, ib = ia;
    ib.live = true;
    ib.reenter = true;
    LND_SOUNDTOUCH_CONFIG c = {.tempo_ratio = 0.75f, .pitch_ratio = 1.25f};
    playback pa = create(&ia, &c), pb = create(&ib, &c);
    CHECK(LND_NodeSetParam(pa.node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO, c.pitch_ratio) == LND_OK);
    deny_alloc = true;
    size_t expected = read_all(&pa, a, 8100, 8100, 1), total = 0;
    LND_PCM pcm = {.data = b, .frames = 8100, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_RendererReadPcm(pb.renderer, &pcm, 0, 31) == 0);
    CHECK(LND_NodeGetStatus(pb.node) == LND_SOURCE_WAITING);
    LND_SOUNDTOUCH_INFO info;
    CHECK(LND_NodeGetSoundTouchInfo(pb.node, &info) == LND_OK && info.input_frames == 0 && !info.input_ended);
    for (unsigned end = 37; end < 6000; end += 37) {
        ib.available = end;
        int64_t got = LND_RendererReadPcm(pb.renderer, &pcm, total, 43);
        CHECK(got >= 0);
        if (got > 0) total += (size_t)got;
    }
    ib.available = 6000;
    ib.eof = true;
    while (total < 8100) {
        int64_t got = LND_RendererReadPcm(pb.renderer, &pcm, total, 8100 - total);
        CHECK(got >= 0);
        if (got <= 0) break;
        total += (size_t)got;
    }
    CHECK(expected == 8000 && total == expected);
    for (size_t i = 0; i < total; i++)
        CHECK(fabsf(a[i] - b[i]) < 0.00001f);
    CHECK(LND_NodeGetStatus(pb.node) == LND_SOURCE_EOF);
    CHECK(LND_SoundSeekFrames(pa.sound, 0) == LND_OK && LND_SoundPlay(pa.sound) == LND_OK);
    CHECK(LND_NodeResetSoundTouch(pa.node) == LND_OK);
    CHECK(read_all(&pa, b, 8100, 7, 1) == expected);
    CHECK(memcmp(a, b, expected * sizeof(float)) == 0);
    CHECK(LND_SoundSeekFrames(pa.sound, 0) == LND_OK && LND_SoundPlay(pa.sound) == LND_OK);
    CHECK(LND_NodeResetSoundTouch(pa.node) == LND_OK);
    CHECK(read_all(&pa, b, 100, 100, 1) == 100);
    CHECK(LND_NodeGetSoundTouchInfo(pa.node, &info) == LND_OK && info.input_frames < 6000);
    uint64_t fed = info.input_frames, target = (uint64_t)(fed / 0.75 + 0.5);
    CHECK(LND_NodeEndSoundTouchInput(pa.node) == LND_OK);
    CHECK(read_all(&pa, b, 8100, 113, 1) + 100 == target);
    CHECK(ia.position == fed && LND_NodeGetStatus(pa.node) == LND_SOURCE_EOF);
    destroy(&pa);
    destroy(&pb);
    input bad = {.data = data, .length = 6000, .channels = 1, .live = true, .error = LND_ERR_IO};
    playback p = create(&bad, nullptr);
    CHECK(LND_RendererReadPcm(p.renderer, &pcm, 0, 31) == LND_ERR_IO);
    CHECK(LND_NodeGetSoundTouchInfo(p.node, &info) == LND_OK && info.error == LND_ERR_IO);
    destroy(&p);
    finish();
}

static void test_limits(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[6400];
    static float out[102410];
    tone(data, 3200, 2);
    const float values[] = {0.25f, 4};
    for (unsigned a = 0; a < 2; a++)
        for (unsigned b = 0; b < 2; b++)
            for (unsigned c = 0; c < 2; c++) {
                input in = {.data = data, .length = 3200, .channels = 2};
                playback p = create(&in, &(LND_SOUNDTOUCH_CONFIG){.tempo_ratio = values[a], .pitch_ratio = values[b], .rate_ratio = values[c]});
                deny_alloc = true;
                size_t expected = (size_t)(3200 / ((double)values[a] * values[c]) + 0.5);
                CHECK(read_all(&p, out, expected, 113, 2) == expected);
                CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
                for (size_t i = 0; i < expected * 2; i++)
                    CHECK(isfinite(out[i]));
                destroy(&p);
            }
    input in = {.data = data, .length = 2000, .channels = 2, .sample_rate_hz = 192000};
    playback p = create(&in, &(LND_SOUNDTOUCH_CONFIG){.tempo_ratio = 0.25f, .rate_ratio = 0.25f, .sequence_ms = 200, .seekwindow_ms = 100, .overlap_ms = 64});
    deny_alloc = true;
    CHECK(read_all(&p, out, 32000, 127, 2) == 32000);
    CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
    destroy(&p);
    finish();
}

static void test_settings(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    CHECK(strcmp(LND_SoundTouchGetVersion(), "2.4.1") == 0);
    CHECK(!LND_NodeCreateSoundTouch(33, 16000, nullptr));
    CHECK(!LND_NodeCreateSoundTouch(1, 100, nullptr));
    CHECK(!LND_NodeCreateSoundTouch(1, 16000, &(LND_SOUNDTOUCH_CONFIG){.tempo_ratio = NAN}));
    CHECK(!LND_NodeCreateSoundTouch(1, 16000, &(LND_SOUNDTOUCH_CONFIG){.pitch_ratio = -1}));
    CHECK(!LND_NodeCreateSoundTouch(1, 16000, &(LND_SOUNDTOUCH_CONFIG){.rate_ratio = 5}));
    CHECK(!LND_NodeCreateSoundTouch(1, 16000, &(LND_SOUNDTOUCH_CONFIG){.aa_filter_length = 9}));
    CHECK(!LND_NodeCreateSoundTouch(1, 16000, &(LND_SOUNDTOUCH_CONFIG){.flags = 0x80}));
    LND_NODE *node = LND_NodeCreateSoundTouch(1, 16000, nullptr);
    CHECK(node != nullptr);
    CHECK(LND_NodeGetSoundTouchSetting(node, LND_SOUNDTOUCH_AA_FILTER) == 1);
    CHECK(LND_NodeSetParam(node, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO, 0) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO, INFINITY) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(node, LND_PARAM_USER + 3, 1) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetSoundTouchPitchSemitones(node, 12) == LND_OK && LND_NodeGetSoundTouchPitchSemitones(node) == 12);
    CHECK(LND_NodeGetParam(node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO) == 2);
    CHECK(LND_NodeSetSoundTouchTempoChangePercent(node, -50) == LND_OK && LND_NodeGetParam(node, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO) == 0.5f);
    CHECK(LND_NodeSetSoundTouchRateChangePercent(node, 100) == LND_OK && LND_NodeGetParam(node, LND_SOUNDTOUCH_PARAM_RATE_RATIO) == 2);
    CHECK(LND_NodeSetSoundTouchPitchSemitones(node, 25) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetSoundTouchTempoChangePercent(node, -100) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetSoundTouchRateChangePercent(node, INFINITY) == LND_ERR_INVALID_ARG);
    const int settings[] = {0, 128, 1, 60, 12, 6};
    for (int i = 0; i < 6; i++) {
        CHECK(LND_NodeSetSoundTouchSetting(node, i, settings[i]) == LND_OK);
        CHECK(LND_NodeGetSoundTouchSetting(node, i) == settings[i]);
    }
    CHECK(LND_NodeSetSoundTouchSetting(node, LND_SOUNDTOUCH_AA_FILTER, 1) == LND_OK);
    CHECK(LND_NodeGetSoundTouchSetting(node, LND_SOUNDTOUCH_AA_FILTER) == 1);
    CHECK(LND_NodeSetSoundTouchSetting(node, LND_SOUNDTOUCH_AA_FILTER_LENGTH, 15) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetSoundTouchSetting(node, LND_SOUNDTOUCH_SEQUENCE_MS, 300) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeGetSoundTouchSetting(node, 8) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeResetSoundTouch(nullptr) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeEndSoundTouchInput(nullptr) == LND_ERR_INVALID_ARG);
    LND_NODE *bus = LND_NodeCreateBus(1, 16000);
    CHECK(LND_NodeSetSoundTouchPitchSemitones(bus, 1) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeGetSoundTouchInfo(node, nullptr) == LND_ERR_INVALID_ARG);
#if LND_MODULE_SLIDE
    CHECK(LND_NodeSlideParam(node, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO, 5, &(LND_SLIDE_CONFIG){.duration_frames = 100}) == LND_ERR_INVALID_ARG);
#endif
    finish();
    bool success = false;
    for (int fail = 0; fail < 32 && !success; fail++) {
        begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
        fail_after = fail;
        node = LND_NodeCreateSoundTouch(2, 16000, nullptr);
        success = node != nullptr;
        fail_after = -1;
        finish();
    }
    CHECK(success);
}

#if LND_MODULE_SLIDE
static void test_slide(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float data[16000], a[4000], b[4000];
    tone(data, 16000, 1);
    input ia = {.data = data, .length = 16000, .channels = 1}, ib = ia;
    playback pa = create(&ia, nullptr), pb = create(&ib, nullptr);
    CHECK(LND_NodeSetParam(pa.node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO, 1) == LND_OK);
    LND_SLIDE_CONFIG c = {.duration_frames = 3000, .step_frames = 64, .curve = LND_SLIDE_LOGARITHMIC};
    CHECK(LND_NodeSlideParam(pa.node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO, 1.5f, &c) == LND_OK);
    CHECK(LND_NodeSlideParam(pb.node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO, 1.5f, &c) == LND_OK);
    CHECK(LND_NodeSlideParam(pa.node, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO, 0.75f, &c) == LND_OK);
    CHECK(LND_NodeSlideParam(pb.node, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO, 0.75f, &c) == LND_OK);
    deny_alloc = true;
    CHECK(read_all(&pa, a, 4000, 4000, 1) == 4000);
    CHECK(read_all(&pb, b, 4000, 3, 1) == 4000);
    CHECK(memcmp(a, b, sizeof a) == 0);
    CHECK(!LND_NodeIsSliding(pa.node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO));
    CHECK(LND_NodeGetParam(pa.node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO) == 1.5f);
    CHECK(LND_NodeGetParam(pa.node, LND_SOUNDTOUCH_PARAM_TEMPO_RATIO) == 0.75f);
    CHECK(LND_NodeSetParam(pa.node, LND_SOUNDTOUCH_PARAM_PITCH_RATIO, 1) == LND_OK);
    CHECK(read_all(&pa, a, 4000, 113, 1) == 4000);
    for (size_t i = 0; i < 4000; i++)
        CHECK(isfinite(a[i]));
    destroy(&pa);
    destroy(&pb);
    finish();
}
#endif

#if LND_MODULE_WAV_DECODER
static void put_u32(uint8_t *dst, uint32_t value) {
    for (unsigned i = 0; i < 4; i++)
        dst[i] = (uint8_t)(value >> (i * 8));
}

static void test_decoded(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    static uint8_t wav[44 + 32000];
    static float data[16000], out[32001];
    tone(data, 16000, 1);
    memcpy(wav, "RIFF", 4);
    put_u32(wav + 4, sizeof wav - 8);
    memcpy(wav + 8, "WAVEfmt ", 8);
    put_u32(wav + 16, 16);
    wav[20] = wav[22] = 1;
    put_u32(wav + 24, 16000);
    put_u32(wav + 28, 32000);
    wav[32] = 2;
    wav[34] = 16;
    memcpy(wav + 36, "data", 4);
    put_u32(wav + 40, 32000);
    for (unsigned f = 0; f < 16000; f++) {
        uint16_t value = (uint16_t)(int16_t)(data[f] * 32768);
        wav[44 + f * 2] = (uint8_t)value;
        wav[45 + f * 2] = (uint8_t)(value >> 8);
    }
    playback p = {0};
    p.source = LND_SourceCreateEncodedMemory(wav, sizeof wav, LND_ENCODED_SOURCE_LIGHTWEIGHT, &(LND_ENCODED_SOURCE_OPTIONS){.codec_name = "wav", .block_frames = 37});
    CHECK(p.source != nullptr);
    p.sound = LND_SourceEnsureSound(p.source, nullptr);
    p.node = LND_NodeCreateSoundTouch(1, 16000, &(LND_SOUNDTOUCH_CONFIG){.pitch_ratio = 2, .tempo_ratio = 0.5f});
    CHECK(p.sound && p.node && LND_SoundSetOutput(p.sound, p.node) == LND_OK);
    p.renderer = LND_RendererCreateNode(p.node);
    CHECK(p.renderer && LND_SoundPlay(p.sound) == LND_OK);
    deny_alloc = true;
    CHECK(read_all(&p, out, 32001, 127, 1) == 32000);
    CHECK(fabs(frequency(out, 32000, 1) - 874) < 20);
    CHECK(LND_NodeGetStatus(p.node) == LND_SOURCE_EOF);
    CHECK(LND_NodeFree(LND_SourceEnsureNode(p.source)) == LND_OK);
    destroy(&p);
    finish();
}
#endif

int main(void) {
    test_modes(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    test_modes(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    test_short_and_channels();
    test_stream();
    test_limits();
    test_settings();
#if LND_MODULE_WAV_DECODER
    test_decoded();
#endif
#if LND_MODULE_SLIDE
    test_slide();
#endif
    return report();
}
