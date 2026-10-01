#include "lindar_files.h"
#include "codec_test.h"
#include "lindar_buffers.h"
#include "lindar_dsp.h"
#include "lindar_pcm_float.h"
#include "lindar_slide.h"
#include "pcm/buffers/buffer.h"
#include "playback/graph/context.h"
#include "playback/graph/node.h"
#include "playback/graph/walk.h"
#include "playback/graph/gain.h"
#include "playback/graph/native.h"
#include "src/pcm.h"
#include "src/config.h"
#include <math.h>
#if LND_MODULE_SINK
#include "lindar_sink.h"
#endif
#include "delay_reference.h"

static void test_terminal(void) {
    static const lnd_node_vt terminal_vt = {0};
    CHECK(lnd_context_enter());
    LND_NODE *terminal = lnd_node_alloc(sizeof *terminal, &terminal_vt, LND_NODE_TERMINAL, 1, 48000);
    lnd_context_unlock();
    CHECK(terminal && LND_NodeGetType(terminal) == LND_NODE_TERMINAL);
    LND_NODE *bus = LND_NodeCreateBus(1, 48000);
    CHECK(bus && LND_NodeConnect(bus, terminal) == LND_OK);
    CHECK(LND_NodeConnect(terminal, bus) == LND_ERR_INVALID_ARG);
#if LND_MODULE_SINK
    CHECK(!LND_NodeGetSinkOutput(terminal));
#endif
    CHECK(LND_NodeFree(bus) == LND_OK);
    CHECK(LND_NodeFree(terminal) == LND_OK);
}

static void test_reuse(void) {
    for (unsigned shared = 0; shared < 2; shared++) {
        LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_U8, 1, 8000, 8);
        CHECK(buffer != nullptr);
        if (!buffer) continue;
        memset(LND_BufferGetData(buffer), 41, 8);
        if (shared) lnd_buffer_ref(buffer);
        void *data = LND_BufferGetData(buffer);
        fail_allocation = 0;
        CHECK(LND_BufferReuse(buffer, LND_FORMAT_F32, 2, 48000, 4096) == nullptr);
        fail_allocation = -1;
        CHECK(LND_BufferGetData(buffer) == data && LND_BufferGetFrames(buffer) == 8);
        CHECK(LND_BufferGetFormat(buffer) == LND_FORMAT_U8 && *(uint8_t *)data == 41);
        if (shared) LND_BufferFree(buffer);
        LND_BufferFree(buffer);
    }
}

static const char *path = "lindar-audit-output.wav";
static void keep_file(void) {
    FILE *file = fopen(path, "wb");
    CHECK(file != nullptr);
    if (file) {
        CHECK(fwrite("KEEP", 1, 4, file) == 4);
        CHECK(fclose(file) == 0);
    }
}
static void check_keep(void) {
    size_t bytes = 0;
    uint8_t *data = read_file(path, &bytes);
    CHECK(data && bytes == 4 && !memcmp(data, "KEEP", 4));
    free(data);
}
static LND_ENCODER_PARAMS wav = {.encoder_name = "wav", .channels = 1, .sample_rate_hz = 48000, .format = LND_FORMAT_S16};
static void test_atomic_file(void) {
    LND_ENCODER_PARAMS invalid = wav;
    invalid.encoder_name = "not-an-encoder";
    CHECK(!LND_OutputCreateFile(path, &invalid));
    check_keep();
    bool created = false;
    for (int fault = 0; fault < 64 && !created; fault++) {
        fail_allocation = fault;
        LND_OUTPUT *output = LND_OutputCreateFile(path, &wav);
        fail_allocation = -1;
        check_keep();
        if (!output) continue;
        created = true;
        float samples[8] = {0};
        CHECK(LND_OutputWrite(output, samples, LND_FORMAT_F32, 8) == LND_OK);
        check_keep();
        CHECK(LND_OutputFinish(output) == LND_OK);
        CHECK(LND_OutputFinish(output) == LND_OK);
        LND_OUTPUT_INFO info;
        CHECK(LND_OutputGetInfo(output, &info) == LND_OK && info.finished && info.frames == 8 && info.bytes > 16);
        CHECK(LND_OutputWrite(output, samples, LND_FORMAT_F32, 1) == LND_ERR_STATE);
        CHECK(LND_OutputFree(output) == LND_OK);
    }
    CHECK(created);
    CHECK(remove(path) == 0);
}

