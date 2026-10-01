#include "stream_test.h"
#include "lindar_autofree.h"
#include "lindar_buffers.h"
#if LND_MODULE_QUEUE
#include "lindar_queue.h"
#endif
#if LND_MODULE_FILES
#include "lindar_files.h"
#endif
#include <math.h>

typedef struct reader {
    size_t position_frames, length_frames;
    unsigned closes;
    bool wait, error;
} reader;
static int64_t read_pcm(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    reader *r = user;
    if (r->error) return LND_ERR_IO;
    if (r->wait) return 0;
    size_t n = frames < r->length_frames - r->position_frames ? frames : r->length_frames - r->position_frames;
    float data[128];
    CHECK(n <= 128);
    for (size_t i = 0; i < n; i++)
        data[i] = 0.25f;
    LND_PCM input = {.data = data, .frames = n, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_PcmConvert(pcm, offset, &input, 0, n) == LND_OK);
    r->position_frames += n;
    return n ? (int64_t)n : LND_READ_EOF;
}
static int32_t seek(void *user, uint64_t frame) {
    reader *r = user;
    r->position_frames = frame < r->length_frames ? (size_t)frame : r->length_frames;
    return LND_OK;
}
static void close_reader(void *user) { ((reader *)user)->closes++; }
static LND_SOURCE *source(reader *r, uint32_t sample_rate_hz) {
    LND_SOURCE_CONFIG config = {
        .read = read_pcm, .seek = seek, .close = close_reader, .user = r, .channels = 1, .sample_rate_hz = sample_rate_hz, .block_frames = 128, .flags = LND_SOURCE_LIVE};
    return LND_SourceCreate(&config);
}
static void fill(LND_RENDERER *r, float *data, size_t frames) {
    LND_PCM pcm = {.data = data, .frames = frames, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_RendererFillPcm(r, &pcm, 0, frames) == LND_OK);
}

static void test_lifetime(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    LND_NODE *bus = LND_NodeCreateBus(1, 16000);
    LND_RENDERER *renderer = LND_RendererCreateNode(bus);
    reader r = {.length_frames = 11, .wait = true};
    LND_SOURCE *s = source(&r, 16000);
    LND_AUTOFREE id = LND_AutofreeTakeSource(s, bus);
    CHECK(id && LND_AutofreeIsValid(id));
    CHECK(!LND_AutofreeTakeSource(s, bus) && LND_ErrorGetLast() == LND_ERR_BUSY);
    float data[32];
    deny_alloc = true;
    fill(renderer, data, 16);
    CHECK(LND_LibraryUpdate() == LND_OK && LND_AutofreeIsValid(id) && r.closes == 0);
    LND_AUTOFREE_INFO info;
    CHECK(LND_AutofreeGetInfo(id, &info) == LND_OK && info.state == LND_SOUND_STALLED && info.position_frames == 0);
    CHECK(LND_AutofreeSetPause(id, true) == LND_OK);
    r.wait = false;
    fill(renderer, data, 16);
    CHECK(r.position_frames == 0 && LND_LibraryUpdate() == LND_OK && LND_AutofreeIsValid(id));
    CHECK(LND_AutofreeSetPause(id, false) == LND_OK);
    fill(renderer, data, 16);
    CHECK(r.closes == 0 && LND_AutofreeIsValid(id));
    for (unsigned i = 0; i < 11; i++)
        CHECK(data[i] == 0.25f);
    for (unsigned i = 11; i < 16; i++)
        CHECK(data[i] == 0);
    fill(renderer, data, 16);
    CHECK(LND_AutofreeGetInfo(id, &info) == LND_OK && info.draining);
    CHECK(LND_LibraryUpdate() == LND_OK);
    CHECK(!LND_AutofreeIsValid(id) && r.closes == 1 && LND_NodeGetInputCount(bus) == 0);
    CHECK(LND_AutofreeGetInfo(id, &info) == LND_ERR_STATE);
    CHECK(LND_AutofreeSetGain(id, 1) == LND_ERR_STATE && LND_AutofreeCancel(id) == LND_ERR_STATE);
    fill(renderer, data, 16);
    for (unsigned i = 0; i < 16; i++)
        CHECK(data[i] == 0);
    deny_alloc = false;
    r = (reader){.length_frames = 3};
    id = LND_AutofreeTakeSource(source(&r, 16000), bus);
    CHECK(id != 0 && LND_AutofreeSetLoop(id, true) == LND_OK);
    fill(renderer, data, 32);
    CHECK(LND_LibraryUpdate() == LND_OK && LND_AutofreeIsValid(id) && r.closes == 0);
    CHECK(LND_AutofreeSeekFrames(id, 1) == LND_OK);
    CHECK(LND_AutofreeCancel(id) == LND_OK && r.closes == 1);
    r = (reader){.length_frames = 10};
    id = LND_AutofreeTakeSource(source(&r, 16000), bus);
    finish();
    CHECK(r.closes == 1 && !LND_AutofreeIsValid(id));
}

