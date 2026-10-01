#include "stream_test.h"
#if LND_MODULE_GRAPH
#include "lindar_graph.h"
#include "lindar_pcm_float.h"
#endif

typedef struct producer {
    uint32_t available, position, reads, closes;
    int64_t result;
    LND_SOURCE *source;
} producer;

static int64_t read_pcm(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    producer *p = user;
    p->reads++;
    if (p->source) CHECK(LND_SourceEnd(p->source) == LND_ERR_BUSY);
    if (p->result) return p->result;
    size_t count = frames < p->available ? frames : p->available;
    int16_t data[32];
    for (size_t i = 0; i < count; i++)
        data[i] = (int16_t)(1000 + p->position + i);
    LND_PCM input = {.data = data, .frames = count, .channels = 1, .format = LND_FORMAT_S16};
    CHECK(LND_PcmConvert(pcm, offset, &input, 0, count) == LND_OK);
    p->available -= (uint32_t)count;
    p->position += (uint32_t)count;
    return (int64_t)count;
}
static int32_t seek(void *user, uint64_t frame) {
    ((producer *)user)->position = (uint32_t)frame;
    return LND_OK;
}
static void close_source(void *user) { ((producer *)user)->closes++; }

static void test_native(bool live, bool explicit_end) {
    begin(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    producer p = {0};
    LND_SOURCE_CONFIG config = {.read = read_pcm,
                                .seek = seek,
                                .close = close_source,
                                .user = &p,
                                .channels = 1,
                                .sample_rate_hz = 16000,
                                .block_frames = 7,
                                .flags = live ? LND_SOURCE_LIVE : 0};
    p.source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(p.source, nullptr);
    LND_RENDERER_CONFIG rc = {.render = LND_SoundRenderPcm, .user = sound, .channels = 1, .sample_rate_hz = 16000, .block_frames = 7};
    LND_RENDERER *renderer = LND_RendererCreateProc(&rc);
    CHECK(p.source && sound && renderer);
    int16_t data[11];
    LND_PCM pcm = {.data = data, .frames = 11, .channels = 1, .format = LND_FORMAT_S16};
    deny_alloc = true;
    CHECK(LND_SourceGetStatus(p.source) == LND_SOURCE_READY);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 0) == LND_OK && p.reads == 0);
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 11) == LND_OK);
    for (unsigned i = 0; i < 11; i++)
        CHECK(data[i] == 0);
    CHECK(LND_SoundGetPositionFrames(sound) == 0);
    CHECK(LND_SourceGetStatus(p.source) == (live ? LND_SOURCE_WAITING : LND_SOURCE_EOF));
    CHECK(LND_SoundGetState(sound) == (live ? LND_SOUND_STALLED : LND_SOUND_STOPPED));
    if (!live) {
        p.available = 3;
        CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 11) == LND_OK && p.available == 3);
        CHECK(LND_SoundPlay(sound) == LND_OK);
    } else {
        CHECK(LND_SoundSetPause(sound, true) == LND_OK);
        p.available = 3;
        CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 11) == LND_OK && p.available == 3);
        CHECK(LND_SoundPlay(sound) == LND_OK);
    }
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 11) == LND_OK);
    for (unsigned i = 0; i < 3; i++)
        CHECK(data[i] == 1000 + i);
    for (unsigned i = 3; i < 11; i++)
        CHECK(data[i] == 0);
    CHECK(LND_SourceGetPositionFrames(p.source) == 3);
    if (live) {
        p.available = 11;
        CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 11) == LND_OK);
        CHECK(LND_SoundGetState(sound) == LND_SOUND_PLAYING);
        CHECK(LND_SourceGetPositionFrames(p.source) == 14 && data[10] == 1013);
        p.available = 4;
        if (explicit_end) CHECK(LND_SourceEnd(p.source) == LND_OK);
        CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 4) == LND_OK);
        if (!explicit_end) p.result = LND_READ_EOF;
        CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 11) == LND_OK);
        CHECK(LND_SourceGetStatus(p.source) == LND_SOURCE_EOF);
        CHECK(LND_SoundGetState(sound) == LND_SOUND_STOPPED);
        CHECK(LND_SourceGetPositionFrames(p.source) == 18);
        unsigned reads = p.reads;
        CHECK(LND_SourceReadPcm(p.source, &pcm, 0, 1) == 0 && p.reads == reads);
        CHECK(LND_SourceSeekFrames(p.source, 0) == LND_OK && LND_SourceGetStatus(p.source) == LND_SOURCE_READY);
        p.result = LND_ERR_IO;
        CHECK(LND_SourceReadPcm(p.source, &pcm, 0, 1) == LND_ERR_IO);
        CHECK(LND_SourceGetStatus(p.source) == LND_ERR_IO);
        p.result = 0;
        p.available = 1;
        CHECK(LND_SourceReadPcm(p.source, &pcm, 0, 1) == 1 && LND_SourceGetStatus(p.source) == LND_SOURCE_READY);
    }
    CHECK(LND_RendererFree(renderer) == LND_OK);
    CHECK(LND_SourceFree(p.source) == LND_OK && p.closes == 1);
    finish();
}