typedef struct boundary {
    size_t bytes;
    unsigned closes;
    int32_t flush_error;
    int32_t close_error;
} boundary;
static size_t boundary_write(void *user, const void *data, size_t bytes) {
    boundary *b = user;
    b->bytes += bytes;
    return bytes;
}
static int32_t boundary_seek(void *user, uint64_t at) { return LND_OK; }
static int32_t boundary_flush(void *user) { return ((boundary *)user)->flush_error; }
static int32_t boundary_close(void *user) {
    boundary *b = user;
    b->closes++;
    return b->close_error;
}
static void test_output_boundary(void) {
    LND_IO_OUTPUT_PROCS procs = {.write = boundary_write, .seek = boundary_seek, .flush = boundary_flush, .close = boundary_close};
    boundary b = {0};
    fail_allocation = 0;
    CHECK(!LND_OutputCreateProc(&procs, &b, &wav));
    fail_allocation = -1;
    CHECK(!b.closes);
    for (unsigned error = 0; error < 2; error++) {
        b = (boundary){0};
        LND_OUTPUT *output = LND_OutputCreateProc(&procs, &b, &wav);
        CHECK(output != nullptr);
        if (!output) continue;
        if (error) b.close_error = LND_ERR_IO;
        else b.flush_error = LND_ERR_IO;
        CHECK(LND_OutputFinish(output) == LND_ERR_IO);
        CHECK(LND_OutputFinish(output) == LND_ERR_IO && b.closes == 1);
        CHECK(LND_OutputFree(output) == LND_ERR_IO && b.closes == 1);
    }
    b = (boundary){0};
    LND_OUTPUT *output = LND_OutputCreateProc(&procs, &b, &wav);
    int16_t samples[8] = {0};
    LND_PCM pcm = {.data = samples, .frames = 8, .channels = 1, .format = LND_FORMAT_S16};
    LND_SOURCE_CONFIG config = {.pcm = &pcm, .channels = 1, .sample_rate_hz = 32000};
    LND_SOURCE *source = LND_SourceCreate(&config);
    CHECK(output && source);
    CHECK(LND_OutputWriteSource(output, source, 4) == LND_ERR_FORMAT);
    CHECK(LND_SourceGetPositionFrames(source) == 0);
    LND_SourceFree(source);
    LND_OutputFree(output);
    uint8_t bad[32] = {0};
    test_input input = {.data = bad, .size = sizeof bad};
    LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "wav"};
    CHECK(!LND_SourceCreateEncodedInput(&input_procs, &input, sizeof bad, 0, &options));
    CHECK(!input.closes);
    LND_IO *io = LND_IoCreateInput(&input_procs, &input, sizeof bad);
    CHECK(io != nullptr);
    CHECK(LND_IoFree(io) == LND_OK && input.closes == 1);
}

