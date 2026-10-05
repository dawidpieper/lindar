#include "io/devices/capture.h"
#include "lindar_devices.h"
#include "lindar.h"
#include "playback/graph/sound.h"
#include "lindar_graph.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks, failures;

#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

static void test_overflow(void) {
    lnd_capture *c = lnd_capture_new(1, 8000, 1024);
    CHECK(c != nullptr);
    float in[1536], out[1024];
    for (unsigned i = 0; i < 1536; i++)
        in[i] = (float)i;
    lnd_capture_push(c, in, LND_FORMAT_F32, 1536);
    CHECK(lnd_capture_available(c) == 1024);
    CHECK(lnd_load(&c->overruns) == 512);
    CHECK(lnd_capture_read(c, out, 512, LND_CAPTURE_READ_PARTIAL) == 512);
    for (unsigned i = 0; i < 512; i++)
        CHECK(out[i] == (float)i);
    lnd_capture_push(c, in + 1024, LND_FORMAT_F32, 512);
    CHECK(lnd_capture_read(c, out, 1024, LND_CAPTURE_READ_PARTIAL) == 1024);
    for (unsigned i = 0; i < 1024; i++)
        CHECK(out[i] == (float)(i + 512));
    CHECK(lnd_capture_available(c) == 0);
    lnd_capture_free(c);
}

static void test_converted_large_push(void) {
    lnd_capture *c = lnd_capture_new(2, 48000, 1024);
    CHECK(c != nullptr);
    unsigned char *in = malloc(8192 * 2);
    for (unsigned i = 0; i < 8192 * 2; i++)
        in[i] = i % 2 ? 192 : 64;
    lnd_capture_push(c, in, LND_FORMAT_U8, 8192);
    CHECK(lnd_capture_available(c) == 1024);
    CHECK(lnd_load(&c->overruns) == 7168);
    float out[2048];
    CHECK(lnd_capture_read(c, out, 1024, LND_CAPTURE_READ_PARTIAL) == 1024);
    for (unsigned i = 0; i < 2048; i++)
        CHECK(out[i] == (i % 2 ? 0.5f : -0.5f));
    lnd_capture_end(c);
    CHECK(lnd_capture_read(c, out, 1, LND_CAPTURE_READ_WAIT) == 0);
    free(in);
    lnd_capture_free(c);
}

static void test_prefill(void) {
    lnd_capture *c = lnd_capture_new(1, 48000, 2048);
    CHECK(c != nullptr);
    c->prefill = 960;
    float input[960], output[256];
    for (unsigned i = 0; i < 960; i++)
        input[i] = 0.5f;
    lnd_capture_push(c, input, LND_FORMAT_F32, 512);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 0);
    CHECK(lnd_capture_available(c) == 512);
    lnd_capture_push(c, input, LND_FORMAT_F32, 448);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 256);
    CHECK(lnd_capture_available(c) == 704);
    for (unsigned i = 0; i < 256; i++)
        CHECK(output[i] == 0.5f);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 256);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 256);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 0);
    CHECK(lnd_capture_available(c) == 192);
    lnd_capture_push(c, input, LND_FORMAT_F32, 512);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 0);
    CHECK(lnd_capture_available(c) == 704);
    lnd_capture_push(c, input, LND_FORMAT_F32, 256);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 256);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_PARTIAL) == 256);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 0);
    lnd_capture_end(c);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 256);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 192);
    for (unsigned i = 0; i < 192; i++)
        CHECK(output[i] == 0.5f);
    CHECK(lnd_capture_read(c, output, 256, LND_CAPTURE_READ_STREAM) == 0);
    lnd_capture_free(c);
}

static void test_source_status(uint32_t sample_rate_hz) {
    lnd_capture *c = lnd_capture_new(1, 48000, 2048);
    CHECK(c != nullptr);
    c->prefill = 960;
    lnd_source *s = lnd_capture_source_create(c, 1, sample_rate_hz, LND_CAPTURE_RESAMPLE | LND_CAPTURE_NONBLOCKING);
    CHECK(s != nullptr);
    float input[1440], output[256];
    for (unsigned i = 0; i < 1440; i++)
        input[i] = 0.25f;
    CHECK(lnd_source_read(s, output, 256) == 0);
    CHECK(lnd_source_status(s) == LND_SOURCE_WAITING);
    lnd_capture_push(c, input, LND_FORMAT_F32, 1440);
    CHECK(lnd_source_read(s, output, 256) == 256);
    CHECK(lnd_source_status(s) == LND_SOURCE_READY);
    lnd_capture_end(c);
    uint64_t total = 256, got;
    do {
        got = lnd_source_read_sync(s, output, 256);
        total += got;
    } while (got && total < 2048);
    uint64_t expected = sample_rate_hz * 3 / 100;
    CHECK(total >= expected && total <= expected + (sample_rate_hz != 48000));
    CHECK(lnd_source_status(s) == LND_SOURCE_EOF);
    CHECK(lnd_source_read(s, output, 256) == 0);
    lnd_source_free(s);
}

