#include "stream_test.h"
#include "lindar_pcm_float.h"
#include <math.h>

typedef struct feed {
    uint64_t position;
    uint64_t length;
    unsigned calls;
    bool fail;
    bool ramp;
} feed;

static int64_t read_float(void *user, void *dst, uint64_t frames) {
    feed *f = user;
    f->calls++;
    if (f->fail && f->calls > 1) return LND_ERR_IO;
    if (f->position == f->length) return LND_READ_EOF;
    uint64_t count = frames < f->length - f->position ? frames : f->length - f->position;
    for (uint64_t i = 0; i < count; i++) ((float *)dst)[i] = f->ramp ? (float)(f->position + i) / 2048 : 0.5f;
    f->position += count;
    return (int64_t)count;
}

static int64_t read_pcm(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    float samples[4];
    CHECK(frames <= 4);
    int64_t got = read_float(user, samples, frames);
    if (got > 0) {
        LND_PCM input = {.data = samples, .frames = (size_t)got, .channels = 1, .format = LND_FORMAT_F32};
        CHECK(LND_PcmConvert(pcm, offset, &input, 0, (size_t)got) == LND_OK);
    }
    return got;
}

static int32_t seek(void *user, uint64_t frame) {
    feed *f = user;
    f->position = frame;
    f->calls = 0;
    return LND_OK;
}

static void test_identity(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float samples[128] = {0};
    LND_PCM pcm = {.data = samples, .frames = 128, .channels = 1, .format = LND_FORMAT_F32};
    LND_SOURCE_CONFIG input = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 48000, .block_frames = 16};
    LND_SOURCE *source = LND_SourceCreate(&input);
    LND_SOUND_CONFIG config = {.sample_rate_hz = 24000, .flags = LND_SOUND_RESAMPLE_LINEAR};
    LND_SOUND *sound = LND_SourceEnsureSound(source, &config);
    CHECK(source && sound);
    if (!source || !sound) { finish(); return; }
    LND_NODE *node = LND_SourceGetNode(source);
    CHECK(node && LND_SourceGetSound(source) == sound && LND_SoundGetSource(sound) == source);
    CHECK(LND_SourceEnsureSound(source, nullptr) == sound && LND_SourceEnsureSound(source, &config) == sound);
    CHECK(LND_NodeGetSound(node) == sound && LND_NodeEnsureSound(node, nullptr) == sound && LND_NodeEnsureSound(node, &config) == sound);
    CHECK(LND_SoundGetSampleRateHz(sound) == 24000 && LND_SoundGetLengthFrames(sound) == 64);
    CHECK(!LND_SourceEnsureSound(source, &(LND_SOUND_CONFIG){0}) && LND_ErrorGetLast() == LND_ERR_STATE);
    CHECK(!LND_NodeEnsureSound(node, &(LND_SOUND_CONFIG){.sample_rate_hz = 16000}) && LND_ErrorGetLast() == LND_ERR_STATE);
    config.sample_rate_hz = 16000;
    CHECK(LND_SoundSetConfig(sound, &config) == LND_OK);
    CHECK(LND_SourceGetSound(source) == sound && LND_NodeGetSound(node) == sound && LND_SoundGetSampleRateHz(sound) == 16000);
    CHECK(LND_SoundSetConfig(sound, nullptr) == LND_OK && LND_SoundGetSampleRateHz(sound) == 48000);
    CHECK(LND_SourceEnsureSound(source, nullptr) == sound && LND_NodeEnsureSound(node, nullptr) == sound);
    CHECK(LND_NodeFree(node) == LND_OK && LND_SourceGetSound(source) == sound);
    CHECK(LND_SourceFree(source) == LND_OK);
    source = LND_SourceCreate(&input);
    unsigned before = allocations;
    sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(sound && allocations == before + 1 && !LND_SourceGetNode(source));
    CHECK(!LND_SourceEnsureSound(source, &config) && LND_ErrorGetLast() == LND_ERR_STATE);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundSetGain(sound, 0.5f) == LND_OK);
    CHECK(LND_SoundSetConfig(sound, &config) == LND_OK);
    CHECK(LND_SourceGetSound(source) == sound && LND_SoundGetLoop(sound) && LND_SoundGetGain(sound) == 0.5f);
    CHECK(LND_NodeFree(LND_SoundGetNode(sound)) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    finish();
}