static void test_topology(void) {
    LND_NODE *source = LND_NodeCreateBus(1, 32000);
    LND_NODE *node = LND_NodeCreateBus(1, 48000);
    LND_NODE *dest = LND_NodeCreateBus(3, 48000);
    CHECK(source && node && dest);
    CHECK(LND_NodeConnect(source, node) == LND_OK && LND_NodeConnect(node, dest) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(node);
    CHECK(renderer != nullptr);
    bool committed = false;
    for (int fault = 0; fault < 100 && !committed; fault++) {
        CHECK(lnd_context_enter());
        void *old_native = node->native, *old_map = node->scratch_map;
        lnd_source *old_resampler = node->inputs->resampler;
        fail_allocation = fault;
        int32_t result = lnd_node_reconfigure(node, 2, 44100);
        fail_allocation = -1;
        lnd_context_unlock();
        if (result == LND_OK) {
            committed = true;
            CHECK(node->channels == 2 && node->sample_rate_hz == 44100);
        } else {
            CHECK(result == LND_ERR_OUT_OF_MEMORY);
            CHECK(node->channels == 1 && node->sample_rate_hz == 48000 && node->native == old_native && node->scratch_map == old_map);
            CHECK(node->inputs->resampler == old_resampler && node->outputs->dst == dest);
            int16_t data[8];
            LND_PCM pcm = {.data = data, .frames = 8, .channels = 1, .format = LND_FORMAT_S16};
            CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 8) == LND_OK);
        }
    }
    CHECK(committed);
    LND_RendererFree(renderer);
    LND_NODE *other = LND_NodeCreateBus(2, 22050);
    CHECK(other != nullptr);
    CHECK(lnd_context_enter());
    fail_allocation = 0;
    CHECK(lnd_node_set_output(node, other) == LND_ERR_OUT_OF_MEMORY);
    fail_allocation = -1;
    CHECK(node->outputs_count == 1 && node->outputs->dst == dest);
    lnd_context_unlock();
    LND_NodeFree(other);
    LND_NodeFree(source);
    LND_NodeFree(node);
    LND_NodeFree(dest);
}

static void test_dag(void) {
    LND_NODE *nodes[61];
    nodes[0] = LND_NodeCreateBus(1, 48000);
    for (unsigned i = 1; i < 61; i++) {
        nodes[i] = LND_NodeCreateBus(1, 48000);
        CHECK(nodes[i] != nullptr);
        unsigned previous = i > 2 ? ((i - 1) / 2 - 1) * 2 + 1 : 0;
        CHECK(LND_NodeConnect(nodes[previous], nodes[i]) == LND_OK);
        if (previous) CHECK(LND_NodeConnect(nodes[previous + 1], nodes[i]) == LND_OK);
    }
    CHECK(lnd_context_enter());
    lnd_walk walk = lnd_walk_begin(nodes[0], false, true);
    unsigned count = 0;
    while (lnd_walk_next(&walk)) count++;
    CHECK(count == 61);
    walk = lnd_walk_begin(nodes[60], true, false);
    count = 0;
    while (lnd_walk_next(&walk)) count++;
    CHECK(count == 60);
    CHECK(lnd_node_post(nodes[0], LND_OP_GAIN, 0, 0, 0.5f) == LND_OK);
    CHECK(lnd_graph_ctx.pending_count == 1);
    lnd_nodes_maintain(true, 1);
    CHECK(!lnd_graph_ctx.pending_count && nodes[0]->gain == 0.5f);
    lnd_context_unlock();
    for (unsigned i = 61; i-- > 0;) LND_NodeFree(nodes[i]);
}

typedef struct seek_count {
    unsigned calls;
    uint64_t position;
} seek_count;
static int32_t counted_seek(void *user, uint64_t frame) {
    seek_count *state = user;
    state->calls++;
    state->position = frame;
    return LND_OK;
}
static int64_t counted_read(void *user, void *dst, uint64_t frames) { return LND_READ_EOF; }
static void test_diamond_seek(void) {
    seek_count state = {0};
    LND_SOURCE_PROCS procs = {.read = counted_read, .seek = counted_seek, .length_frames = 1000};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &state, LND_FORMAT_S16, 1, 48000, 0);
    LND_NODE *start = LND_SourceEnsureNode(source);
    LND_NODE *left = LND_NodeCreateBus(1, 48000), *right = LND_NodeCreateBus(1, 48000), *end = LND_NodeCreateBus(1, 48000);
    CHECK(source && start && left && right && end);
    CHECK(LND_NodeConnect(start, left) == LND_OK && LND_NodeConnect(start, right) == LND_OK);
    CHECK(LND_NodeConnect(left, end) == LND_OK && LND_NodeConnect(right, end) == LND_OK);
    LND_SOUND *sound = LND_NodeEnsureSound(end, &(LND_SOUND_CONFIG){.channels = 1, .sample_rate_hz = 48000});
    CHECK(sound != nullptr);
    state.calls = 0;
    CHECK(LND_SoundSeekFrames(sound, 321) == LND_OK);
    CHECK(LND_LibraryUpdate() == LND_OK);
    CHECK(state.calls == 1 && state.position == 321);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(LND_NodeFree(left) == LND_OK);
    CHECK(LND_NodeFree(right) == LND_OK);
    CHECK(LND_NodeFree(end) == LND_OK);
}