static void test_source_available(void) {
    lnd_capture *c = lnd_capture_new(1, 48000, 2048);
    CHECK(c != nullptr);
    c->prefill = 960;
    lnd_source *s = lnd_capture_source_create(c, 1, 48000, 0);
    CHECK(s != nullptr);
    float input[960], output[960];
    for (unsigned i = 0; i < 960; i++)
        input[i] = 0.25f;
    lnd_capture_push(c, input, LND_FORMAT_F32, 512);
    CHECK(lnd_source_available(s) == 0);
    lnd_capture_push(c, input, LND_FORMAT_F32, 448);
    CHECK(lnd_source_available(s) == 480);
    CHECK(lnd_source_read(s, output, 480) == 480);
    CHECK(lnd_source_available(s) == 480);
    CHECK(lnd_source_read(s, output, 480) == 480);
    CHECK(lnd_source_read(s, output, 1) == 0);
    lnd_capture_push(c, input, LND_FORMAT_F32, 700);
    CHECK(lnd_source_available(s) == 0);
    lnd_capture_push(c, input, LND_FORMAT_F32, 260);
    CHECK(lnd_source_available(s) == 480);
    CHECK(lnd_source_read(s, output, 481) == 0);
    CHECK(lnd_source_available(s) == 480);
    CHECK(lnd_source_read(s, output, 480) == 480);
    lnd_capture_end(c);
    CHECK(lnd_source_available(s) == UINT64_MAX);
    CHECK(lnd_source_read(s, output, 960) == 480);
    CHECK(lnd_source_status(s) == LND_SOURCE_EOF);
    lnd_source_free(s);
}

static void test_control(void) {
    CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    lnd_capture *c = lnd_capture_new(1, 48000, 1024);
    lnd_source *inner = lnd_capture_source_create(c, 1, 44100, LND_CAPTURE_RESAMPLE | LND_CAPTURE_NONBLOCKING);
    LND_SOURCE *source = (LND_SOURCE *)lnd_source_obj_create(inner, 0, LND_FORMAT_F32);
    CHECK(source != nullptr);
    float input[2048], output[128];
    for (unsigned i = 0; i < 2048; ++i) input[i] = 0.75f;
    lnd_capture_push(c, input, LND_FORMAT_F32, 2048);
    LND_CAPTURE_INFO info;
    CHECK(LND_SourceGetCaptureInfo(source, &info) == LND_OK && info.buffered_frames == 1024 && info.dropped_frames == 1024 && !info.paused);
    CHECK(LND_SourceRead(source, output, LND_FORMAT_F32, 128) == 128);
    CHECK(LND_SourceSetCapturePause(source, true) == LND_OK);
    CHECK(LND_SourceGetCaptureInfo(source, &info) == LND_OK && info.paused && info.buffered_frames == 0);
    lnd_capture_push(c, input, LND_FORMAT_F32, 1024);
    CHECK(LND_SourceRead(source, output, LND_FORMAT_F32, 128) == 0);
    CHECK(LND_SourceGetStatus(source) == LND_SOURCE_WAITING);
    CHECK(LND_SourceResetCapture(source) == LND_OK);
    CHECK(LND_SourceGetCaptureInfo(source, &info) == LND_OK && info.paused && info.dropped_frames == 0 && info.buffered_frames == 0);
    CHECK(LND_SourceSetCapturePause(source, false) == LND_OK);
    for (unsigned i = 0; i < 2048; ++i) input[i] = -0.25f;
    lnd_capture_push(c, input, LND_FORMAT_F32, 1024);
    CHECK(LND_SourceRead(source, output, LND_FORMAT_F32, 128) == 128);
    CHECK(output[64] < -0.24f && output[64] > -0.26f);
    CHECK(LND_SourceResetCapture(source) == LND_OK);
    CHECK(LND_SourceRead(source, output, LND_FORMAT_F32, 128) == 0);
    CHECK(LND_SourceFree(source) == LND_OK);
}
int main(void) {
    test_control();

    test_source_available();
    test_prefill();
    test_source_status(48000);
    test_source_status(44100);
    test_overflow();
    test_converted_large_push();
    LND_LibraryFree();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
