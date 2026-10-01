#include "stream_test.h"
#include "lindar_graph.h"
#include "lindar_buffers.h"
#include <math.h>
#if LND_MODULE_DSP
#include "lindar_dsp.h"
#endif
#if LND_MODULE_QUEUE
#include "lindar_queue.h"
#endif

typedef struct generator {
    uint32_t pos, limit, calls, closed;
    int32_t error;
    bool live, end;
} generator;

static float sample(uint32_t i) { return (float)(1 + i % 31) / 128.0f; }

static int64_t generate(void *user, void *dst, uint64_t frames) {
    generator *g = user;
    g->calls++;
    if (g->error) return g->error;
    if (g->pos == g->limit) return !g->live || g->end ? LND_READ_EOF : 0;
    uint32_t count = (uint32_t)(frames < g->limit - g->pos ? frames : g->limit - g->pos);
    for (uint32_t i = 0; i < count; i++)
        ((float *)dst)[i] = sample(g->pos + i);
    g->pos += count;
    return count;
}

static void close_source(void *user) { ((generator *)user)->closed++; }

static LND_SOURCE *source(generator *g, LND_NODE *output) {
    LND_SOURCE_PROCS procs = {.read = generate, .close = close_source};
    LND_SOURCE *s = LND_SourceCreateProc(&procs, g, LND_FORMAT_F32, 1, 48000, LND_GRAPH_SOURCE_DIRECT | (g->live ? LND_SOURCE_LIVE : 0));
    CHECK(s != nullptr);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(s), output) == LND_OK);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(s, nullptr)) == LND_OK);
    return s;
}

static LND_RENDERER *branch_renderer(LND_NODE *split, uint32_t index, LND_NODE *output) {
    LND_NODE *branch = LND_NodeGetSplitterOutput(split, index);
    if (output) CHECK(LND_NodeConnect(branch, output) == LND_OK);
    CHECK(LND_SoundPlay(LND_NodeEnsureSound(branch, nullptr)) == LND_OK);
    LND_RENDERER *r = LND_RendererCreateNode(output ? output : branch);
    CHECK(r != nullptr);
    return r;
}

static void test_fanout(int32_t format, int32_t layout) {
    begin(format, layout);
    generator g = {.limit = 737};
    LND_NODE *mix = LND_NodeCreateMixer(1, 48000, LND_MIX_AVAILABLE | LND_MIX_END);
    LND_NODE *split = LND_NodeCreateSplitter(1, 48000, 3);
    CHECK(mix && split && LND_NodeConnect(mix, split) == LND_OK);
    LND_SOURCE *s = source(&g, mix);
    LND_RENDERER *r[3];
    for (uint32_t i = 0; i < 3; i++)
        r[i] = branch_renderer(split, i, nullptr);
    CHECK(LND_NodeConnect(LND_NodeGetSplitterOutput(split, 0), mix) != LND_OK && LND_ErrorGetLast() == LND_ERR_CYCLE);
    float out[737];
    LND_PCM pcm = {.data = out, .frames = 737, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(r[0], &pcm, 0, 737) == 737);
    for (unsigned i = 0; i < 737; i++)
        CHECK(out[i] == sample(i));
    CHECK(LND_RendererReadPcm(r[0], &pcm, 0, 10) == 0);
    CHECK(LND_NodeGetStatus(mix) == LND_SOURCE_EOF);
    for (unsigned i = 0; i < 737; i += 67) {
        unsigned count = 737 - i < 67 ? 737 - i : 67;
        CHECK(LND_RendererReadPcm(r[1], &pcm, i, count) == count);
    }
    for (unsigned i = 0; i < 737; i++)
        CHECK(out[i] == sample(i));
    int16_t integer[737];
    LND_PCM short_pcm = {.data = integer, .frames = 737, .channels = 1, .format = LND_FORMAT_S16};
    CHECK(LND_RendererReadPcm(r[2], &short_pcm, 0, 737) == 737);
    for (unsigned i = 0; i < 737; i++)
        CHECK(integer[i] == (int16_t)(sample(i) * 32768));
    for (unsigned i = 0; i < 3; i++) {
        CHECK(LND_RendererReadPcm(r[i], &pcm, 0, 13) == 0);
        CHECK(LND_NodeGetStatus(LND_NodeGetSplitterOutput(split, i)) == LND_SOURCE_EOF);
        CHECK(LND_NodeGetSplitDroppedFrames(LND_NodeGetSplitterOutput(split, i)) == 0);
    }
    CHECK(g.pos == 737 && LND_SourceGetPositionFrames(s) == 737);
    deny_alloc = false;
    finish();
    CHECK(g.closed == 1);
}