static void test_render(bool native, bool convert, int32_t format, int32_t layout) {
    begin(format, layout);
    float samples[1024];
    for (unsigned i = 0; i < 1024; i++) samples[i] = 0.5f;
    LND_PCM input = {.data = samples, .frames = 1024, .channels = 1, .format = LND_FORMAT_F32};
    feed f = {.length = 1024};
    LND_SOURCE_CONFIG config = {.pcm = &input, .channels = 1, .sample_rate_hz = 48000, .block_frames = 64};
    LND_SOURCE_PROCS procs = {.read = read_float, .seek = seek, .length_frames = 1024};
    LND_SOURCE *source = native ? LND_SourceCreate(&config) : LND_SourceCreateProc(&procs, &f, LND_FORMAT_F32, 1, 48000, LND_GRAPH_SOURCE_DIRECT);
    float matrix[2] = {1, 0.5f};
    LND_SOUND_CONFIG sc = {.sample_rate_hz = 24000, .channels = 2, .channel_matrix = matrix, .flags = LND_SOUND_RESAMPLE_LINEAR};
    LND_SOUND *sound = LND_SourceEnsureSound(source, convert ? &sc : nullptr);
    CHECK(source && sound);
    if (!source || !sound) { finish(); return; }
    uint32_t channels = convert ? 2 : 1, rate = convert ? 24000 : 48000;
    CHECK(LND_SoundSetGain(sound, 0.5f) == LND_OK);
    LND_RENDERER_CONFIG rc = {.render = LND_SoundRenderPcm, .user = sound, .channels = channels, .sample_rate_hz = rate, .block_frames = 16};
    LND_RENDERER *renderer = LND_RendererCreateProc(&rc);
    CHECK(renderer);
    if (!renderer) { finish(); return; }
    unsigned char data[32 * 2 * sizeof(double)];
    memset(data, 0x5a, sizeof data);
    void *planes[2] = {data, data + 32 * sizeof(double)};
    LND_PCM output = {.data = data, .planes = planes, .frames = 32, .channels = channels, .format = format, .layout = layout};
    CHECK(LND_SoundRenderPcm(sound, &output, 2, 0) == 0);
    CHECK(LND_RendererReadPcm(renderer, &output, 2, 16) == 0 && LND_SoundGetPositionFrames(sound) == 0);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(renderer, &output, 2, 16) == 16);
    float readback[32];
    LND_PCM verify = {.data = readback, .frames = 16, .channels = channels, .format = LND_FORMAT_F32};
    CHECK(LND_PcmConvert(&verify, 0, &output, 2, 16) == LND_OK);
    for (unsigned i = 0; i < 16; i++) {
        CHECK(fabsf(readback[i * channels] - 0.25f) < 0.0001f);
        if (channels == 2) CHECK(fabsf(readback[i * channels + 1] - 0.125f) < 0.0001f);
    }
    CHECK(LND_SoundGetPositionFrames(sound) == 16);
    CHECK(LND_SoundSetPause(sound, true) == LND_OK);
    uint64_t at = LND_SoundGetPositionFrames(sound);
    CHECK(LND_RendererReadPcm(renderer, &output, 2, 16) == 0 && LND_SoundGetPositionFrames(sound) == at);
    CHECK(LND_SoundSetPause(sound, false) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &output, 2, 16) == 16);
    uint64_t total = 32, expected = convert ? 512 : 1024;
    for (unsigned i = 0; i < 100; i++) {
        int64_t got = LND_RendererReadPcm(renderer, &output, 2, 16);
        CHECK(got >= 0);
        if (got <= 0) break;
        total += (uint64_t)got;
    }
    CHECK(total == expected);
    CHECK(LND_SoundGetPositionFrames(sound) == expected);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_STOPPED);
    deny_alloc = false;
    CHECK(LND_SourceFree(source) == LND_ERR_BUSY);
    if (convert || !native) CHECK(LND_SoundSetConfig(sound, &(LND_SOUND_CONFIG){.sample_rate_hz = 8000}) == LND_ERR_BUSY);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    CHECK(LND_SoundStop(sound) == LND_OK);
    CHECK(LND_SoundReadF32(sound, readback, 8) == 8);
    CHECK(readback[0] == 0.5f);
    if (native && LND_SoundGetNode(sound)) CHECK(LND_NodeFree(LND_SoundGetNode(sound)) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    finish();
}

