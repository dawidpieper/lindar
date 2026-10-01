#include "stream_test.h"
#include "lindar_notify.h"
#if LND_MODULE_GRAPH
#include "lindar_graph.h"
#endif
#if LND_MODULE_SLIDE
#include "lindar_slide.h"
#endif
#if LND_MODULE_AUTOFREE
#include "lindar_autofree.h"
#endif

static LND_SUBSCRIPTION *subscribe(LND_SOUND *sound, int32_t type, uint64_t position, uint32_t capacity, uint32_t flags) {
    LND_SUBSCRIPTION_CONFIG config = {.type = type, .position_frames = position, .notification_capacity = capacity, .flags = flags};
    LND_SUBSCRIPTION *s = LND_SoundSubscribe(sound, &config);
    CHECK(s != nullptr);
    return s;
}

static void event(LND_SUBSCRIPTION *s, int32_t type, uint64_t position) {
    LND_NOTIFICATION e = {0};
    CHECK(LND_SubscriptionRead(s, &e) == 1);
    CHECK(e.type == type && e.position_frames == position && e.sample_rate_hz == 8000);
}

static void empty(LND_SUBSCRIPTION *s) {
    LND_NOTIFICATION e;
    CHECK(LND_SubscriptionRead(s, &e) == 0);
}

static void native(void) {
    begin(LND_FORMAT_S16, LND_LAYOUT_INTERLEAVED);
    int16_t input[8] = {0}, output[32];
    LND_PCM in = {.data = input, .frames = 8, .channels = 1, .format = LND_FORMAT_S16};
    LND_PCM out = {.data = output, .frames = 32, .channels = 1, .format = LND_FORMAT_S16};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &in, .sample_rate_hz = 8000, .channels = 1});
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(source && sound);
    CHECK(!LND_SoundSubscribe(sound, &(LND_SUBSCRIPTION_CONFIG){.notification_capacity = 65537}));
    CHECK(LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    fail_after = 0;
    CHECK(!LND_SoundSubscribe(sound, &(LND_SUBSCRIPTION_CONFIG){0}));
    CHECK(LND_ErrorGetLast() == LND_ERR_OUT_OF_MEMORY);
    fail_after = -1;
    LND_SUBSCRIPTION *end = subscribe(sound, LND_NOTIFY_END, 0, 0, 0);
    LND_SUBSCRIPTION *position = subscribe(sound, LND_NOTIFY_POSITION, 4, 2, 0);
    LND_SUBSCRIPTION *once = subscribe(sound, LND_NOTIFY_POSITION, 4, 0, LND_NOTIFY_ONCE);
    LND_SUBSCRIPTION *zero = subscribe(sound, LND_NOTIFY_POSITION, 0, 0, 0);
    CHECK(!LND_SoundSubscribe(sound, &(LND_SUBSCRIPTION_CONFIG){.type = LND_NOTIFY_SLIDE_END}));
    CHECK(LND_SubscriptionIsAttached(end));
    CHECK(LND_SoundPlay(sound) == LND_OK);
    deny_alloc = true;
    CHECK(LND_SoundRenderPcm(sound, &out, 0, 3) == 3);
    empty(position);
    CHECK(LND_SoundRenderPcm(sound, &out, 0, 5) == 5);
    event(position, LND_NOTIFY_POSITION, 4);
    event(once, LND_NOTIFY_POSITION, 4);
    event(zero, LND_NOTIFY_POSITION, 0);
    event(end, LND_NOTIFY_END, 8);
    CHECK(LND_SoundRenderPcm(sound, &out, 0, 8) == 0);
    empty(end);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_SoundStop(sound) == LND_OK);
    empty(end);
    CHECK(LND_SoundSeekFrames(sound, 6) == LND_OK);
    empty(position);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_SoundRenderPcm(sound, &out, 0, 8) == 2);
    empty(position);
    event(end, LND_NOTIFY_END, 8);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK);
    CHECK(LND_SoundRenderPcm(sound, &out, 0, 32) == 32);
    empty(end);
    event(position, LND_NOTIFY_POSITION, 4);
    event(position, LND_NOTIFY_POSITION, 4);
    empty(position);
    CHECK(LND_SubscriptionGetDroppedCount(position) == 2);
    empty(once);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(!LND_SubscriptionIsAttached(end));
    CHECK(LND_SubscriptionFree(end) == LND_OK);
    CHECK(LND_SubscriptionFree(end) == LND_ERR_INVALID_ARG);
    finish();
}

typedef struct feed {
    size_t frames;
} feed;

