#include "stream_test.h"
#include "lindar_slide.h"

#include <math.h>

typedef struct effect {
    float a, b;
    unsigned changes;
} effect;

static void process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    (void)sample_rate_hz;
    effect *e = user;
    for (size_t i = 0; i < (size_t)frames * channels; i++)
        pcm[i] = e->a + e->b;
}
static void param(void *user, int32_t param, float value) {
    effect *e = user;
    if (param == LND_PARAM_USER) e->a = value;
    if (param == LND_PARAM_USER + 1) e->b = value;
    e->changes++;
}
static LND_NODE *node(effect *e) {
    LND_PROCESSOR_PROCS procs = {.process = process, .param = param};
    return LND_NodeCreateProcessor(&procs, e, 1, 16000);
}
static void render(LND_RENDERER *r, float *data, size_t count, size_t chunk) {
    void *planes[] = {data};
    LND_PCM pcm = {
        .data = data, .planes = planes, .frames = count, .channels = 1, .format = LND_FORMAT_F32, .layout = (int32_t)LND_ConfigGet(LND_CFG_INTERNAL_LAYOUT)};
    for (size_t at = 0; at < count; at += chunk) {
        size_t n = count - at < chunk ? count - at : chunk;
        CHECK(LND_RendererFillPcm(r, &pcm, at, n) == LND_OK);
    }
}

static void test_slide(int32_t format, int32_t layout) {
    begin(format, layout);
    effect a = {0}, b = {0};
    LND_NODE *na = node(&a), *nb = node(&b);
    LND_RENDERER *ra = LND_RendererCreateNode(na), *rb = LND_RendererCreateNode(nb);
    CHECK(na && nb && ra && rb);
    CHECK(LND_NodeSetParam(na, LND_PARAM_USER, 0) == LND_OK);
    CHECK(LND_NodeSetParam(nb, LND_PARAM_USER, 0) == LND_OK);
    LND_SLIDE_CONFIG config = {.duration_frames = 100, .step_frames = 7};
    CHECK(LND_NodeSlideParam(na, LND_PARAM_USER, 0.5f, &config) == LND_OK);
    CHECK(LND_NodeSlideParam(nb, LND_PARAM_USER, 0.5f, &config) == LND_OK);
    CHECK(LND_NodeSlideParam(na, LND_PARAM_USER + 1, 0.25f, &(LND_SLIDE_CONFIG){.duration_frames = 53, .step_frames = 11}) == LND_OK);
    CHECK(LND_NodeSlideParam(nb, LND_PARAM_USER + 1, 0.25f, &(LND_SLIDE_CONFIG){.duration_frames = 53, .step_frames = 11}) == LND_OK);
    float x[128], y[128];
    deny_alloc = true;
    render(ra, x, 128, 128);
    render(rb, y, 128, 3);
    for (unsigned i = 0; i < 128; i++)
        CHECK(fabsf(x[i] - y[i]) < 0.00004f);
    CHECK(!LND_NodeIsSliding(na, LND_PARAM_USER));
    CHECK(a.changes == b.changes && a.changes < 25);
    CHECK(LND_NodeGetParam(na, LND_PARAM_USER) == 0.5f);
    LND_SLIDE_STATE state;
    CHECK(LND_NodeGetSlideState(na, LND_PARAM_USER, &state) == LND_OK && state.elapsed_frames == 100 && state.value == 0.5f);
    CHECK(fabsf(x[127] - 0.75f) < 0.00004f);
    config = (LND_SLIDE_CONFIG){.duration_frames = 64, .step_frames = 1, .curve = LND_SLIDE_LOGARITHMIC};
    CHECK(LND_NodeSlideParam(na, LND_PARAM_USER, 0.125f, &config) == LND_OK);
    render(ra, x, 32, 5);
    CHECK(LND_NodeGetSlideState(na, LND_PARAM_USER, &state) == LND_OK && fabsf(state.value - 0.25f) < 0.000001f);
    CHECK(LND_NodeSlideParam(na, LND_PARAM_USER, 0, &config) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeCancelSlide(na, LND_PARAM_USER) == LND_OK && !LND_NodeIsSliding(na, LND_PARAM_USER));
    CHECK(fabsf(LND_NodeGetParam(na, LND_PARAM_USER) - 0.25f) < 0.000001f);
    CHECK(LND_NodeSlideParam(na, LND_PARAM_USER, 1, &(LND_SLIDE_CONFIG){.duration_frames = 100}) == LND_OK);
    CHECK(LND_NodeSetParam(na, LND_PARAM_USER, 0.125f) == LND_OK);
    render(ra, x, 1, 1);
    CHECK(!LND_NodeIsSliding(na, LND_PARAM_USER) && LND_NodeGetParam(na, LND_PARAM_USER) == 0.125f);
    CHECK(LND_NodeSlideParam(na, LND_PARAM_USER, 0.5f, &(LND_SLIDE_CONFIG){0}) == LND_OK);
    CHECK(LND_NodeGetParam(na, LND_PARAM_USER) == 0.5f);
    deny_alloc = false;
    CHECK(LND_NodeSlideParam(na, LND_PARAM_GAIN, 0, &(LND_SLIDE_CONFIG){.duration_frames = 32}) == LND_OK);
    deny_alloc = true;
    render(ra, x, 64, 7);
    for (unsigned i = 0; i < 32; i++)
        CHECK(fabsf(x[i] - 0.75f * (31 - i) / 32) < 0.00008f);
    for (unsigned i = 32; i < 64; i++)
        CHECK(x[i] == 0);
    CHECK(LND_NodeGetGain(na) == 0 && !LND_NodeIsSliding(na, LND_PARAM_GAIN));
    CHECK(LND_NodeSlideParam(na, LND_PARAM_GAIN, -1, &config) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSlideParam(na, INT32_MAX, 1, &config) == LND_ERR_INVALID_ARG);
    finish();
}
int main(void) {
    test_slide(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    test_slide(LND_FORMAT_S16, LND_LAYOUT_PLANAR);
    return report();
}