static void test_live(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    generator g = {.live = true};
    LND_NODE *mix = LND_NodeCreateMixer(1, 48000, LND_MIX_AVAILABLE | LND_MIX_END);
    LND_SOURCE *s = source(&g, mix);
    LND_RENDERER *r = LND_RendererCreateNode(mix);
    float out[127];
    LND_PCM pcm = {.data = out, .frames = 127, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 127) == 0);
    CHECK(LND_NodeGetStatus(mix) == LND_SOURCE_WAITING);
    g.limit = 13;
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 127) == 13);
    for (unsigned i = 0; i < 13; i++)
        CHECK(out[i] == sample(i));
    for (unsigned i = 13; i < 127; i++)
        CHECK(out[i] == 0);
    CHECK(LND_NodeGetStatus(mix) == LND_SOURCE_WAITING);
    CHECK(LND_NodeSetInputPause(mix, LND_SourceEnsureNode(s), true) == LND_OK);
    CHECK(LND_NodeGetInputPause(mix, LND_SourceEnsureNode(s)) == 1);
    g.limit = 83;
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 127) == 0 && g.pos == 13);
    CHECK(LND_NodeSetInputPause(mix, LND_SourceEnsureNode(s), false) == LND_OK);
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 127) == 70);
    for (unsigned i = 0; i < 70; i++)
        CHECK(out[i] == sample(i + 13));
    CHECK(LND_SourceEnd(s) == LND_OK);
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 127) == 0);
    CHECK(LND_NodeGetStatus(mix) == LND_SOURCE_EOF);
    finish();
    CHECK(g.closed == 1);
}

static void test_modes(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float out[17];
    LND_PCM pcm = {.data = out, .frames = 17, .channels = 1, .format = LND_FORMAT_F32};
    for (uint32_t mode = 0; mode <= LND_MIX_AVAILABLE; mode++) {
        LND_NODE *mix = LND_NodeCreateMixer(1, 48000, mode);
        LND_RENDERER *r = LND_RendererCreateNode(mix);
        CHECK(LND_RendererReadPcm(r, &pcm, 0, 17) == (mode == LND_MIX_CONTINUOUS ? 17 : 0));
        CHECK(LND_NodeGetStatus(mix) == (mode == LND_MIX_CONTINUOUS ? LND_SOURCE_READY : LND_SOURCE_WAITING));
        generator g = {.limit = 51};
        LND_SOURCE *s = source(&g, mix);
        CHECK(LND_RendererReadPcm(r, &pcm, 0, 17) == 17);
        CHECK(LND_NodeDisconnect(LND_SourceEnsureNode(s), mix) == LND_OK);
        CHECK(LND_RendererReadPcm(r, &pcm, 0, 17) == (mode == LND_MIX_CONTINUOUS ? 17 : 0));
        CHECK(g.pos == 17);
        CHECK(LND_NodeConnect(LND_SourceEnsureNode(s), mix) == LND_OK);
        CHECK(LND_RendererReadPcm(r, &pcm, 0, 17) == 17);
        for (unsigned i = 0; i < 17; i++)
            CHECK(out[i] == sample(17 + i));
        CHECK(LND_SourceFree(s) == LND_OK && g.closed == 1);
        CHECK(LND_NodeFree(mix) == LND_OK);
        CHECK(LND_RendererFree(r) == LND_OK);
    }
    finish();
}