static void test_dsp_validation(void) {
    LND_NODE *biquad = LND_NodeCreateBiquad(1, 48000, &(LND_BIQUAD_CONFIG){.type = LND_BIQUAD_LOWPASS, .frequency_hz = 1000, .q = 0.707f, .gain_db = 0});
    LND_NODE *delay = LND_NodeCreateDelay(1, 48000, &(LND_DELAY_CONFIG){.max_delay_ms = 100, .delay_ms = 20, .feedback = 0.5f, .mix = 0.5f});
    LND_NODE *panner = LND_NodeCreatePanner(48000, 0, LND_PAN_STEREO);
    LND_NODE *meter = LND_NodeCreateMeter(1, 48000, 100);
    LND_SLIDE_CONFIG slide = {.duration_frames = 100};
    CHECK(biquad && delay && panner && meter);
    CHECK(LND_NodeSetParam(biquad, LND_DSP_PARAM_Q, -1) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSlideParam(biquad, LND_DSP_PARAM_Q, NAN, &slide) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSlideParam(biquad, LND_DSP_PARAM_TYPE, LND_BIQUAD_HIGHPASS, &slide) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(biquad, LND_DSP_PARAM_PAN, 0) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(delay, LND_DSP_PARAM_DELAY_MS, 101) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSlideParam(delay, LND_DSP_PARAM_FEEDBACK, 1, &slide) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSlideParam(panner, LND_DSP_PARAM_PAN_MODE, LND_PAN_STEREO, &slide) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(meter, LND_DSP_PARAM_DECAY_MS, -1) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(delay, LND_DSP_PARAM_FEEDBACK, 0.995f) == LND_OK);
    LND_NodeFree(biquad);
    LND_NodeFree(delay);
    LND_NodeFree(panner);
    LND_NodeFree(meter);
}

typedef struct input_status {
    int64_t result;
    unsigned reads;
} input_status;

static size_t read_error(void *user, uint64_t offset, void *data, size_t bytes) {
    ((input_status *)user)->reads++;
    return LND_IO_READ_ERROR;
}
static int64_t read_state(void *user, void *data, size_t bytes) {
    input_status *s = user;
    s->reads++;
    if (s->result > 0 && (uint64_t)s->result <= bytes) memset(data, 0, (size_t)s->result);
    return s->result;
}
static void test_io_status(void) {
    uint8_t data[8];
    input_status state = {0};
    LND_IO_INPUT_PROCS random = {.read_at = read_error};
    LND_IO *io = LND_IoCreateInput(&random, &state, sizeof data);
    CHECK(LND_IoReadSome(io, data, 0) == 0 && !state.reads);
    CHECK(LND_IoReadSome(io, data, sizeof data) == LND_ERR_IO);
    CHECK(LND_IoGetStatus(io) == LND_ERR_IO && LND_IoGetPositionBytes(io) == 0);
    CHECK(LND_IoRead(io, data, sizeof data) == LND_ERR_IO && LND_ErrorGetLast() == LND_ERR_IO);
    LND_IoFree(io);
    LND_IO_STREAM_INPUT_PROCS stream = {.read = read_state};
    io = LND_IoCreateStream(&stream, &state, LND_IO_SIZE_UNKNOWN);
    CHECK(LND_IoReadSome(io, data, sizeof data) == 0 && LND_IoGetStatus(io) == LND_SOURCE_WAITING);
    state.result = 3;
    CHECK(LND_IoRead(io, data, sizeof data) == 3 && LND_IoGetStatus(io) == LND_SOURCE_READY);
    state.result = LND_READ_EOF;
    CHECK(LND_IoReadSome(io, data, sizeof data) == 0 && LND_IoGetStatus(io) == LND_SOURCE_EOF);
    unsigned reads = state.reads;
    CHECK(LND_IoRead(io, data, sizeof data) == 0 && state.reads == reads);
    LND_IoFree(io);
    state.result = 9;
    io = LND_IoCreateStream(&stream, &state, LND_IO_SIZE_UNKNOWN);
    CHECK(LND_IoReadSome(io, data, sizeof data) == LND_ERR_IO && LND_IoGetPositionBytes(io) == 0);
    LND_IoFree(io);
}

