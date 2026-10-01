#include "lindar_output.h"
#include "src/thread.h"
#include "src/atomic.h"
#include <stdio.h>

typedef struct state {
    LND_OUTPUT *output;
    lnd_atomic_u32 done;
    int32_t result;
    uint64_t position;
} state;
static int32_t encoder_open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **user) {
    *user = io;
    return LND_OK;
}
static int32_t encoder_write(void *user, const float *pcm, uint64_t frames) {
    return LND_IoWrite(user, pcm, (size_t)frames * sizeof(float)) == frames * sizeof(float) ? LND_OK : LND_ERR_IO;
}
static int32_t encoder_close(void *user) { return LND_OK; }
static const LND_ENCODER encoder = {.name = "stats-test", .extensions = "stats", .open = encoder_open, .write = encoder_write, .close = encoder_close};
static size_t output_write(void *user, const void *data, size_t bytes) {
    ((state *)user)->position += bytes;
    return bytes;
}
static void writer(void *user) {
    state *s = user;
    float pcm[31] = {0};
    for (unsigned i = 0; i < 20000; i++) {
        s->result = LND_OutputWrite(s->output, pcm, LND_FORMAT_F32, 31);
        if (s->result != LND_OK) break;
    }
    if (s->result == LND_OK) s->result = LND_OutputFinish(s->output);
    lnd_store(&s->done, 1);
}
#define REQUIRE(x)                                                                                                                                             \
    do {                                                                                                                                                       \
        if (!(x)) {                                                                                                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                                                                         \
            return 1;                                                                                                                                          \
        }                                                                                                                                                      \
    } while (0)
int main(void) {
    REQUIRE(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_MANUAL) == LND_OK);
    REQUIRE(LND_LibraryInit() == LND_OK);
    REQUIRE(LND_EncoderRegister(&encoder) == LND_OK);
    state s = {0};
    LND_IO_OUTPUT_PROCS procs = {.write = output_write};
    LND_ENCODER_PARAMS params = {.encoder_name = encoder.name, .channels = 1, .sample_rate_hz = 48000, .format = LND_FORMAT_F32};
    s.output = LND_OutputCreateProc(&procs, &s, &params);
    REQUIRE(s.output);
    lnd_thread thread = {0};
    REQUIRE(lnd_thread_create(&thread, writer, &s) == LND_OK);
    uint64_t last = 0, snapshots = 0;
    do {
        LND_OUTPUT_INFO info;
        REQUIRE(LND_OutputGetInfo(s.output, &info) == LND_OK);
        REQUIRE(info.frames >= last && info.frames % 31 == 0 && info.bytes == info.frames * sizeof(float) && info.result == LND_OK);
        last = info.frames;
        snapshots++;
    } while (!lnd_load(&s.done));
    lnd_thread_join(&thread);
    REQUIRE(s.result == LND_OK);
    LND_OUTPUT_INFO info;
    REQUIRE(LND_OutputGetInfo(s.output, &info) == LND_OK && info.finished && info.frames == 620000 && info.bytes == 2480000);
    REQUIRE(LND_OutputFree(s.output) == LND_OK);
    LND_LibraryFree();
    printf("%llu concurrent snapshots\n", (unsigned long long)snapshots);
    return 0;
}
