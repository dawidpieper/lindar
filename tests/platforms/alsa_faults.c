#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static snd_pcm_sframes_t mock_write(snd_pcm_t *, const void *, snd_pcm_uframes_t);
static snd_pcm_sframes_t mock_read(snd_pcm_t *, void *, snd_pcm_uframes_t);
static int mock_wait(snd_pcm_t *, int);
static int mock_prepare(snd_pcm_t *);
static int mock_resume(snd_pcm_t *);
static int mock_start(snd_pcm_t *);
#define snd_pcm_writei mock_write
#define snd_pcm_readi mock_read
#define snd_pcm_wait mock_wait
#define snd_pcm_prepare mock_prepare
#define snd_pcm_resume mock_resume
#define snd_pcm_start mock_start
#define lnd_backend_alsa_vt lnd_backend_alsa_fault_vt
#include "io/devices/alsa/alsa.c"

static unsigned checks, failures, index_, callback_count, prepared, resumed, started, waits, scenario;
static uint64_t received;
static lnd_stream *current;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%d: %s\n", __LINE__, #x);                                                                                                                  \
        }                                                                                                                                                      \
    } while (0)

static int mock_prepare(snd_pcm_t *pcm) {
    prepared++;
    return 0;
}
static int mock_resume(snd_pcm_t *pcm) { return ++resumed == 1 ? -EAGAIN : 0; }
static int mock_start(snd_pcm_t *pcm) {
    started++;
    return 0;
}
static int mock_wait(snd_pcm_t *pcm, int timeout) {
    waits++;
    CHECK(timeout > 0 && timeout <= 20);
    return 1;
}

static snd_pcm_sframes_t mock_write(snd_pcm_t *pcm, const void *data, snd_pcm_uframes_t count) {
    static const int partial[] = {2, -EAGAIN, -EINTR, 2, -EPIPE, 4, -ENODEV};
    static const unsigned offsets[] = {0, 2, 2, 2, 0, 0, 0};
    static const int suspended[] = {-ESTRPIPE, -ESTRPIPE, 4, -ENODEV};
    if (scenario == 1) {
        CHECK(index_ < LND_COUNTOF(partial));
        if (index_ >= LND_COUNTOF(partial)) return -ENODEV;
        CHECK((const unsigned char *)data == (const unsigned char *)current->buffer + offsets[index_] * sizeof(float));
        CHECK(count == 4 - offsets[index_]);
        return partial[index_++];
    }
    CHECK(index_ < LND_COUNTOF(suspended));
    return index_ < LND_COUNTOF(suspended) ? suspended[index_++] : -ENODEV;
}

static snd_pcm_sframes_t mock_read(snd_pcm_t *pcm, void *data, snd_pcm_uframes_t count) {
    static const int values[] = {2, -EAGAIN, -EPIPE, 1, -ENODEV};
    CHECK(index_ < LND_COUNTOF(values));
    int n = index_ < LND_COUNTOF(values) ? values[index_++] : -ENODEV;
    if (n > 0) memset(data, 0, (size_t)n * sizeof(float));
    return n;
}

static void process(void *user, void *data, uint64_t frames) {
    callback_count++;
    if (current->capture)
        received += frames;
    else {
        CHECK(frames == 4);
        for (uint32_t i = 0; i < frames; i++)
            ((float *)data)[i] = (float)i;
    }
}

int main(void) {
    lnd_stream stream = {.cfg = {.channels = 1, .sample_rate_hz = 48000, .format = LND_FORMAT_F32, .period_frames = 4}, .proc = process};
    float buffer[4];
    stream.buffer = buffer;
    current = &stream;
    CHECK(lnd_event_init(&stream.stop) == LND_OK);
    scenario = 1;
    lnd_alsa_thread(&stream);
    CHECK(lnd_load(&stream.failed) && index_ == 7 && prepared == 1 && waits == 1 && callback_count == 3);
    scenario = 2;
    index_ = callback_count = prepared = resumed = waits = 0;
    lnd_store(&stream.failed, 0);
    lnd_alsa_thread(&stream);
    CHECK(lnd_load(&stream.failed) && index_ == 4 && resumed == 2 && prepared == 0);
    stream.capture = true;
    index_ = callback_count = prepared = resumed = waits = started = 0;
    lnd_store(&stream.failed, 0);
    lnd_alsa_thread(&stream);
    CHECK(lnd_load(&stream.failed) && index_ == 5 && prepared == 1 && started == 1 && callback_count == 2 && received == 3);
    index_ = 0;
    lnd_event_signal(&stream.stop);
    lnd_alsa_thread(&stream);
    CHECK(index_ == 0);
    lnd_event_free(&stream.stop);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