static size_t partial_io_read(void *user, uint64_t offset, void *dst, size_t size) {
    if (offset >= 3 && *(bool *)user) return LND_IO_READ_ERROR;
    size_t count = size < 3 ? size : 3;
    memset(dst, 0x5a, count);
    return count;
}

static void test_partial_io(void) {
    bool fail = true;
    LND_IO_INPUT_PROCS procs = {.read_at = partial_io_read};
    LND_IO *io = LND_IoCreateInput(&procs, &fail, 8);
    unsigned char data[8] = {0};
    CHECK(io && LND_IoRead(io, data, sizeof data) == 3);
    CHECK(LND_IoGetPositionBytes(io) == 3 && LND_IoGetStatus(io) == LND_ERR_IO);
    CHECK(data[0] == 0x5a && data[2] == 0x5a && data[3] == 0);
    CHECK(LND_IoRead(io, data, sizeof data) == LND_ERR_IO);
    CHECK(LND_IoRead(nullptr, data, sizeof data) == LND_ERR_INVALID_ARG);
    fail = false;
    CHECK(LND_IoSeekBytes(io, 0) == LND_OK);
    CHECK(LND_IoRead(io, data, sizeof data) == 8);
    CHECK(LND_IoRead(io, data, sizeof data) == 0 && LND_IoGetStatus(io) == LND_SOURCE_EOF);
    CHECK(LND_IoFree(io) == LND_OK);
}

static int64_t empty_read(void *user, const LND_PCM *pcm, size_t offset, size_t frames) { return LND_READ_EOF; }
static int32_t save_position(void *user, uint64_t frame) {
    *(uint64_t *)user = frame;
    return LND_OK;
}

static void test_views_and_units(void) {
    uint64_t position = 0;
    LND_SOURCE_CONFIG config = {.read = empty_read, .seek = save_position, .user = &position, .channels = 1, .sample_rate_hz = 48000};
    LND_SOURCE *source = LND_SourceCreate(&config);
    CHECK(source != nullptr);
    LND_SOURCE_INFO info;
    unsigned before = allocations;
    CHECK(LND_SourceGetInfo(source, &info) == LND_OK && info.length_kind == LND_LENGTH_UNKNOWN);
    CHECK(!LND_SourceGetSound(source) && !LND_SourceGetNode(source) && before == allocations);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(sound && LND_SourceGetSound(source) == sound);
    before = allocations;
    CHECK(!LND_SoundGetNode(sound) && before == allocations);
    uint64_t large = (1ull << 54) + 123;
    CHECK(LND_SoundSeekFrames(sound, large) == LND_OK);
    CHECK(position == large && LND_SoundGetPositionFrames(sound) == large);
    uint64_t frames = 48000ull * 86400 * 500 + 123;
    CHECK(LND_SoundSeekFrames(sound, frames) == LND_OK);
    CHECK(LND_SoundSeekSeconds(sound, LND_SoundGetPositionSeconds(sound)) == LND_OK && position == frames);
    CHECK(LND_SoundSeekSeconds(sound, INFINITY) == LND_ERR_INVALID_ARG);
    LND_SourceFree(source);
    config.length_known = true;
    source = LND_SourceCreate(&config);
    CHECK(LND_SourceGetInfo(source, &info) == LND_OK && info.length_kind == LND_LENGTH_EXACT && info.length_frames == 0);
    test_output empty_output = {0};
    LND_OUTPUT *output = LND_OutputCreateProc(&output_procs, &empty_output, &wav);
    CHECK(output != nullptr);
    fail_allocation = 0;
    CHECK(LND_OutputWriteSource(output, source, 0) == 0);
    fail_allocation = -1;
    CHECK(LND_OutputFree(output) == LND_OK);
    LND_SourceFree(source);
    LND_NODE *node = LND_NodeCreateBus(1, 48000);
    before = allocations;
    CHECK(!LND_NodeGetSound(node) && !LND_NodeGetSource(node) && before == allocations);
    sound = LND_NodeEnsureSound(node, nullptr);
    CHECK(sound && LND_NodeGetSound(node) == sound);
    CHECK(!LND_NodeEnsureSound(node, &(LND_SOUND_CONFIG){.sample_rate_hz = 24000}) && LND_ErrorGetLast() == LND_ERR_STATE);
    CHECK(LND_SoundGetSampleRateHz(sound) == 48000);
    CHECK(LND_SoundSetConfig(sound, &(LND_SOUND_CONFIG){.sample_rate_hz = 24000}) == LND_OK);
    CHECK(LND_NodeGetSound(node) == sound && LND_SoundGetSampleRateHz(sound) == 24000);
    LND_SOURCE *view = LND_NodeEnsureSource(node);
    CHECK(view && LND_SourceGetInfo(view, &info) == LND_OK && info.length_kind == LND_LENGTH_UNKNOWN);
    LND_NodeFree(node);
}