static void test_overrun(void) {
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_MIXER_BUFFER_FRAMES, 1024) == LND_OK);
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    generator g = {.limit = 3000};
    LND_NODE *split = LND_NodeCreateSplitter(1, 48000, 2);
    source(&g, split);
    LND_RENDERER *a = branch_renderer(split, 0, nullptr), *b = branch_renderer(split, 1, nullptr);
    float out[2000];
    LND_PCM pcm = {.data = out, .frames = 2000, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(a, &pcm, 0, 2000) == 2000);
    CHECK(LND_RendererReadPcm(b, &pcm, 0, 100) == 100);
    CHECK(LND_NodeGetSplitDroppedFrames(LND_NodeGetSplitterOutput(split, 1)) == 976);
    for (unsigned i = 0; i < 100; i++)
        CHECK(out[i] == sample(976 + i));
    CHECK(LND_NodeResetSplit(LND_NodeGetSplitterOutput(split, 1)) == LND_OK);
    CHECK(LND_RendererReadPcm(b, &pcm, 0, 100) == 100);
    for (unsigned i = 0; i < 100; i++)
        CHECK(out[i] == sample(2000 + i));
    CHECK(LND_NodeGetSplitDroppedFrames(LND_NodeGetSplitterOutput(split, 1)) == 0);
    finish();
}

static void test_conversion(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_F32, 6, 44100, 441);
    float *data = LND_BufferGetData(buffer);
    for (unsigned i = 0; i < 441; i++)
        for (unsigned c = 0; c < 6; c++)
            data[i * 6 + c] = c == 2 ? 0.125f : 0;
    LND_SOURCE *s = LND_SourceCreateBuffer(buffer);
    LND_NODE *mix = LND_NodeCreateMixer(2, 48000, LND_MIX_AVAILABLE | LND_MIX_END);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(s), mix) == LND_OK);
    CHECK(LND_NodeSetGainRampFrames(LND_SourceEnsureNode(s), 0) == LND_OK);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(s, nullptr)) == LND_OK);
    LND_RENDERER *r = LND_RendererCreateNode(mix);
    float out[1024];
    LND_PCM pcm = {.data = out, .frames = 512, .channels = 2, .format = LND_FORMAT_F32};
    deny_alloc = true;
    int64_t got = LND_RendererReadPcm(r, &pcm, 0, 512);
    CHECK(got >= 479 && got <= 481);
    CHECK(out[100] > 0.01f && out[100] == out[101]);
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 512) == 0);
    CHECK(LND_NodeGetStatus(mix) == LND_SOURCE_EOF);
    deny_alloc = false;
    LND_BufferFree(buffer);
    finish();
}

#if LND_MODULE_QUEUE
static void test_lockstep(void) {
    begin(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    LND_SOURCE *a = LND_SourceCreateQueue(1, 48000, 64), *b = LND_SourceCreateQueue(1, 48000, 64);
    LND_NODE *mix = LND_NodeCreateMixer(1, 48000, LND_MIX_LOCKSTEP | LND_MIX_END);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(a), mix) == LND_OK);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(b), mix) == LND_OK);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(a, nullptr)) == LND_OK);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(b, nullptr)) == LND_OK);
    LND_RENDERER *r = LND_RendererCreateNode(mix);
    int16_t samples[31];
    for (unsigned i = 0; i < 31; i++)
        samples[i] = 100 + i;
    LND_PCM pcm = {.data = samples, .frames = 31, .channels = 1, .format = LND_FORMAT_S16};
    CHECK(LND_QueueWritePcm(a, &pcm, 0, 31) == 31);
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 31) == 0);
    CHECK(LND_QueueGetBufferedFrames(a) == 31);
    for (unsigned i = 0; i < 31; i++)
        samples[i] = 200 + i;
    CHECK(LND_QueueWritePcm(b, &pcm, 0, 13) == 13);
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 31) == 13);
    for (unsigned i = 0; i < 13; i++)
        CHECK(samples[i] == 300 + 2 * i);
    CHECK(LND_QueueGetBufferedFrames(a) == 18 && LND_QueueGetBufferedFrames(b) == 0);
    CHECK(LND_SourceEnd(b) == LND_OK && LND_SourceEnd(a) == LND_OK);
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 31) == 18);
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 31) == 0);
    CHECK(LND_NodeGetStatus(mix) == LND_SOURCE_EOF);
    finish();
}
#endif