static int64_t read_feed(void *user, const LND_PCM *pcm, size_t offset, size_t frames) {
    feed *f = user;
    size_t n = f->frames < frames ? f->frames : frames;
    LND_PcmSilence(pcm, offset, n);
    f->frames -= n;
    return (int64_t)n;
}

static unsigned calls;
static void callback(void *user, const LND_NOTIFICATION *e) {
    unsigned *count = user;
    (*count)++;
    CHECK(e->type == LND_NOTIFY_END);
    CHECK(LND_LibraryUpdate() == LND_ERR_BUSY);
}

static void waiting(void) {
    begin(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    feed f = {0};
    LND_SOURCE_CONFIG config = {.read = read_feed, .user = &f, .channels = 1, .sample_rate_hz = 8000, .block_frames = 8, .flags = LND_SOURCE_LIVE};
    LND_SOURCE *source = LND_SourceCreate(&config);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(source && sound);
    LND_SUBSCRIPTION *stalled = subscribe(sound, LND_NOTIFY_STALLED, 0, 0, 0);
    LND_SUBSCRIPTION *resumed = subscribe(sound, LND_NOTIFY_RESUMED, 0, 0, 0);
    LND_SUBSCRIPTION *end = LND_SoundSubscribe(sound, &(LND_SUBSCRIPTION_CONFIG){.type = LND_NOTIFY_END, .proc = callback, .user = &calls});
    int16_t data[8];
    LND_PCM pcm = {.data = data, .frames = 8, .channels = 1, .format = LND_FORMAT_S16};
    LND_RENDERER *r =
        LND_RendererCreateProc(&(LND_RENDERER_CONFIG){.render = LND_SoundRenderPcm, .user = sound, .channels = 1, .sample_rate_hz = 8000, .block_frames = 8});
    CHECK(r && end);
    CHECK(!LND_SubscriptionIsAutomatic(end));
    CHECK(LND_SoundPlay(sound) == LND_OK);
    deny_alloc = true;
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 8) == LND_OK);
    event(stalled, LND_NOTIFY_STALLED, 0);
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 8) == LND_OK);
    empty(stalled);
    f.frames = 8;
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 8) == LND_OK);
    event(resumed, LND_NOTIFY_RESUMED, 8);
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 8) == LND_OK);
    event(stalled, LND_NOTIFY_STALLED, 8);
    CHECK(LND_SourceEnd(source) == LND_OK);
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 8) == LND_OK);
    CHECK(calls == 0);
    CHECK(LND_RendererFree(r) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);
    CHECK(!LND_SubscriptionIsAttached(end));
    CHECK(LND_LibraryUpdate() == LND_OK && calls == 1);
    CHECK(LND_LibraryUpdate() == LND_OK && calls == 1);
    finish();
}

#if LND_MODULE_GRAPH
static void process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t rate) {
    unsigned *count = user;
    (*count)++;
    CHECK(rate == 8000);
    for (size_t i = 0; i < (size_t)frames * channels; i++)
        pcm[i] += 0.25f;
}

static int64_t graph_read(void *user, void *pcm, uint64_t frames) {
    feed *f = user;
    size_t n = f->frames < frames ? f->frames : (size_t)frames;
    memset(pcm, 0, n * sizeof(float));
    f->frames -= n;
    return (int64_t)n;
}

static void graph_waiting(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    feed f = {0};
    LND_SOURCE_PROCS procs = {.read = graph_read};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &f, LND_FORMAT_F32, 1, 8000, LND_GRAPH_SOURCE_DIRECT | LND_SOURCE_LIVE);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_RENDERER *r = LND_RendererCreateNode(LND_SoundEnsureNode(sound));
    LND_SUBSCRIPTION *stall = subscribe(sound, LND_NOTIFY_STALLED, 0, 0, 0);
    LND_SUBSCRIPTION *resume = subscribe(sound, LND_NOTIFY_RESUMED, 0, 0, 0);
    LND_SUBSCRIPTION *end = subscribe(sound, LND_NOTIFY_END, 0, 0, 0);
    CHECK(r && LND_SoundPlay(sound) == LND_OK);
    float data[8];
    LND_PCM pcm = {.data = data, .frames = 8, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 8) == LND_OK);
    event(stall, LND_NOTIFY_STALLED, 0);
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 8) == LND_OK);
    empty(stall);
    f.frames = 8;
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 8) == LND_OK);
    event(resume, LND_NOTIFY_RESUMED, 8);
    CHECK(LND_SourceEnd(source) == LND_OK);
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 8) == LND_OK);
    event(end, LND_NOTIFY_END, 8);
    finish();
}