static void test_tail(uint32_t in_rate, uint32_t out_rate, uint32_t quality, int32_t layout) {
    begin(LND_FORMAT_F32, layout);
    CHECK(LND_ConfigSet(LND_CFG_AUDIO_RESAMPLE_QUALITY, quality) == LND_OK);
    LND_NODE *bus = LND_NodeCreateBus(1, out_rate);
    LND_NODE *reference_bus = LND_NodeCreateBus(1, out_rate);
    LND_RENDERER *renderer = LND_RendererCreateNode(bus), *reference = LND_RendererCreateNode(reference_bus);
    reader a = {.length_frames = 271}, b = a;
    LND_SOURCE *s = source(&a, in_rate), *normal = source(&b, in_rate);
    LND_SOUND *sound = LND_SourceEnsureSound(normal, nullptr);
    CHECK(LND_SoundSetOutput(sound, reference_bus) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    LND_AUTOFREE id = LND_AutofreeTakeSource(s, bus);
    CHECK(id != 0);
    float data[17], expected[17];
    deny_alloc = true;
    bool heard = false;
    for (unsigned block = 0; block < 300; block++) {
        fill(renderer, data, 17);
        fill(reference, expected, 17);
        for (unsigned i = 0; i < 17; i++) {
            CHECK(fabsf(data[i] - expected[i]) < 0.000001f);
            if (data[i] > 0.1f) heard = true;
        }
        CHECK(LND_LibraryUpdate() == LND_OK);
    }
    CHECK(heard && !LND_AutofreeIsValid(id) && a.closes == 1);
    finish();
    CHECK(b.closes == 1);
}

static void test_inputs(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    LND_NODE *bus = LND_NodeCreateBus(1, 16000);
    LND_RENDERER *renderer = LND_RendererCreateNode(bus);
    LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_F32, 1, 16000, 11);
    float *values = LND_BufferGetData(buffer);
    for (unsigned i = 0; i < 11; i++)
        values[i] = 0.25f;
    LND_AUTOFREE id = LND_AutofreeTakeSource(LND_SourceCreateBuffer(buffer), bus);
    LND_BufferFree(buffer);
    float data[128];
    fill(renderer, data, 32);
    CHECK(data[0] == 0.25f && data[10] == 0.25f && data[11] == 0);
    CHECK(LND_LibraryUpdate() == LND_OK && !LND_AutofreeIsValid(id));
#if LND_MODULE_QUEUE
    id = LND_AutofreeTakeSource(LND_SourceCreateQueue(1, 16000, 32), bus);
    CHECK(id != 0);
    LND_PCM pcm = {.data = data, .frames = 11, .channels = 1, .format = LND_FORMAT_F32};
    CHECK(LND_AutofreeWritePcm(id, &pcm, 0, 11) == 11);
    CHECK(LND_AutofreeEnd(id) == LND_OK);
    fill(renderer, data, 32);
    fill(renderer, data, 1);
    CHECK(LND_LibraryUpdate() == LND_OK && !LND_AutofreeIsValid(id));
#endif
#if LND_MODULE_WAV_DECODER && LND_MODULE_FILES
    LND_SOURCE *file = LND_SourceCreateFile("audiosamples/tone.wav", LND_ENCODED_SOURCE_LIGHTWEIGHT | LND_ENCODED_SOURCE_DIRECT, nullptr);
    CHECK(file != nullptr);
    id = LND_AutofreeTakeSource(file, bus);
    CHECK(id != 0);
    for (unsigned i = 0; i < 1000 && LND_AutofreeIsValid(id); i++) {
        fill(renderer, data, 128);
        CHECK(LND_LibraryUpdate() == LND_OK);
    }
    CHECK(!LND_AutofreeIsValid(id));
    FILE *f = fopen("audiosamples/tone.wav", "rb");
    CHECK(f != nullptr);
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);
        void *bytes = malloc((size_t)size);
        CHECK(bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size);
        fclose(f);
        file = LND_SourceCreateEncodedMemory(bytes, (size_t)size, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
        id = LND_AutofreeTakeSource(file, bus);
        CHECK(id != 0);
        CHECK(LND_AutofreeCancel(id) == LND_OK);
        free(bytes);
    }