static void test_delay_wrap(void) {
    for (uint32_t channels = 1; channels <= 6; channels += channels == 1 ? 1 : 4)
        for (unsigned capacity = 0; capacity < 3; capacity++)
            for (unsigned mode = 0; mode < 3; mode++) {
                float maximum = capacity == 0 ? 0.125f : capacity == 1 ? 1.25f : 100.0f;
                float delay = mode == 0 ? 0 : mode == 1 ? maximum : maximum * 0.371f;
                LND_NODE *node = LND_NodeCreateDelay(channels, 8000, &(LND_DELAY_CONFIG){.max_delay_ms = maximum, .delay_ms = delay, .feedback = 0.75f, .mix = 0.6f});
                CHECK(node);
                if (!node) continue;
                const LND_PROCESSOR_PROCS *procs = LND_NodeGetProcessorProcs(node);
                void *user = LND_NodeGetProcessorUser(node);
                delay_reference reference = delay_reference_create(channels, 8000, maximum, delay, 0.75f, 0.6f);
                CHECK(reference.ring);
                float actual[257 * 6], expected[257 * 6];
                for (unsigned pass = 0; pass < 80; pass++) {
                    if (pass == 40) {
                        float next = maximum - delay;
                        procs->param(user, LND_DSP_PARAM_DELAY_MS, next);
                        reference.target = (float)((double)next * 8000 / 1000);
                    }
                    uint32_t frames = pass % 3 == 0 ? 1 : pass % 3 == 1 ? 7 : 257;
                    for (uint32_t i = 0; i < frames * channels; i++) actual[i] = expected[i] = ((int)((i * 23 + pass) % 71) - 35) / 71.0f;
                    procs->process(user, actual, frames, channels, 8000);
                    delay_reference_process(&reference, expected, frames, channels, 8000);
                    float error = 0;
                    for (uint32_t i = 0; i < frames * channels; i++) error = fmaxf(error, fabsf(actual[i] - expected[i]));
                    CHECK(error < 0.000002f);
                }
                free(reference.ring);
                CHECK(LND_NodeFree(node) == LND_OK);
            }
}