#if LND_MODULE_GRAPH
static int64_t read_graph(void *user, void *dst, uint64_t frames) {
    producer *p = user;
    p->reads++;
    if (p->result) return p->result;
    uint64_t count = frames < p->available ? frames : p->available;
    float *data = dst;
    for (uint64_t i = 0; i < count; i++)
        data[i] = 0.25f;
    p->available -= (uint32_t)count;
    p->position += (uint32_t)count;
    return (int64_t)count;
}
static void test_graph(uint32_t sample_rate_hz, bool end) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    producer p = {0};
    LND_SOURCE_PROCS procs = {.read = read_graph, .seek = seek, .close = close_source};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &p, LND_FORMAT_F32, 1, 16000, LND_SOURCE_LIVE | LND_GRAPH_SOURCE_DIRECT);
    LND_SOUND *sound = LND_SourceEnsureSound(source, &(LND_SOUND_CONFIG){.channels = 1, .sample_rate_hz = sample_rate_hz, .flags = LND_SOUND_RESAMPLE_LINEAR});
    CHECK(source && sound);
    float data[16];
    CHECK(LND_SoundReadF32(sound, data, 16) == 0);
    CHECK(LND_SourceGetStatus(source) == LND_SOURCE_WAITING);
    p.available = 64;
    CHECK(LND_SoundReadF32(sound, data, 16) == 16);
    for (unsigned i = 0; i < 16; i++)
        CHECK(data[i] == 0.25f);
    if (end)
        CHECK(LND_SourceEnd(source) == LND_OK);
    else
        p.result = LND_READ_EOF;
    for (unsigned i = 0; i < 32 && LND_SoundReadF32(sound, data, 16); i++) {
    }
    CHECK(LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
    p.result = LND_ERR_IO;
    LND_PCM error_pcm = {.data = data, .frames = 16, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_SourceReadPcm(source, &error_pcm, 0, 1) == LND_ERR_IO);
    p.result = 0;
    p.available = 1;
    CHECK(LND_SourceReadPcm(source, &error_pcm, 0, 1) == 1 && LND_SourceGetStatus(source) == LND_SOURCE_READY);
    CHECK(LND_SourceFree(source) == LND_OK && p.closes == 1);
    finish();

    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    p = (producer){0};
    source = LND_SourceCreateProc(&procs, &p, LND_FORMAT_F32, 1, 16000, LND_SOURCE_LIVE | LND_GRAPH_SOURCE_DIRECT);
    sound = LND_SourceEnsureSound(source, nullptr);
    LND_NODE *bus = LND_NodeCreateBus(1, sample_rate_hz);
    CHECK(LND_SoundSetOutput(sound, bus) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(bus);
    LND_PCM pcm = {.data = data, .frames = 16, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 16) == LND_OK);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_STALLED);
    p.available = 4096;
    bool heard = false;
    for (unsigned i = 0; i < 100; i++) {
        CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 16) == LND_OK);
        for (unsigned j = 0; j < 16; j++)
            if (data[j] > 0.1f) heard = true;
    }
    CHECK(heard && LND_SoundGetState(sound) == LND_SOUND_PLAYING);
    p.result = LND_READ_EOF;
    for (unsigned i = 0; i < 100; i++)
        CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 16) == LND_OK);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_STOPPED && LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    finish();
    CHECK(p.closes == 1);
}
#endif

int main(void) {
    test_native(false, false);
    test_native(true, false);
    test_native(true, true);
#if LND_MODULE_GRAPH
    test_graph(16000, false);
    test_graph(32000, true);
#endif
    return report();
}