static void test_read_render_position(bool native) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    float samples[256];
    for (unsigned i = 0; i < 256; i++) samples[i] = (float)i / 2048;
    LND_PCM input = {.data = samples, .frames = 256, .channels = 1, .format = LND_FORMAT_F32};
    LND_SOURCE_CONFIG config = {.pcm = &input, .channels = 1, .sample_rate_hz = 48000, .block_frames = 32};
    feed f = {.length = 256, .ramp = true};
    LND_SOURCE_PROCS procs = {.read = read_float, .seek = seek, .length_frames = 256};
    LND_SOURCE *source = native ? LND_SourceCreate(&config) : LND_SourceCreateProc(&procs, &f, LND_FORMAT_F32, 1, 48000, LND_GRAPH_SOURCE_DIRECT);
    LND_SOUND *sound = LND_SourceEnsureSound(source, &(LND_SOUND_CONFIG){.sample_rate_hz = 24000, .flags = LND_SOUND_RESAMPLE_LINEAR});
    LND_RENDERER_CONFIG rc = {.render = LND_SoundRenderPcm, .user = sound, .channels = 1, .sample_rate_hz = 24000, .block_frames = 8};
    LND_RENDERER *renderer = LND_RendererCreateProc(&rc);
    CHECK(source && sound && renderer);
    if (!renderer) { finish(); return; }
    float data[8];
    LND_PCM output = {.data = data, .frames = 8, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &output, 0, 8) == 8);
    for (unsigned i = 0; i < 8; i++) CHECK(data[i] == (float)(2 * i) / 2048);
    CHECK(LND_SoundReadF32(sound, data, 8) == 8);
    for (unsigned i = 0; i < 8; i++) CHECK(data[i] == (float)(16 + 2 * i) / 2048);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &output, 0, 8) == 8);
    for (unsigned i = 0; i < 8; i++) CHECK(data[i] == (float)(32 + 2 * i) / 2048);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_PLAYING);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_SoundSeekFrames(sound, 3) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &output, 0, 8) == 8);
    for (unsigned i = 0; i < 8; i++) CHECK(data[i] == (float)(6 + 2 * i) / 2048);
    CHECK(LND_SoundSetPause(sound, true) == LND_OK);
    CHECK(LND_SoundSeekFrames(sound, 5) == LND_OK);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_PAUSED && LND_RendererReadPcm(renderer, &output, 0, 8) == 0);
    CHECK(LND_SoundSetPause(sound, false) == LND_OK);
    CHECK(LND_RendererReadPcm(renderer, &output, 0, 8) == 8);
    for (unsigned i = 0; i < 8; i++) CHECK(data[i] == (float)(10 + 2 * i) / 2048);
    CHECK(LND_SoundSetPause(sound, true) == LND_OK && LND_SoundStop(sound) == LND_OK);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_STOPPED);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    finish();
}

static void test_partial_reads(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    feed f = {.length = 64, .fail = true};
    LND_SOURCE_CONFIG config = {.read = read_pcm, .seek = seek, .user = &f, .channels = 1, .sample_rate_hz = 48000, .block_frames = 4, .flags = LND_SOURCE_LIVE};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    float data[16] = {0};
    LND_PCM pcm = {.data = data, .frames = 16, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_SourceRead(nullptr, data, LND_FORMAT_F32, 1) == LND_ERR_INVALID_ARG);
    CHECK(LND_SoundReadF32(nullptr, data, 1) == LND_ERR_INVALID_ARG);
    CHECK(LND_SourceRead(source, data, LND_FORMAT_F32, 12) == 4);
    CHECK(LND_SourceGetStatus(source) == LND_ERR_IO && data[0] == 0.5f);
    f.fail = false;
    CHECK(LND_SourceRead(source, data, LND_FORMAT_F32, 4) == LND_ERR_IO);
    CHECK(LND_SourceRead(source, data, LND_FORMAT_F32, 4) == 4);
    CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
    f.fail = true;
    CHECK(LND_SoundReadF32(sound, data, 12) == 4);
    CHECK(LND_SoundReadF32(sound, data, 4) == LND_ERR_IO);
    CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
    LND_RENDERER_CONFIG rc = {.render = read_pcm, .user = &f, .channels = 1, .sample_rate_hz = 48000, .block_frames = 4};
    LND_RENDERER *renderer = LND_RendererCreateProc(&rc);
    CHECK(renderer && LND_RendererReadPcm(renderer, &pcm, 0, 12) == 4);
    CHECK(LND_RendererReadPcm(renderer, &pcm, 0, 4) == LND_ERR_IO);
    CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 12) == LND_ERR_IO);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    finish();
}