static void test_gain_ramps(void) {
    enum { frames = 137, storage = 8192 };
    uint8_t reference[storage], actual[storage], split[storage];
    void *rp[6], *ap[6], *sp[6];
    for (int format = LND_FORMAT_U8; format <= LND_FORMAT_F64; format++)
        for (int layout = 0; layout < 2; layout++)
            for (unsigned channels = 1; channels <= 6; channels += channels == 1 ? 1 : 4)
                for (unsigned padded = 0; padded < 2; padded++)
                    for (unsigned mode = 0; mode < 4; mode++) {
                        size_t bytes = LND_PcmGetSampleBytes(format);
                        size_t stride = bytes * (layout ? 1 : channels) + padded;
                        memset(reference, 0xa5, storage);
                        LND_PCM r = {.data = reference + padded,
                                     .planes = rp,
                                     .frames = frames,
                                     .channels = channels,
                                     .format = format,
                                     .layout = layout,
                                     .stride_bytes = stride};
                        for (unsigned c = 0; c < channels; c++) {
                            rp[c] = reference + c * 1280 + padded;
                            ap[c] = actual + c * 1280 + padded;
                            sp[c] = split + c * 1280 + padded;
                            for (unsigned f = 0; f < frames; f++)
                                lnd_pcm_store_sample(lnd_pcm_at(&r, c, f), format, ((int)((f * 17 + c * 11) % 31) - 15) / 16.0);
                        }
                        memcpy(actual, reference, storage);
                        memcpy(split, reference, storage);
                        LND_PCM a = r, s = r;
                        a.data = actual + padded;
                        a.planes = ap;
                        s.data = split + padded;
                        s.planes = sp;
                        float gain = mode == 3 ? 0.0f : 0.37f, target = mode == 0 ? 0.0f : mode == 3 ? 65536.0f : 2.31f;
                        uint32_t remaining = mode == 1 ? 300 : mode == 2 ? 0 : 71, ar = remaining, sr = remaining;
                        float step = remaining ? (target - gain) / remaining : 0, ag = gain, sg = gain;
                        for (unsigned f = 0; f < frames; f++) {
                            if (remaining) {
                                gain += step;
                                if (!--remaining) gain = target;
                            }
                            lnd_graph_pcm_gain(&r, f, 1, gain);
                        }
                        lnd_graph_pcm_ramp(&a, 0, frames, &ag, step, target, &ar);
                        for (unsigned f = 0; f < frames;) {
                            unsigned count = LND_MIN(frames - f, (f % 13) + 1);
                            lnd_graph_pcm_ramp(&s, f, count, &sg, step, target, &sr);
                            f += count;
                        }
                        CHECK(ar == remaining && sr == remaining && ag == gain && sg == gain);
                        CHECK(!memcmp(actual, split, storage));
                        if (format <= LND_FORMAT_S32) CHECK(!memcmp(actual, reference, storage));
                        else
                            for (unsigned c = 0; c < channels; c++)
                                for (unsigned f = 0; f < frames; f++)
                                    CHECK(lnd_pcm_load_sample(lnd_pcm_at(&a, c, f), format) == lnd_pcm_load_sample(lnd_pcm_at(&r, c, f), format));
                    }
}

int main(void) {
    setbuf(stdout, nullptr);
    uint32_t count = LND_ConfigGetKeyCount();
    uint64_t *defaults = malloc(count * sizeof *defaults);
    CHECK(defaults != nullptr);
    if (!defaults) return 1;
    for (uint32_t i = 0; i < count; i++) {
        LND_CONFIG_KEY *key = LND_ConfigGetKey(i);
        defaults[i] = LND_ConfigGet(key);
        CHECK(defaults[i] == key->desc->def);
        CHECK(LND_ConfigFindKey(LND_ConfigKeyGetName(key)) == key);
    }
    CHECK(!LND_ConfigGetKey(count));
    CHECK(!LND_ConfigFindKey("absent.key"));
    CHECK(!LND_ConfigFindKey(nullptr));
    CHECK(!LND_ConfigKeyGetName(nullptr));
    lnd_config_reset();
    for (uint32_t i = 0; i < count; i++) CHECK(LND_ConfigGet(LND_ConfigGetKey(i)) == defaults[i]);
    free(defaults);
    keep_file();
    CHECK(!LND_OutputCreateFile(path, &wav));
    check_keep();
    for (int layout = 0; layout < 2; layout++) {
        test_init(layout);
        test_reuse();
        test_partial_io();
    test_views_and_units();
        test_io_status();
        if (!layout) {
            test_atomic_file();
            test_output_boundary();
        }
        test_terminal();
        test_topology();
        test_dag();
        test_diamond_seek();
        test_dsp_validation();
        test_gain_ramps();
        test_delay_wrap();
        LND_LibraryFree();
        CHECK(!live_allocations);
    }
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
