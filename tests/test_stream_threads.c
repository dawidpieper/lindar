#include "lindar_graph.h"
#include "lindar.h"
#include "lindar_devices.h"
#include "lindar_autofree.h"
#include "lindar_slide.h"
#include "lnd_modules.h"
#if LND_MODULE_QUEUE
#include "lindar_queue.h"
#endif
#include "src/atomic.h"
#include "src/thread.h"

#include <stdio.h>
#include <string.h>

static lnd_atomic_u32 checks, failures;
static thread_local bool in_fill;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        lnd_add(&checks, 1);                                                                                                                                   \
        if (!(x)) {                                                                                                                                            \
            lnd_add(&failures, 1);                                                                                                                             \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

typedef struct producer {
    lnd_atomic_u32 ready, remaining, produced, closes, eof;
} producer;

static int64_t read_stream(void *user, void *dst, uint64_t frames) {
    producer *p = user;
    if (!lnd_load(&p->ready)) return 0;
    uint32_t remaining = lnd_load(&p->remaining);
    if (!remaining) return lnd_load(&p->eof) ? LND_READ_EOF : 0;
    uint32_t count = frames < remaining ? (uint32_t)frames : remaining;
    uint32_t at = lnd_load(&p->produced);
    int16_t *out = dst;
    for (uint32_t i = 0; i < count; i++)
        out[i] = (int16_t)(at + i + 1);
    lnd_store(&p->produced, at + count);
    lnd_store(&p->remaining, remaining - count);
    return count;
}
static void close_source(void *user) {
    CHECK(!in_fill);
    lnd_add(&((producer *)user)->closes, 1);
}
static void begin(void) {
    CHECK(LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("null")) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_BUFFER_FRAMES, 64) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_BUFFER_COUNT, 4) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
}
static void test_prefetch(bool explicit_end) {
    begin();
    producer p = {.remaining = 523, .eof = !explicit_end};
    LND_SOURCE_PROCS procs = {.read = read_stream, .close = close_source};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &p, LND_FORMAT_S16, 1, 16000, LND_SOURCE_LIVE);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_NODE *bus = LND_NodeCreateBus(1, 16000);
    CHECK(LND_SoundSetOutput(sound, bus) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(bus);
    int16_t data[17];
    LND_PCM pcm = {.data = data, .frames = 17, .channels = 1, .format = LND_FORMAT_S16};
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 17) == LND_OK);
    CHECK(LND_SoundGetState(sound) == LND_SOUND_STALLED && LND_SourceGetPositionFrames(source) == 0);
    lnd_store(&p.ready, 1);
    unsigned received = 0;
    bool ended = false;
    for (unsigned i = 0; i < 2000 && LND_SoundGetState(sound) != LND_SOUND_STOPPED; i++) {
        CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 17) == LND_OK);
        for (unsigned j = 0; j < 17; j++)
            if (data[j]) CHECK(data[j] == ++received);
        if (explicit_end && !ended && !lnd_load(&p.remaining)) {
            CHECK(LND_SourceEnd(source) == LND_OK);
            ended = true;
        }
        lnd_sleep_ms(1);
    }
    CHECK(received == 523 && LND_SourceGetPositionFrames(source) == 523);
    CHECK(LND_SourceGetStatus(source) == LND_SOURCE_EOF && LND_SoundGetState(sound) == LND_SOUND_STOPPED);
    CHECK(LND_SourceFree(source) == LND_OK && lnd_load(&p.closes) == 1);
    LND_LibraryFree();
}