static void test_wrap_eof(int32_t layout) {
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_MIXER_BUFFER_FRAMES, 1024) == LND_OK);
    begin(LND_FORMAT_F32, layout);
    generator g = {.limit = 1031};
    LND_NODE *split = LND_NodeCreateSplitter(1, 48000, 2);
    source(&g, split);
    LND_RENDERER *a = branch_renderer(split, 0, nullptr), *b = branch_renderer(split, 1, nullptr);
    float out[1100];
    LND_PCM pcm = {.data = out, .frames = 1100, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_RendererReadPcm(b, &pcm, 0, 20) == 20);
    CHECK(LND_RendererReadPcm(a, &pcm, 0, 1100) == 1031);
    CHECK(LND_RendererReadPcm(a, &pcm, 0, 100) == 0);
    CHECK(LND_RendererReadPcm(b, &pcm, 0, 1100) == 1011);
    for (unsigned i = 0; i < 1011; i++)
        CHECK(out[i] == sample(i + 20));
    CHECK(LND_NodeGetSplitDroppedFrames(LND_NodeGetSplitterOutput(split, 1)) == 0);
    finish();
}

static void test_clock(int32_t format, int32_t layout, bool reverse) {
    begin(format, layout);
    generator clock = {.live = true}, music = {.limit = 1000, .live = true};
    LND_NODE *mix = LND_NodeCreateMixer(1, 48000, LND_MIX_AVAILABLE);
    LND_SOURCE *a = nullptr, *b = nullptr;
    if (reverse) b = source(&music, mix);
    a = source(&clock, mix);
    if (!reverse) b = source(&music, mix);
    CHECK(LND_NodeSetMixerClock(mix, LND_SourceEnsureNode(a)) == LND_OK);
    CHECK(LND_NodeGetMixerClock(mix) == LND_SourceEnsureNode(a));
    LND_RENDERER *r = LND_RendererCreateNode(mix);
    float out[37];
    LND_PCM pcm = {.data = out, .frames = 37, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 37) == 0 && music.pos == 0);
    clock.limit = 13;
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 37) == 13 && music.pos == 13);
    for (unsigned i = 0; i < 13; i++)
        CHECK(out[i] == 2 * sample(i));
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 37) == 0 && music.pos == 13);
    CHECK(LND_SourceEnd(a) == LND_OK);
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 37) == 37 && music.pos == 50);
    CHECK(LND_NodeDisconnect(LND_SourceEnsureNode(a), mix) == LND_OK);
    CHECK(LND_NodeGetMixerClock(mix) == nullptr);
    finish();
    CHECK(clock.closed == 1 && music.closed == 1);
}

static void test_matrix(int32_t format, int32_t layout) {
    begin(format, layout);
    float data[] = {0.25f, -0.5f, 0.125f, -0.25f};
    LND_PCM input = {.data = data, .frames = 2, .channels = 2, .format = LND_FORMAT_F32};
    LND_SOURCE_CONFIG config = {.pcm = &input, .channels = 2, .sample_rate_hz = 48000};
    LND_SOURCE *s = LND_SourceCreate(&config);
    LND_NODE *mix = LND_NodeCreateMixer(2, 48000, LND_MIX_AVAILABLE | LND_MIX_END);
    LND_NODE *node = LND_SourceEnsureNode(s);
    CHECK(LND_NodeConnect(node, mix) == LND_OK);
    float matrix[] = {0, 1, 1, 0};
    CHECK(LND_NodeSetInputMatrix(mix, node, matrix) == LND_OK);
    LND_RENDERER *r = LND_RendererCreateNode(mix);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(s, nullptr)) == LND_OK);
    float out[4];
    LND_PCM pcm = {.data = out, .frames = 2, .channels = 2, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 2) == 2);
    CHECK(out[0] == -0.5f && out[1] == 0.25f && out[2] == -0.25f && out[3] == 0.125f);
    CHECK(LND_NodeSetInputMatrix(mix, node, nullptr) == LND_OK);
    CHECK(LND_SourceSeekFrames(s, 0) == LND_OK);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(s, nullptr)) == LND_OK);
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 2) == 2 && memcmp(out, data, sizeof data) == 0);
    finish();
}