#endif
    reader r = {.length_frames = 1, .error = true};
    id = LND_AutofreeTakeSource(source(&r, 16000), bus);
    fill(renderer, data, 16);
    CHECK(LND_LibraryUpdate() == LND_OK && !LND_AutofreeIsValid(id) && r.closes == 1);
    r = (reader){.length_frames = 1, .wait = true};
    id = LND_AutofreeTakeSource(source(&r, 16000), bus);
    CHECK(LND_NodeFree(bus) == LND_OK);
    CHECK(LND_LibraryUpdate() == LND_OK && !LND_AutofreeIsValid(id) && r.closes == 1);
    finish();
}

static void test_renderer_release(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    reader r = {.length_frames = 32};
    LND_SOURCE *input = source(&r, 32000);
    LND_SOUND *sound = LND_SourceEnsureSound(input, &(LND_SOUND_CONFIG){.sample_rate_hz = 16000});
    LND_RENDERER_CONFIG config = {.render = LND_SoundRenderPcm, .user = sound, .channels = 1, .sample_rate_hz = 16000, .block_frames = 32};
    LND_RENDERER *renderer = LND_RendererCreateProc(&config);
    CHECK(renderer);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    LND_NODE *bus = LND_NodeCreateBus(1, 16000);
    LND_AUTOFREE id = LND_AutofreeTakeSource(input, bus);
    CHECK(id && LND_AutofreeCancel(id) == LND_OK && r.closes == 1);
    finish();
}

static void test_failure(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    LND_NODE *bus = LND_NodeCreateBus(1, 32000);
    unsigned successes = 0;
    for (int i = 0; i < 40; i++) {
        reader r = {.length_frames = 10};
        LND_SOURCE *s = source(&r, 16000);
        fail_after = i;
        LND_AUTOFREE id = LND_AutofreeTakeSource(s, bus);
        fail_after = -1;
        if (id) {
            successes++;
            CHECK(LND_AutofreeCancel(id) == LND_OK);
        } else {
            CHECK(r.closes == 0);
            CHECK(LND_SourceFree(s) == LND_OK);
        }
        CHECK(r.closes == 1 && LND_NodeGetInputCount(bus) == 0);
    }
    CHECK(successes > 0 && successes < 40);
    finish();
}

int main(void) {
    test_renderer_release();
    test_failure();
    test_lifetime();
    test_tail(16000, 32000, LND_RESAMPLE_LINEAR, LND_LAYOUT_INTERLEAVED);
    test_tail(48000, 16000, LND_RESAMPLE_SINC32, LND_LAYOUT_PLANAR);
    test_tail(16000, 44100, LND_RESAMPLE_SINC16, LND_LAYOUT_INTERLEAVED);
    test_inputs();
    return report();
}