static int32_t graph_seek(void *user, uint64_t frame) {
    if (frame > 8)
        return LND_ERR_INVALID_ARG;
    ((feed *)user)->frames = (size_t)(8 - frame);
    return LND_OK;
}

static void graph_loop(void) {
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    feed f = {.frames = 8};
    LND_SOURCE_PROCS procs = {.read = graph_read, .seek = graph_seek, .length_frames = 8, .length_known = true};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &f, LND_FORMAT_F32, 1, 8000, LND_GRAPH_SOURCE_DIRECT);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_NODE *node = LND_SoundEnsureNode(sound);
    unsigned processed = 0;
    LND_NODE *effect = LND_NodeCreateProcessorProc(process, &processed, 1, 8000, LND_PROCESSOR_BOUNDED);
    CHECK(LND_NodeConnect(node, effect) == LND_OK);
    LND_RENDERER *r = LND_RendererCreateNode(effect);
    LND_SUBSCRIPTION *end = LND_NodeSubscribe(effect, &(LND_SUBSCRIPTION_CONFIG){.type = LND_NOTIFY_END});
    LND_SUBSCRIPTION *position = subscribe(sound, LND_NOTIFY_POSITION, 4, 0, 0);
    CHECK(r && end);
    CHECK(!LND_SubscriptionIsAutomatic(end));
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    float data[24];
    LND_PCM pcm = {.data = data, .frames = 24, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    for (unsigned i = 0; i < 3; i++) {
        CHECK(LND_RendererReadPcm(r, &pcm, 0, 8) == 8);
        event(position, LND_NOTIFY_POSITION, 4);
        empty(end);
    }
    CHECK(LND_RendererReadPcm(r, &pcm, 0, 24) == 24);
    for (unsigned i = 0; i < 3; i++)
        event(position, LND_NOTIFY_POSITION, 4);
    empty(end);
    finish();
}

static void graph(int32_t format, int32_t layout) {
    begin(format, layout);
    feed f = {.frames = 8};
    LND_SOURCE_PROCS procs = {.read = graph_read};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &f, LND_FORMAT_F32, 1, 8000, LND_GRAPH_SOURCE_DIRECT);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_NODE *node = LND_SoundEnsureNode(sound);
    CHECK(source && sound && node);
    unsigned processed = 0;
    LND_NODE *effect = LND_NodeCreateProcessorProc(process, &processed, 1, 8000, LND_PROCESSOR_BOUNDED);
    CHECK(effect != nullptr);
    CHECK(LND_NodeConnect(node, effect) == LND_OK);
    LND_RENDERER *r = LND_RendererCreateNode(effect);
    LND_SUBSCRIPTION *end = subscribe(sound, LND_NOTIFY_END, 0, 0, 0);
    LND_SUBSCRIPTION *position = subscribe(sound, LND_NOTIFY_POSITION, 4, 0, 0);
    LND_SUBSCRIPTION *node_end = LND_NodeSubscribe(effect, &(LND_SUBSCRIPTION_CONFIG){.type = LND_NOTIFY_END});
    LND_SUBSCRIPTION *node_position = LND_NodeSubscribe(node, &(LND_SUBSCRIPTION_CONFIG){.type = LND_NOTIFY_POSITION, .position_frames = 4});
    CHECK(r && node_end && node_position);
    CHECK(LND_SoundPlay(sound) == LND_OK);
    float output[16];
    LND_PCM pcm = {.data = output, .frames = 16, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 16) == LND_OK);
    CHECK(processed == 1 && output[0] == 0.25f && output[8] == 0.0f);
    event(position, LND_NOTIFY_POSITION, 4);
    event(node_position, LND_NOTIFY_POSITION, 4);
    event(end, LND_NOTIFY_END, 8);
    event(node_end, LND_NOTIFY_END, 8);
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 16) == LND_OK);
    empty(end);
    empty(node_end);
    finish();
}
#endif