static void test_seek(bool native, uint32_t quality) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    CHECK(LND_ConfigSet(LND_CFG_AUDIO_RESAMPLE_QUALITY, quality) == LND_OK);
    float data[1000];
    for (unsigned i = 0; i < 1000; i++)
        data[i] = i < 500 ? 0.25f : -0.5f;
    LND_PCM pcm = {.data = data, .frames = 1000, .channels = 1, .format = LND_FORMAT_F32};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 24000};
    LND_BUFFER *buffer = native ? nullptr : LND_BufferCreate(LND_FORMAT_F32, 1, 24000, 1000);
    if (buffer) memcpy(LND_BufferGetData(buffer), data, sizeof data);
    LND_SOURCE *s = native ? LND_SourceCreate(&config) : LND_SourceCreateBuffer(buffer);
    LND_NODE *mix = LND_NodeCreateMixer(1, 48000, LND_MIX_AVAILABLE | LND_MIX_END);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(s), mix) == LND_OK);
    LND_SOUND *sound = LND_SourceEnsureSound(s, nullptr);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    LND_RENDERER *r = LND_RendererCreateNode(mix);
    float out[64];
    LND_PCM output = {.data = out, .frames = 64, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(r, &output, 0, 64) == 64);
    CHECK(LND_SoundSeekFrames(sound, 750) == LND_OK);
    CHECK(LND_RendererReadPcm(r, &output, 0, 64) == 64);
    CHECK(fabsf(out[32] + 0.5f) < 0.0001f);
    for (unsigned i = 0; i < 20 && LND_NodeGetStatus(mix) != LND_SOURCE_EOF; i++)
        CHECK(LND_RendererReadPcm(r, &output, 0, 64) >= 0);
    CHECK(LND_NodeGetStatus(mix) == LND_SOURCE_EOF);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_RendererReadPcm(r, &output, 0, 64) == 64);
    CHECK(fabsf(out[32] - 0.25f) < 0.0001f);
    deny_alloc = false;
    if (buffer) LND_BufferFree(buffer);
    finish();
}

static void test_conference(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    generator remote = {.limit = 256}, music = {.limit = 256};
    LND_NODE *channel = LND_NodeCreateSplitter(1, 48000, 2), *stream = LND_NodeCreateSplitter(1, 48000, 2);
    LND_SOURCE *a = source(&remote, channel), *b = source(&music, stream);
    LND_NODE *listen = LND_NodeCreateMixer(1, 48000, LND_MIX_AVAILABLE);
    LND_NODE *upload = LND_NodeCreateMixer(1, 48000, LND_MIX_AVAILABLE);
    LND_NODE *saver = LND_NodeCreateMixer(1, 48000, LND_MIX_AVAILABLE);
    LND_NODE *local_music = LND_NodeGetSplitterOutput(stream, 0);
    CHECK(LND_NodeSetGain(local_music, 0.5f) == LND_OK);
    CHECK(LND_NodeConnect(LND_NodeGetSplitterOutput(channel, 0), listen) == LND_OK);
    CHECK(LND_NodeConnect(local_music, listen) == LND_OK);
    CHECK(LND_NodeConnect(LND_NodeGetSplitterOutput(stream, 1), upload) == LND_OK);
    CHECK(LND_NodeConnect(LND_NodeGetSplitterOutput(channel, 1), saver) == LND_OK);
    CHECK(LND_NodeConnect(upload, saver) == LND_OK);
    for (unsigned i = 0; i < 2; i++) {
        CHECK(LND_SoundPlay(LND_NodeEnsureSound(LND_NodeGetSplitterOutput(channel, i), nullptr)) == LND_OK);
        CHECK(LND_SoundPlay(LND_NodeEnsureSound(LND_NodeGetSplitterOutput(stream, i), nullptr)) == LND_OK);
    }
    LND_RENDERER *listeners = LND_RendererCreateNode(listen), *uploads = LND_RendererCreateNode(upload), *saves = LND_RendererCreateNode(saver);
    float out[64];
    LND_PCM pcm = {.data = out, .frames = 64, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    for (unsigned block = 0; block < 4; block++) {
        CHECK(LND_RendererReadPcm(listeners, &pcm, 0, 64) == 64);
        for (unsigned i = 0; i < 64; i++)
            CHECK(out[i] == 1.5f * sample(64 * block + i));
        CHECK(LND_RendererReadPcm(uploads, &pcm, 0, 64) == 64);
        for (unsigned i = 0; i < 64; i++)
            CHECK(out[i] == sample(64 * block + i));
        CHECK(LND_RendererReadPcm(saves, &pcm, 0, 64) == 64);
        for (unsigned i = 0; i < 64; i++)
            CHECK(out[i] == 2 * sample(64 * block + i));
        CHECK(LND_SourceGetPositionFrames(a) == (block + 1) * 64 && LND_SourceGetPositionFrames(b) == (block + 1) * 64);
    }
    finish();
}
static void test_error(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    generator g = {.error = LND_ERR_IO, .limit = 4};
    LND_NODE *mix = LND_NodeCreateMixer(1, 48000, LND_MIX_AVAILABLE | LND_MIX_END);
    source(&g, mix);
    LND_RENDERER *r = LND_RendererCreateNode(mix);
    float data[8];
    LND_PCM pcm = {.data = data, .frames = 8, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 8) == LND_ERR_IO);
    CHECK(LND_NodeGetStatus(mix) == LND_ERR_IO);
    g.error = 0;
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 8) == 4);
    for (unsigned i = 0; i < 4; i++)
        CHECK(data[i] == sample(i));
    finish();
}