typedef struct rendering {
    LND_RENDERER *renderer;
    lnd_atomic_u32 stop, count;
} rendering;
static void render_thread(void *user) {
    rendering *r = user;
    int16_t data[16];
    LND_PCM pcm = {.data = data, .frames = 16, .channels = 1, .format = LND_FORMAT_S16};
    while (!lnd_load(&r->stop)) {
        in_fill = true;
        int32_t result = LND_RendererFillPcm(r->renderer, &pcm, 0, 16);
        CHECK(result == LND_OK || result == LND_ERR_BUSY);
        in_fill = false;
        if (result == LND_OK) lnd_add(&r->count, 1);
        lnd_sleep_ms(1);
    }
}
static void test_concurrent(void) {
    begin();
    LND_NODE *bus = LND_NodeCreateBus(1, 16000);
    rendering render = {.renderer = LND_RendererCreateNode(bus)};
    lnd_thread thread;
    CHECK(lnd_thread_create(&thread, render_thread, &render) == LND_OK);
    producer producers[100] = {0};
    LND_SOURCE_PROCS procs = {.read = read_stream, .close = close_source};
    LND_SLIDE_CONFIG slide = {.duration_frames = 64};
    for (unsigned i = 0; i < 100; i++) {
        uint32_t before = lnd_load(&render.count);
        producer *p = &producers[i];
        lnd_store(&p->remaining, 523);
        lnd_store(&p->ready, 1);
        lnd_store(&p->eof, 1);
        LND_SOURCE *source = LND_SourceCreateProc(&procs, p, LND_FORMAT_S16, 1, 16000, LND_SOURCE_LIVE);
        LND_AUTOFREE id = LND_AutofreeTakeSource(source, bus);
        CHECK(id != 0);
        if (i % 10 == 0) {
            for (unsigned retry = 0; retry < 1000 && lnd_load(&render.count) == before; retry++)
                lnd_sleep_ms(1);
            CHECK(lnd_load(&render.count) > before);
        }
        CHECK(LND_NodeSlideParam(bus, LND_PARAM_GAIN, i % 2 ? 1.0f : 0.5f, &slide) == LND_OK);
        LND_AUTOFREE_INFO info;
        int32_t status = LND_AutofreeGetInfo(id, &info);
        CHECK(status == LND_OK || status == LND_ERR_STATE);
        status = LND_AutofreeCancel(id);
        CHECK(status == LND_OK || status == LND_ERR_STATE);
        CHECK(lnd_load(&p->closes) == 1);
    }
    lnd_store(&render.stop, 1);
    lnd_thread_join(&thread);
    CHECK(lnd_load(&render.count) > 0);
    LND_LibraryFree();
}

static int32_t seek_stream(void *user, uint64_t frame) {
    producer *p = user;
    if (frame > 5000) return LND_ERR_INVALID_ARG;
    lnd_store(&p->produced, (uint32_t)frame);
    lnd_store(&p->remaining, 5000 - (uint32_t)frame);
    return LND_OK;
}

typedef struct source_observer {
    LND_SOURCE *source;
    lnd_atomic_u32 ready, stop;
    uint64_t reads, invalid;
} source_observer;

static void observe_source(void *user) {
    source_observer *s = user;
    lnd_store(&s->ready, 1);
    while (!lnd_load(&s->stop)) {
        if (LND_SourceGetPositionFrames(s->source) > 5000) s->invalid++;
        s->reads++;
    }
}

static void test_position_threads(bool direct) {
    begin();
    producer p = {.ready = 1, .remaining = 5000, .eof = 1};
    LND_SOURCE_PROCS procs = {.read = read_stream, .seek = seek_stream, .length_frames = 5000, .close = close_source};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &p, LND_FORMAT_S16, 1, 16000, direct ? LND_GRAPH_SOURCE_DIRECT : 0);
    CHECK(source);
    source_observer observer = {.source = source};
    lnd_thread thread;
    CHECK(lnd_thread_create(&thread, observe_source, &observer) == LND_OK);
    while (!lnd_load(&observer.ready)) lnd_sleep_ms(1);
    int16_t data[17];
    for (unsigned i = 0; i < 256; i++) {
        if (i == 128) CHECK(LND_SourceEnsureSound(source, nullptr));
        uint64_t position = i * 97 % 4500;
        CHECK(LND_SourceSeekFrames(source, position) == LND_OK);
        CHECK(LND_SourceRead(source, data, LND_FORMAT_S16, 17) == 17);
        CHECK(LND_SourceGetPositionFrames(source) == position + 17);
        for (unsigned j = 0; j < 17; j++) CHECK(data[j] == position + j + 1);
    }
    lnd_store(&observer.stop, 1);
    lnd_thread_join(&thread);
    CHECK(observer.reads && !observer.invalid);
    CHECK(LND_SourceFree(source) == LND_OK && lnd_load(&p.closes) == 1);
    LND_LibraryFree();
}