static void test_adapter_recreate(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_PLANAR);
    float samples[32] = {0};
    LND_PCM pcm = {.data = samples, .frames = 32, .channels = 1, .format = LND_FORMAT_F32};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &pcm, .channels = 1, .sample_rate_hz = 48000});
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(source && sound);
    bool success = false;
    for (int fault = 0; fault < 32 && !success; fault++) {
        fail_after = fault;
        LND_NODE *node = LND_SourceEnsureNode(source);
        fail_after = -1;
        success = node != nullptr;
        CHECK(LND_SourceGetNode(source) == node && LND_SoundGetNode(sound) == node);
        if (node) CHECK(LND_NodeFree(node) == LND_OK);
        CHECK(!LND_SourceGetNode(source) && !LND_SoundGetNode(sound));
        node = LND_SourceEnsureNode(source);
        CHECK(node && LND_SourceGetNode(source) == node && LND_SoundGetNode(sound) == node);
        CHECK(LND_NodeFree(node) == LND_OK);
        CHECK(!LND_SourceGetNode(source) && LND_SourceGetSound(source) == sound);
    }
    CHECK(success && LND_SourceFree(source) == LND_OK);
    finish();
}

static void test_scratch_reconfigure(int32_t layout) {
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_MIX_BLOCK_FRAMES, 16) == LND_OK);
    begin(LND_FORMAT_F32, layout);
    feed f = {.length = 16384};
    LND_SOURCE_PROCS procs = {.read = read_float, .seek = seek, .length_frames = 16384};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &f, LND_FORMAT_F32, 1, 48000, LND_GRAPH_SOURCE_DIRECT);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    float matrix[] = {0.5f, 0.25f};
    const LND_SOUND_CONFIG configs[] = {{0}, {.channels = 2}, {.channel_matrix = matrix},
        {.channels = 2, .channel_matrix = matrix, .sample_rate_hz = 24000, .flags = LND_SOUND_RESAMPLE_LINEAR}, {0}};
    float data[4102];
    CHECK(source && sound);
    if (!source || !sound) { finish(); return; }
    for (unsigned config = 0; config < 5; config++) {
        CHECK(LND_SoundStop(sound) == LND_OK);
        CHECK(LND_SoundSetConfig(sound, &configs[config]) == LND_OK);
        CHECK(LND_SoundSeekFrames(sound, 0) == LND_OK);
        uint32_t channels = LND_SoundGetChannels(sound);
        float expected = configs[config].channel_matrix ? 0.25f : 0.5f;
        CHECK(LND_SoundReadF32(sound, data, 2049) == 2049);
        for (unsigned i = 0; i < 2049; i++) {
            CHECK(data[i * channels] == expected);
            if (channels == 2) CHECK(data[i * channels + 1] == (configs[config].channel_matrix ? 0.125f : 0.5f));
        }
        CHECK(LND_SoundSeekFrames(sound, 0) == LND_OK && LND_SoundPlay(sound) == LND_OK);
        LND_RENDERER_CONFIG rc = {.render = LND_SoundRenderPcm, .user = sound, .channels = channels,
            .sample_rate_hz = LND_SoundGetSampleRateHz(sound), .block_frames = 31};
        LND_RENDERER *renderer = LND_RendererCreateProc(&rc);
        CHECK(renderer);
        if (!renderer) { finish(); return; }
        for (unsigned i = 0; i < 4102; i++) data[i] = -7;
        LND_PCM pcm = {.data = data, .frames = 2051, .channels = channels, .format = LND_FORMAT_F32};
        deny_alloc = true;
        CHECK(LND_RendererReadPcm(renderer, &pcm, 1, 2049) == 2049);
        deny_alloc = false;
        for (unsigned c = 0; c < channels; c++) {
            CHECK(data[c] == -7 && data[2050 * channels + c] == -7);
            for (unsigned i = 1; i <= 2049; i++) CHECK(data[i * channels + c] == (c && configs[config].channel_matrix ? 0.125f : expected));
        }
        CHECK(LND_RendererFree(renderer) == LND_OK);
    }
    CHECK(LND_SourceFree(source) == LND_OK);
    finish();
}

int main(void) {
    test_identity();
    test_adapter_recreate();
    test_scratch_reconfigure(LND_LAYOUT_INTERLEAVED);
    test_scratch_reconfigure(LND_LAYOUT_PLANAR);
    for (unsigned native = 0; native < 2; native++)
        for (unsigned convert = 0; convert < 2; convert++)
            for (int32_t format = LND_FORMAT_S16; format <= LND_FORMAT_F64; format++)
                for (int32_t layout = LND_LAYOUT_INTERLEAVED; layout <= LND_LAYOUT_PLANAR; layout++)
                    test_render(native, convert, format, layout);
    test_read_render_position(false);
    test_read_render_position(true);
    test_partial_reads();
    return report();
}