#if LND_MODULE_DSP
static void test_panned_split(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    generator g = {.live = true};
    LND_NODE *split = LND_NodeCreateSplitter(1, 48000, 2);
    LND_SOURCE *s = source(&g, split);
    LND_NODE *pan = LND_NodeCreatePanner(48000, -1, LND_PAN_MONO);
    LND_NODE *mix = LND_NodeCreateMixer(2, 48000, LND_MIX_AVAILABLE | LND_MIX_END);
    CHECK(LND_NodeConnect(pan, mix) == LND_OK);
    LND_NODE *left = LND_NodeGetSplitterOutput(split, 0);
    CHECK(LND_NodeConnect(left, pan) == LND_OK);
    CHECK(LND_SoundPlay(LND_NodeEnsureSound(left, nullptr)) == LND_OK);
    LND_RENDERER *local = LND_RendererCreateNode(mix), *upload = branch_renderer(split, 1, nullptr);
    float data[64];
    LND_PCM pcm = {.data = data, .frames = 32, .channels = 2, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(local, &pcm, 0, 32) == 0);
    CHECK(LND_NodeGetStatus(mix) == LND_SOURCE_WAITING);
    g.limit = 13;
    CHECK(LND_RendererReadPcm(local, &pcm, 0, 32) == 13);
    for (unsigned i = 0; i < 13; i++)
        CHECK(data[i * 2] == sample(i) && data[i * 2 + 1] == 0);
    CHECK(LND_SourceEnd(s) == LND_OK);
    CHECK(LND_RendererReadPcm(local, &pcm, 0, 32) == 0 && LND_NodeGetStatus(mix) == LND_SOURCE_EOF);
    pcm.channels = 1;
    CHECK(LND_RendererReadPcm(upload, &pcm, 0, 32) == 13);
    for (unsigned i = 0; i < 13; i++)
        CHECK(data[i] == sample(i));
    finish();
}
#endif

int main(void) {
#if LND_MODULE_DSP
    test_panned_split();
#endif
    test_error();
    test_conference();
    test_clock(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED, false);
    test_clock(LND_FORMAT_S16, LND_LAYOUT_PLANAR, true);
    test_matrix(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    test_matrix(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    test_seek(false, LND_RESAMPLE_LINEAR);
    test_seek(true, LND_RESAMPLE_SINC32);
    test_wrap_eof(LND_LAYOUT_INTERLEAVED);
    test_wrap_eof(LND_LAYOUT_PLANAR);
    test_fanout(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    test_fanout(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    test_fanout(LND_FORMAT_U8, LND_LAYOUT_INTERLEAVED);
    test_fanout(LND_FORMAT_S32, LND_LAYOUT_PLANAR);
    test_live();
    test_modes();
    test_overrun();
    test_conversion();
#if LND_MODULE_QUEUE
    test_lockstep();
#endif
    return report();
}