static void test_stop_seek(bool direct) {
    begin();
    producer p = {.ready = 1, .remaining = 5000, .eof = 1};
    LND_SOURCE_PROCS procs = {.read = read_stream, .seek = seek_stream, .length_frames = 5000, .close = close_source};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &p, LND_FORMAT_S16, 1, 24000, direct ? LND_GRAPH_SOURCE_DIRECT : 0);
    LND_NODE *bus = LND_NodeCreateBus(1, 48000);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(LND_SoundSetOutput(sound, bus) == LND_OK);
    rendering render = {.renderer = LND_RendererCreateNode(bus)};
    lnd_thread thread;
    CHECK(lnd_thread_create(&thread, render_thread, &render) == LND_OK);
    int16_t samples[17];
    for (unsigned i = 0; i < 16; i++) {
        uint32_t before = lnd_load(&render.count);
        CHECK(LND_SoundPlay(sound) == LND_OK);
        for (unsigned retry = 0; retry < 1000 && lnd_load(&render.count) == before; retry++)
            lnd_sleep_ms(1);
        CHECK(lnd_load(&render.count) > before);
        CHECK(LND_SoundStop(sound) == LND_OK);
        CHECK(LND_SourceSeekFrames(source, 1000) == LND_OK);
        CHECK(LND_SourceRead(source, samples, LND_FORMAT_S16, 17) == 17);
        CHECK(LND_SourceGetPositionFrames(source) == 1017);
        for (unsigned n = 0; n < 17; n++)
            CHECK(samples[n] == 1001 + n);
    }
    lnd_store(&render.stop, 1);
    lnd_thread_join(&thread);
    LND_LibraryFree();
    CHECK(lnd_load(&p.closes) == 1);
}

static void test_maintenance(void) {
    begin();
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 1) == LND_OK);
    LND_NODE *output = LND_DeviceEnsureOutputNode();
    CHECK(output != nullptr);
    producer p = {.remaining = 523, .ready = 1, .eof = 1};
    LND_SOURCE_PROCS procs = {.read = read_stream, .close = close_source};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &p, LND_FORMAT_S16, 1, 16000, LND_SOURCE_LIVE);
    LND_AUTOFREE id = LND_AutofreeTakeSource(source, output);
    CHECK(id != 0);
    for (unsigned i = 0; i < 3000 && LND_AutofreeIsValid(id); i++)
        lnd_sleep_ms(1);
    CHECK(!LND_AutofreeIsValid(id) && lnd_load(&p.closes) == 1);
    LND_LibraryFree();
}
#if LND_MODULE_QUEUE
typedef struct queue_writer {
    LND_SOURCE *source;
    lnd_atomic_u32 done;
} queue_writer;

static void write_queue(void *user) {
    queue_writer *q = user;
    int16_t samples[23];
    LND_PCM pcm = {.data = samples, .frames = 23, .channels = 1, .format = LND_FORMAT_S16};
    unsigned sent = 0;
    while (sent < 2000) {
        size_t count = 2000 - sent < 23 ? 2000 - sent : 23;
        for (size_t i = 0; i < count; i++)
            samples[i] = (int16_t)(sent + i + 1);
        int64_t written = LND_QueueWritePcm(q->source, &pcm, 0, count);
        CHECK(written >= 0 || written == LND_ERR_BUSY);
        if (written > 0)
            sent += (unsigned)written;
        else
            lnd_sleep_ms(1);
    }
    int32_t result;
    do {
        result = LND_SourceEnd(q->source);
        if (result == LND_ERR_BUSY) lnd_sleep_ms(1);
    } while (result == LND_ERR_BUSY);
    CHECK(result == LND_OK);
    lnd_store(&q->done, 1);
}

