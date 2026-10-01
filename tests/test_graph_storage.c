#include "stream_test.h"
#include "playback/graph/node.h"
#include "playback/graph/native.h"
#include "playback/graph/context.h"
#include "playback/graph/render.h"
#include "src/pcm.h"
#include <math.h>

static void test_commands(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    LND_NODE *node = LND_NodeCreateBus(2, 48000);
    CHECK(node && !node->ring.items);
    fail_after = 0;
    CHECK(LND_NodeSetGain(node, 0.5f) == LND_ERR_OUT_OF_MEMORY);
    CHECK(!node->ring.items && LND_NodeGetGain(node) == 1);
    fail_after = -1;
    CHECK(LND_NodeSetGain(node, 0.5f) == LND_OK && node->ring.items);
    LND_RENDERER *renderer = LND_RendererCreateNode(node);
    CHECK(renderer);
    float samples[64];
    LND_PCM pcm = {.data = samples, .frames = 32, .channels = 2, .format = LND_FORMAT_F32};
    unsigned count = allocations;
    deny_alloc = true;
    for (unsigned i = 0; i < 32; i++) {
        CHECK(LND_NodeSetGain(node, (float)i / 32) == LND_OK);
        CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 32) == LND_OK);
        CHECK(LND_NodeGetGain(node) == (float)i / 32);
    }
    CHECK(count == allocations);
    finish();
}

static int64_t empty_read(void *user, void *data, uint64_t frames) { return LND_READ_EOF; }
static int32_t reset_seek(void *user, uint64_t frame) {
    *(uint64_t *)user = frame;
    return LND_OK;
}

static void test_source_reset(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    uint64_t position = 9;
    LND_SOURCE_PROCS procs = {.read = empty_read, .seek = reset_seek, .length_frames = 16};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &position, LND_FORMAT_F32, 1, 48000, LND_GRAPH_SOURCE_DIRECT);
    LND_NODE *node = LND_SourceEnsureNode(source);
    CHECK(node && node->type == LND_NODE_SOURCE && !node->ring.items);
    CHECK(lnd_context_enter());
    unsigned count = allocations;
    deny_alloc = true;
    lnd_source_node_reset(node);
    CHECK(position == 0 && !node->ring.items && allocations == count && lnd_source_node_state(node) == LND_SOUND_STOPPED);
    deny_alloc = false;
    CHECK(lnd_node_post(node, LND_OP_START, 0, 0, 0) == LND_OK);
    position = 7;
    deny_alloc = true;
    lnd_source_node_reset(node);
    CHECK(position == 0 && !lnd_load(&node->active) && !lnd_ring_count(&node->ring));
    CHECK(lnd_source_node_state(node) == LND_SOUND_STOPPED);
    lnd_context_unlock();
    finish();
}

static void channels(void *user, float *pcm, uint32_t frames, uint32_t count, uint32_t rate) {
    (void)user;
    (void)rate;
    for (uint32_t f = 0; f < frames; f++)
        for (uint32_t c = 0; c < count; c++) pcm[f * count + c] = (float)(c + 1) / 32;
}

static void test_buffers(int32_t format, int32_t layout) {
    begin(format, layout);
    LND_NODE *source = LND_NodeCreateProcessorProc(channels, nullptr, 32, 48000, 0);
    LND_NODE *bus = LND_NodeCreateBus(1, 48000);
    CHECK(source && bus && bus->input_channels == 1 && !bus->scratch_map);
    bool connected = false;
    for (int fault = 0; fault < 16 && !connected; fault++) {
        fail_after = fault;
        int32_t result = LND_NodeConnect(source, bus);
        fail_after = -1;
        connected = result == LND_OK;
        CHECK(connected || result == LND_ERR_OUT_OF_MEMORY);
        CHECK(bus->inputs_count == (connected ? 1u : 0u));
    }
    CHECK(connected && bus->input_channels == 32);
    float matrix[32] = {0};
    matrix[31] = 0.5f;
    CHECK(LND_NodeSetInputMatrix(bus, source, matrix) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(bus);
    CHECK(renderer);
    float samples[67];
    LND_PCM pcm = {.data = samples, .frames = 67, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    for (unsigned i = 0; i < 67; i++) samples[i] = 9;
    CHECK(LND_RendererFillPcm(renderer, &pcm, 2, 64) == LND_OK);
    CHECK(samples[0] == 9 && samples[1] == 9 && samples[66] == 9);
    for (unsigned i = 2; i < 66; i++) CHECK(fabsf(samples[i] - 0.5f) < 0.0001f);
    deny_alloc = false;
    CHECK(lnd_context_enter());
    fail_after = 0;
    CHECK(lnd_node_reconfigure(source, 16, 48000) == LND_ERR_OUT_OF_MEMORY);
    fail_after = -1;
    CHECK(source->channels == 32);
    CHECK(lnd_node_reconfigure(source, 16, 48000) == LND_OK);
    CHECK(lnd_node_reconfigure(source, 32, 48000) == LND_OK);
    lnd_context_unlock();
    CHECK(LND_NodeSetInputMatrix(bus, source, matrix) == LND_OK);
    CHECK(LND_RendererFillPcm(renderer, &pcm, 2, 64) == LND_OK);
    for (unsigned i = 2; i < 66; i++) CHECK(fabsf(samples[i] - 0.5f) < 0.0001f);
    finish();
}

static void test_single(int32_t format, int32_t layout) {
    begin(format, layout);
    uint8_t samples[19 * 2 * 8], silence[19 * 2 * 8];
    LND_PCM input = {.data = samples, .frames = 19, .channels = 2, .format = format};
    const double values[] = {-0.0, 0, 0.125, -0.5, 1, -1, INFINITY, -INFINITY, NAN};
    for (uint32_t f = 0; f < 19; f++)
        for (uint32_t c = 0; c < 2; c++)
            lnd_pcm_store_sample(lnd_pcm_at(&input, c, f), format, values[(f + c) % (sizeof values / sizeof *values)]);
    LND_SOURCE_CONFIG config = {.pcm = &input, .channels = 2, .sample_rate_hz = 48000};
    LND_SOURCE *source = LND_SourceCreate(&config);
    input.data = silence;
    CHECK(LND_PcmSilence(&input, 0, 19) == LND_OK);
    LND_SOURCE *zero = LND_SourceCreate(&config);
    LND_NODE *single = LND_NodeCreateBus(2, 48000), *multiple = LND_NodeCreateBus(2, 48000);
    CHECK(source && zero && single && multiple);
    LND_NODE *node = LND_SourceEnsureNode(source);
    CHECK(LND_NodeConnect(node, single) == LND_OK);
    CHECK(LND_NodeConnect(node, multiple) == LND_OK);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(zero), multiple) == LND_OK);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(source, nullptr)) == LND_OK);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(zero, nullptr)) == LND_OK);
    LND_RENDERER *a = LND_RendererCreateNode(single), *b = LND_RendererCreateNode(multiple);
    CHECK(a && b);
    uint8_t left[1024], right[1024];
    memset(left, 0xa5, sizeof left);
    memset(right, 0xa5, sizeof right);
    void *ap[] = {left + 1, left + 513}, *bp[] = {right + 1, right + 513};
    LND_PCM x = {.data = left + 1, .planes = ap, .frames = 29, .channels = 2, .format = format, .layout = layout,
                 .stride_bytes = LND_PcmGetSampleBytes(format) * (layout ? 1 : 2) + 1};
    LND_PCM y = x;
    y.data = right + 1;
    y.planes = bp;
    deny_alloc = true;
    CHECK(LND_RendererReadPcm(a, &x, 3, 24) == 24);
    CHECK(LND_RendererReadPcm(b, &y, 3, 24) == 24);
    CHECK(!memcmp(left, right, sizeof left));
    finish();
}