#if LND_MODULE_AUTOFREE
static void autofree(void) {
    begin(LND_FORMAT_S16, LND_LAYOUT_INTERLEAVED);
    int16_t input[8] = {0}, output[16];
    LND_PCM in = {.data = input, .frames = 8, .channels = 1, .format = LND_FORMAT_S16};
    LND_SOURCE *source = LND_SourceCreate(&(LND_SOURCE_CONFIG){.pcm = &in, .channels = 1, .sample_rate_hz = 8000});
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_SUBSCRIPTION *end = subscribe(sound, LND_NOTIFY_END, 0, 0, 0);
    LND_NODE *bus = LND_NodeCreateBus(1, 8000);
    LND_RENDERER *r = LND_RendererCreateNode(bus);
    LND_AUTOFREE id = LND_AutofreeTakeSource(source, bus);
    CHECK(id && r);
    LND_PCM out = {.data = output, .frames = 16, .channels = 1, .format = LND_FORMAT_S16};
    CHECK(LND_RendererFillPcm(r, &out, 0, 16) == LND_OK);
    CHECK(LND_LibraryUpdate() == LND_OK);
    CHECK(!LND_AutofreeIsValid(id));
    CHECK(!LND_SubscriptionIsAttached(end));
    event(end, LND_NOTIFY_END, 8);
    finish();
}
#endif

#if LND_MODULE_SLIDE
static void set_param(void *user, int32_t param, float value) {
    (void)user;
    CHECK(param == LND_PARAM_USER && value >= 0 && value <= 1);
}

static void slides(int32_t format, int32_t layout) {
    begin(format, layout);
    unsigned processed = 0;
    LND_PROCESSOR_PROCS procs = {.process = process, .param = set_param};
    LND_NODE *node = LND_NodeCreateProcessor(&procs, &processed, 1, 8000);
    LND_RENDERER *r = LND_RendererCreateNode(node);
    LND_SUBSCRIPTION *sub = LND_NodeSubscribe(node, &(LND_SUBSCRIPTION_CONFIG){.type = LND_NOTIFY_SLIDE_END, .param = LND_PARAM_GAIN});
    CHECK(node && r && sub);
    CHECK(LND_NodeSlideParam(node, LND_PARAM_GAIN, 0.5f, &(LND_SLIDE_CONFIG){.duration_frames = 5}) == LND_OK);
    float output[16];
    LND_PCM pcm = {.data = output, .frames = 16, .channels = 1, .format = LND_FORMAT_F32};
    deny_alloc = true;
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 4) == LND_OK);
    empty(sub);
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 12) == LND_OK);
    event(sub, LND_NOTIFY_SLIDE_END, 5);
    CHECK(LND_NodeSlideParam(node, LND_PARAM_GAIN, 1, &(LND_SLIDE_CONFIG){.duration_frames = 3}) == LND_OK);
    CHECK(LND_NodeCancelSlide(node, LND_PARAM_GAIN) == LND_OK);
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 8) == LND_OK);
    empty(sub);
    CHECK(LND_NodeSlideParam(node, LND_PARAM_GAIN, 1, &(LND_SLIDE_CONFIG){0}) == LND_OK);
    event(sub, LND_NOTIFY_SLIDE_END, 24);
    CHECK(LND_NodeSlideParam(node, LND_PARAM_GAIN, 1, &(LND_SLIDE_CONFIG){.duration_frames = 40}) == LND_OK);
    event(sub, LND_NOTIFY_SLIDE_END, 24);
    deny_alloc = false;
    LND_SUBSCRIPTION *parameter = LND_NodeSubscribe(node, &(LND_SUBSCRIPTION_CONFIG){.type = LND_NOTIFY_SLIDE_END, .param = LND_PARAM_USER});
    CHECK(parameter != nullptr);
    CHECK(LND_NodeSlideParam(node, LND_PARAM_USER, 0.75f, &(LND_SLIDE_CONFIG){.duration_frames = 5, .step_frames = 3}) == LND_OK);
    deny_alloc = true;
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 16) == LND_OK);
    LND_NOTIFICATION done;
    CHECK(LND_SubscriptionRead(parameter, &done) == 1);
    CHECK(done.type == LND_NOTIFY_SLIDE_END && done.param == LND_PARAM_USER && done.value == 0.75f && done.position_frames == 29);
    CHECK(LND_NodeSlideParam(node, LND_PARAM_USER, 0, &(LND_SLIDE_CONFIG){.duration_frames = 5}) == LND_OK);
    CHECK(LND_NodeSlideParam(node, LND_PARAM_USER, 1, &(LND_SLIDE_CONFIG){.duration_frames = 7}) == LND_OK);
    CHECK(LND_RendererFillPcm(r, &pcm, 0, 16) == LND_OK);
    event(parameter, LND_NOTIFY_SLIDE_END, 47);
    empty(parameter);
    finish();
}
#endif

int main(void) {
    native();
    waiting();
#if LND_MODULE_AUTOFREE
    autofree();
#endif
#if LND_MODULE_GRAPH
    graph_loop();
    graph_waiting();
    graph(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    graph(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
#endif
#if LND_MODULE_SLIDE
    slides(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    slides(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
#endif
    return report();
}