static void test_queue_threads(void) {
    begin();
    LND_SOURCE *source = LND_SourceCreateQueue(1, 16000, 97);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_RENDERER_CONFIG config = {.render = LND_SoundRenderPcm, .user = sound, .channels = 1, .sample_rate_hz = 16000, .block_frames = 17};
    LND_RENDERER *renderer = LND_RendererCreateProc(&config);
    CHECK(renderer && LND_SoundPlay(sound) == LND_OK);
    queue_writer writer = {.source = source};
    lnd_thread thread;
    CHECK(lnd_thread_create(&thread, write_queue, &writer) == LND_OK);
    int16_t samples[17];
    LND_PCM pcm = {.data = samples, .frames = 17, .channels = 1, .format = LND_FORMAT_S16};
    unsigned received = 0;
    for (unsigned i = 0; i < 3000 && LND_SoundGetState(sound) != LND_SOUND_STOPPED; i++) {
        uint64_t before = LND_SourceGetPositionFrames(source);
        int32_t result = LND_RendererFillPcm(renderer, &pcm, 0, 17);
        CHECK(result == LND_OK || result == LND_ERR_BUSY);
        if (result == LND_OK) {
            for (unsigned j = 0; j < 17; j++)
                if (samples[j]) CHECK(samples[j] == ++received);
        } else
            CHECK(LND_SourceGetPositionFrames(source) == before);
        lnd_sleep_ms(1);
    }
    CHECK(lnd_load(&writer.done));
    lnd_thread_join(&thread);
    CHECK(received == 2000 && LND_SourceGetPositionFrames(source) == 2000 && LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    LND_LibraryFree();
}
#endif

typedef struct split_reader {
    LND_RENDERER *renderer;
    LND_NODE *node;
    uint32_t block, received;
    lnd_atomic_u32 done;
} split_reader;

static void read_split_thread(void *user) {
    split_reader *r = user;
    int16_t samples[31];
    LND_PCM pcm = {.data = samples, .frames = 31, .channels = 1, .format = LND_FORMAT_S16};
    for (unsigned retry = 0; retry < 2000; retry++) {
        int64_t got = LND_RendererReadPcm(r->renderer, &pcm, 0, r->block);
        CHECK(got >= 0 && got <= r->block);
        for (int64_t i = 0; i < got; i++)
            CHECK(samples[i] == (int16_t)++r->received);
        if (!got && LND_NodeGetStatus(r->node) == LND_SOURCE_EOF) break;
        lnd_sleep_ms(1);
    }
    CHECK(r->received == 4093);
    lnd_store(&r->done, 1);
}

static void test_split_threads(void) {
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_MIXER_BUFFER_FRAMES, 8192) == LND_OK);
    begin();
    producer p = {.ready = 1, .remaining = 4093, .eof = 1};
    LND_SOURCE_PROCS procs = {.read = read_stream, .close = close_source};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &p, LND_FORMAT_S16, 1, 16000, LND_GRAPH_SOURCE_DIRECT);
    LND_NODE *mix = LND_NodeCreateMixer(1, 16000, LND_MIX_AVAILABLE | LND_MIX_END);
    LND_NODE *split = LND_NodeCreateSplitter(1, 16000, 3);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(source), mix) == LND_OK && LND_NodeConnect(mix, split) == LND_OK);
    split_reader readers[3] = {0};
    lnd_thread threads[3];
    for (unsigned i = 0; i < 3; i++) {
        LND_NODE *branch = LND_NodeGetSplitterOutput(split, i);
        LND_NODE *output = LND_NodeCreateMixer(1, 16000, LND_MIX_AVAILABLE | LND_MIX_END);
        CHECK(LND_NodeConnect(branch, output) == LND_OK);
        CHECK(LND_SoundPlay(LND_NodeEnsureSound(branch, nullptr)) == LND_OK);
        readers[i] = (split_reader){.renderer = LND_RendererCreateNode(output), .node = output, .block = 17 + 7 * i};
    }
    for (unsigned i = 0; i < 3; i++)
        CHECK(lnd_thread_create(&threads[i], read_split_thread, &readers[i]) == LND_OK);
    LND_NODE *branch = LND_NodeGetSplitterOutput(split, 1);
    LND_NODE *aux = LND_NodeCreateBus(1, 16000);
    for (unsigned i = 0; i < 20; i++) {
        CHECK(LND_NodeSetInputPause(readers[1].node, branch, true) == LND_OK);
        CHECK(LND_NodeConnect(branch, aux) == LND_OK);
        lnd_sleep_ms(1);
        CHECK(LND_NodeDisconnect(branch, aux) == LND_OK);
        CHECK(LND_NodeSetInputPause(readers[1].node, branch, false) == LND_OK);
        lnd_sleep_ms(1);
    }
    for (unsigned i = 0; i < 3; i++) {
        lnd_thread_join(&threads[i]);
        CHECK(lnd_load(&readers[i].done) && LND_NodeGetSplitDroppedFrames(LND_NodeGetSplitterOutput(split, i)) == 0);
    }
    CHECK(lnd_load(&p.produced) == 4093 && LND_SourceGetPositionFrames(source) == 4093);
    LND_LibraryFree();
    CHECK(lnd_load(&p.closes) == 1);
}

int main(void) {
    test_position_threads(false);
    test_position_threads(true);
    test_split_threads();
    test_stop_seek(false);
    test_stop_seek(true);
#if LND_MODULE_QUEUE
    test_queue_threads();
#endif
    test_prefetch(false);
    test_prefetch(true);
    test_concurrent();
    test_maintenance();
    printf("%u checks, %u failures\n", lnd_load(&checks), lnd_load(&failures));
    return lnd_load(&failures) ? 1 : 0;
}