static void test_sinc(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    CHECK(LND_ConfigSet(LND_CFG_AUDIO_RESAMPLE_QUALITY, 3) == LND_OK);
    LND_NODE *source = LND_NodeCreateBus(2, 48000);
    LND_NODE *a = LND_NodeCreateBus(2, 44100), *b = LND_NodeCreateBus(2, 44100);
    CHECK(source && a && b);
    CHECK(lnd_node_ensure_ring(source) == LND_OK);
    CHECK(LND_NodeConnect(source, a) == LND_OK);
    unsigned count = allocations;
    CHECK(LND_NodeConnect(source, b) == LND_OK);
    unsigned shared = allocations - count;
    CHECK(LND_NodeFree(a) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(b);
    float data[128];
    LND_PCM pcm = {.data = data, .frames = 64, .channels = 2, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(renderer && LND_RendererFillPcm(renderer, &pcm, 0, 64) == LND_OK);
    for (unsigned i = 0; i < 128; i++) CHECK(data[i] == 0);
    deny_alloc = false;
    CHECK(LND_NodeDisconnect(source, b) == LND_OK);
    count = allocations;
    CHECK(LND_NodeConnect(source, b) == LND_OK);
    CHECK(allocations - count == shared + 1);
    finish();
}

#if LND_THREADS
typedef struct ring_reader {
    lnd_ring *ring;
    bool matched;
    uint64_t timestamp;
} ring_reader;

static void consume(void *user) {
    ring_reader *reader = user;
    reader->matched = true;
    reader->timestamp = lnd_render_begin();
    uint64_t nested = lnd_render_begin();
    if (nested != reader->timestamp || lnd_render_depth != 2) reader->matched = false;
    lnd_render_end();
    lnd_render_end();
    if (lnd_render_depth) reader->matched = false;
    for (uint64_t i = 0; i < 65536; i++) {
        lnd_cmd command;
        while (!lnd_ring_pop(reader->ring, &command)) {}
        if (command.u64 != i) reader->matched = false;
    }
}

static void test_publication(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    lnd_ring ring;
    CHECK(lnd_ring_init(&ring, 64) == LND_OK && !ring.items);
    ring_reader reader = {.ring = &ring};
    lnd_thread thread = {0};
    CHECK(!lnd_render_depth);
    uint64_t timestamp = lnd_render_begin();
    lnd_sleep_ms(2);
    CHECK(lnd_thread_create(&thread, consume, &reader) == LND_OK);
    for (uint64_t i = 0; i < 65536; i++) {
        lnd_cmd command = {.u64 = i};
        int32_t result;
        do { result = lnd_ring_push(&ring, &command); } while (result == LND_ERR_BUSY);
        CHECK(result == LND_OK);
    }
    lnd_thread_join(&thread);
    CHECK(reader.matched && reader.timestamp > timestamp);
    CHECK(lnd_render_depth == 1 && lnd_render_timestamp == timestamp);
    lnd_render_end();
    CHECK(!lnd_render_depth && !lnd_ring_count(&ring));
    lnd_ring_free(&ring);
    finish();
}
#endif

int main(void) {
    test_commands();
    test_source_reset();
    for (int layout = 0; layout < 2; layout++) {
        test_buffers(LND_FORMAT_F32, layout);
        test_buffers(LND_FORMAT_S16, layout);
        for (int format = LND_FORMAT_U8; format <= LND_FORMAT_F64; format++) test_single(format, layout);
    }
    test_sinc();
#if LND_THREADS
    CHECK(!lnd_render_depth);
    test_publication();
#endif
    return report();
}
