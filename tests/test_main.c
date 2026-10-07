#include "lindar_null.h"
#include "lindar_files.h"
#include "lindar_pcm_float.h"
#include "lindar_sink.h"
#include "lindar_buffers.h"
#include "lindar_codecs.h"
#include "lindar_devices.h"
#include "lindar_graph.h"
#include "lindar_io.h"
#include "lindar_output.h"
#include "lindar.h"

#include "io/devices/capture.h"
#include "io/reader.h"
#include "playback/graph/ring.h"
#include "playback/graph/sound.h"
#include "lnd_modules.h"
#if LND_MODULE_LOG
#include "lindar_log.h"
#endif
#if LND_MODULE_DSP
#include "lindar_dsp.h"
#endif
#include "pcm/audio/channels.h"
#include "pcm/audio/convert.h"
#include "pcm/audio/simd.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <unistd.h>
static void sleep_ms(unsigned ms) { usleep(ms * 1000); }
#endif

#define TWO_PI 6.283185307179586

static int checks;
static int failures;

#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #x);                                                                                               \
        }                                                                                                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(fabs((double)(a) - (double)(b)) <= (tol))

#if LND_MODULE_LOG
static void log_proc(void *user, int32_t level, const char *msg) {
    (void)user;
    printf("  [log %d] %s\n", level, msg);
}
#endif

typedef struct capture {
    float *data;
    size_t frames;
    size_t cap;
    uint32_t channels;
} capture;

static capture cap;

static void capture_proc(void *user, const void *data, uint64_t frames) {
    capture *c = user;
    const float *in = data;
    if (c->frames == 0) {
        uint64_t skip = 0;
        while (skip < frames && fabsf(in[skip * c->channels]) < 1e-4f)
            skip++;
        if (skip == frames) return;
        in += skip * c->channels;
        frames -= skip;
    }
    if (c->frames + frames > c->cap) return;
    memcpy(c->data + c->frames * c->channels, in, (size_t)frames * c->channels * sizeof(float));
    c->frames += (size_t)frames;
}

static void capture_reset(uint32_t channels, size_t max_frames) {
    free(cap.data);
    cap.data = calloc(max_frames * channels, sizeof(float));
    cap.frames = 0;
    cap.cap = max_frames;
    cap.channels = channels;
}

typedef struct stats {
    double rms;
    double peak;
    double freq;
} stats;

static stats analyze(const float *data, size_t frames, uint32_t channels, uint32_t channel, uint32_t sample_rate_hz) {
    stats s = {0};
    double sum = 0.0;
    size_t first = 0, last = 0, crossings = 0;
    for (size_t i = 0; i < frames; i++) {
        double v = data[i * channels + channel];
        sum += v * v;
        if (fabs(v) > s.peak) s.peak = fabs(v);
        if (i > 0 && data[(i - 1) * channels + channel] < 0.0f && v >= 0.0) {
            if (!crossings) first = i;
            last = i;
            crossings++;
        }
    }
    s.rms = frames ? sqrt(sum / (double)frames) : 0.0;
    if (crossings > 2) s.freq = (double)(crossings - 1) * (double)sample_rate_hz / (double)(last - first);
    return s;
}

static size_t capture_start(void) {
    size_t start = 0;
    while (start < cap.frames && fabsf(cap.data[start * cap.channels]) < 1e-4f)
        start++;
    return start;
}

static void fill_sine_s16(int16_t *pcm, uint64_t frames, uint32_t sample_rate_hz, double freq, double amplitude) {
    for (uint64_t i = 0; i < frames; i++)
        pcm[i] = (int16_t)(sin(TWO_PI * freq * (double)i / (double)sample_rate_hz) * amplitude * 32767.0);
}

static LND_BUFFER *make_sine(uint32_t sample_rate_hz, uint32_t channels, double freq, double amplitude, double seconds) {
    uint64_t frames = (uint64_t)(sample_rate_hz * seconds);
    LND_BUFFER *b = LND_BufferCreate(LND_FORMAT_F32, channels, sample_rate_hz, frames);
    float *p = LND_BufferGetData(b);
    for (uint64_t i = 0; i < frames; i++) {
        float v = (float)(sin(TWO_PI * freq * (double)i / (double)sample_rate_hz) * amplitude);
        for (uint32_t c = 0; c < channels; c++)
            p[i * channels + c] = v;
    }
    return b;
}

static void wait_state(LND_SOUND *s, int32_t state, unsigned timeout_ms) {
    for (unsigned t = 0; t < timeout_ms && LND_SoundGetState(s) != state; t += 2)
        sleep_ms(2);
}

static void setup_null(bool realtime, uint32_t channels, size_t max_frames) {
#if LND_MODULE_LOG
    LND_LogSetCallback(log_proc, nullptr);
    LND_ConfigSet(LND_CFG_LOG_LEVEL, LND_LOG_WARN);
#endif
    LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("null"));
    LND_ConfigSet(LND_CFG_NULL_REALTIME, realtime);
    capture_reset(channels, max_frames);
    LND_NullSetOutputCallback(capture_proc, &cap);
}

static void test_config(void) {
    printf("config\n");
    uint32_t count = LND_DeviceBackendGetCount();
    CHECK(count > 0);
    CHECK(!LND_DeviceBackendGet(count));
    CHECK(!LND_DeviceBackendGet(UINT32_MAX));
    CHECK(!LND_DeviceBackendFind(nullptr));
    CHECK(!LND_DeviceBackendFind("missing"));
    CHECK(!LND_DeviceBackendGetName(nullptr));
    CHECK(LND_DeviceSetPreferredBackend((const LND_DEVICE_BACKEND *)(uintptr_t)1) == LND_ERR_INVALID_ARG);
    for (uint32_t i = 0; i < count; i++) {
        const LND_DEVICE_BACKEND *backend = LND_DeviceBackendGet(i);
        const char *name = LND_DeviceBackendGetName(backend);
        CHECK(name && LND_DeviceBackendFind(name) == backend);
    }
    CHECK(LND_DeviceSetPreferredBackend(nullptr) == LND_OK);
    CHECK(!LND_DeviceGetPreferredBackend());
    CHECK(LND_DeviceBackendFind("null") != nullptr);
    CHECK(LND_ConfigGet(LND_CFG_GRAPH_BUFFER_FRAMES) == 4096);
    CHECK(LND_ConfigGet(LND_CFG_DEVICES_PERIODS) == 2);
#if LND_MODULE_LOG
    CHECK(LND_ConfigGet(LND_CFG_LOG_LEVEL) == LND_LOG_WARN);
#endif
    CHECK(LND_ConfigGet(LND_CFG_AUDIO_RESAMPLE_QUALITY) == 2);
    CHECK(LND_ConfigSet(nullptr, 0) == LND_ERR_INVALID_ARG);
    CHECK(LND_ConfigSet((LND_CONFIG_KEY *)((uintptr_t)LND_CFG_RUN_MODE + 1), 0) == LND_ERR_INVALID_ARG);
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_PERIODS, 0) == LND_ERR_INVALID_ARG);
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_PERIODS, 4) == LND_OK);
    CHECK(LND_ConfigGet(LND_CFG_DEVICES_PERIODS) == 4);
    CHECK(LND_ConfigSet(LND_CFG_BUFFERS_ALIGN_BYTES, 48) == LND_ERR_INVALID_ARG);
#if LND_MODULE_LOG
    CHECK(LND_ConfigSet(LND_CFG_LOG_LEVEL, 99) == LND_ERR_INVALID_ARG);
#endif
    CHECK(LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("null")) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0) == LND_OK);
    CHECK(LND_LibraryInit() == LND_OK);
    CHECK(LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("null")) == LND_ERR_STATE);
    CHECK(LND_ConfigSet(LND_CFG_BUFFERS_ALIGN_BYTES, 32) == LND_ERR_STATE);
    CHECK(LND_ConfigSet(LND_CFG_GRAPH_SAMPLE_RATE_HZ, 44100) == LND_OK);
    CHECK(LND_DeviceEnsureOutputInstance() == nullptr);
    CHECK(LND_ErrorGetLast() == LND_ERR_NO_DEVICE);
    LND_LibraryFree();
    CHECK(LND_ConfigGet(LND_CFG_DEVICES_PERIODS) == 2);
    CHECK(LND_ConfigGet(LND_CFG_DEVICES_AUTO_OPEN) == 1);
    CHECK(!LND_DeviceGetPreferredBackend());
    CHECK(LND_DeviceBackendFind("null") != nullptr);
}

static void test_ring(void) {
    printf("ring\n");
    lnd_ring r;
    CHECK(lnd_ring_init(&r, 4) == LND_OK);
    lnd_cmd c = {0};
    for (uint32_t i = 0; i < 4; i++) {
        c.u64 = i;
        CHECK(lnd_ring_push(&r, &c) == LND_OK);
    }
    CHECK(lnd_ring_push(&r, &c) == LND_ERR_BUSY);
    CHECK(lnd_ring_count(&r) == 4);
    for (uint32_t i = 0; i < 4; i++) {
        CHECK(lnd_ring_pop(&r, &c));
        CHECK(c.u64 == i);
    }
    CHECK(!lnd_ring_pop(&r, &c));
    for (uint32_t i = 0; i < 1000; i++) {
        c.u64 = i;
        CHECK(lnd_ring_push(&r, &c) == LND_OK);
        CHECK(lnd_ring_pop(&r, &c));
        CHECK(c.u64 == i);
    }
    lnd_ring_free(&r);
}

static void test_convert(void) {
    printf("convert\n");
    enum { N = 4096 };
    static float in[N], out[N];
    static unsigned char raw[N * 8];
    for (int i = 0; i < N; i++)
        in[i] = (float)sin(TWO_PI * (double)i / 97.0) * (i % 7 == 0 ? 1.0f : 0.8f);
    in[0] = 1.0f;
    in[1] = -1.0f;
    in[2] = 0.0f;
    in[3] = 1.5f;
    in[4] = -1.5f;
    const struct {
        int32_t format;
        double tol;
    } cases[] = {
        {LND_FORMAT_U8, 1.0 / 128.0},      {LND_FORMAT_S16, 1.0 / 32768.0}, {LND_FORMAT_S24, 1.0 / 8388608.0},
        {LND_FORMAT_S32, 1.0 / 8388608.0}, {LND_FORMAT_F32, 0.0},           {LND_FORMAT_F64, 0.0},
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        lnd_pcm_from_f32(cases[k].format, in, raw, N);
        lnd_pcm_to_f32(cases[k].format, raw, out, N);
        double max_err = 0.0;
        bool is_float = cases[k].format == LND_FORMAT_F32 || cases[k].format == LND_FORMAT_F64;
        for (int i = 0; i < N; i++) {
            double expect = is_float ? in[i] : (in[i] < -1.0f ? -1.0 : (in[i] > 1.0f ? 1.0 : in[i]));
            if (expect == 1.0 && !is_float) expect = 1.0 - cases[k].tol;
            double err = fabs(out[i] - expect);
            if (err > max_err) max_err = err;
        }
        CHECK(max_err <= cases[k].tol + 1e-7);
        if (max_err > cases[k].tol + 1e-7) printf("  format %d max_err %g\n", cases[k].format, max_err);
    }
    int16_t s16[4] = {32767, -32768, 0, 16384};
    lnd_pcm_to_f32(LND_FORMAT_S16, s16, out, 4);
    CHECK_NEAR(out[0], 32767.0 / 32768.0, 1e-7);
    CHECK_NEAR(out[1], -1.0, 1e-7);
    CHECK_NEAR(out[3], 0.5, 1e-7);
    unsigned char s24[3] = {0x00, 0x00, 0x80};
    lnd_pcm_to_f32(LND_FORMAT_S24, s24, out, 1);
    CHECK_NEAR(out[0], -1.0, 1e-7);
}

static void test_simd(void) {
    printf("simd\n");
    lnd_simd_init(LND_SIMD_NONE);
    lnd_simd_ops scalar = lnd_simd;
    lnd_simd_init(LND_SIMD_AUTO);
    printf("  kernels: %s\n", lnd_simd.name);
    enum { N = 1003 };
    static float a[N], b[N], ra[N], rb[N];
    static int16_t sa[N], sb[N];
    for (int i = 0; i < N; i++) {
        a[i] = (float)sin(i * 0.37) * 1.3f;
        b[i] = (float)cos(i * 0.11);
        sa[i] = (int16_t)(i * 37 - 16000);
    }
    memcpy(ra, a, sizeof a);
    memcpy(rb, a, sizeof a);
    scalar.accumulate(ra, b, 0.7f, N);
    lnd_simd.accumulate(rb, b, 0.7f, N);
    double err = 0.0;
    for (int i = 0; i < N; i++)
        err = fmax(err, fabs(ra[i] - rb[i]));
    CHECK(err < 1e-6);
    memcpy(ra, a, sizeof a);
    memcpy(rb, a, sizeof a);
    scalar.scale(ra, 0.3f, N);
    lnd_simd.scale(rb, 0.3f, N);
    for (int i = 0; i < N; i++)
        err = fmax(err, fabs(ra[i] - rb[i]));
    CHECK(err < 1e-6);
    memcpy(ra, a, sizeof a);
    memcpy(rb, a, sizeof a);
    scalar.clip_hard(ra, N);
    lnd_simd.clip_hard(rb, N);
    CHECK(memcmp(ra, rb, sizeof ra) == 0);
    memcpy(ra, a, sizeof a);
    memcpy(rb, a, sizeof a);
    scalar.clip_soft(ra, N);
    lnd_simd.clip_soft(rb, N);
    for (int i = 0; i < N; i++)
        err = fmax(err, fabs(ra[i] - rb[i]));
    CHECK(err < 1e-6);
    for (int i = 0; i < N; i++)
        CHECK(fabsf(rb[i]) <= 1.0f);
    scalar.s16_to_f32(sa, ra, N);
    lnd_simd.s16_to_f32(sa, rb, N);
    CHECK(memcmp(ra, rb, sizeof ra) == 0);
    scalar.f32_to_s16(a, sa, N);
    lnd_simd.f32_to_s16(a, sb, N);
    int max_lsb = 0;
    for (int i = 0; i < N; i++)
        max_lsb = (int)fmax(max_lsb, abs(sa[i] - sb[i]));
    CHECK(max_lsb <= 1);
    float ramp[64];
    for (int i = 0; i < 64; i++)
        ramp[i] = -2.0f + (float)i * (4.0f / 63.0f);
    lnd_simd.clip_soft(ramp, 64);
    for (int i = 1; i < 64; i++)
        CHECK(ramp[i] >= ramp[i - 1]);
    CHECK(ramp[0] > -1.0f && ramp[63] < 1.0f && ramp[63] > 0.9f);
}

static void test_channels(void) {
    printf("channels\n");
    float mono[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float stereo[8];
    lnd_channels_map(mono, 1, stereo, 2, 4);
    CHECK(stereo[0] == 0.1f && stereo[1] == 0.1f && stereo[6] == 0.4f && stereo[7] == 0.4f);
    float back[4];
    lnd_channels_map(stereo, 2, back, 1, 4);
    CHECK_NEAR(back[2], 0.3, 1e-6);
    float m[64];
    lnd_channels_matrix(6, 2, LND_CHANNEL_MIX_MATRIX, m);
    CHECK(m[0 * 6 + 0] == 1.0f);
    CHECK_NEAR(m[0 * 6 + 2], 0.70710678, 1e-6);
    CHECK_NEAR(m[0 * 6 + 4], 0.70710678, 1e-6);
    CHECK(m[0 * 6 + 1] == 0.0f && m[0 * 6 + 3] == 0.0f && m[0 * 6 + 5] == 0.0f);
    CHECK_NEAR(m[1 * 6 + 1], 1.0, 1e-6);
    CHECK_NEAR(m[1 * 6 + 5], 0.70710678, 1e-6);
    lnd_channels_matrix(6, 1, LND_CHANNEL_MIX_MATRIX, m);
    CHECK_NEAR(m[0], 0.70710678, 1e-6);
    CHECK_NEAR(m[2], 1.0, 1e-6);
    CHECK_NEAR(m[4], 0.5, 1e-6);
    CHECK(m[3] == 0.0f);
    lnd_channels_matrix(4, 2, LND_CHANNEL_MIX_MATRIX, m);
    CHECK(m[0 * 4 + 0] == 1.0f && m[0 * 4 + 2] == 0.5f && m[1 * 4 + 1] == 1.0f && m[1 * 4 + 3] == 0.5f);
    lnd_channels_matrix(4, 1, LND_CHANNEL_MIX_MATRIX, m);
    CHECK(m[0] == 0.25f && m[1] == 0.25f && m[2] == 0.25f && m[3] == 0.25f);
    lnd_channels_matrix(2, 6, LND_CHANNEL_MIX_MATRIX, m);
    CHECK(m[0 * 2 + 0] == 1.0f && m[1 * 2 + 1] == 1.0f && m[2 * 2 + 0] == 0.0f && m[3 * 2 + 1] == 0.0f);
    lnd_channels_matrix(1, 2, LND_CHANNEL_MIX_MATRIX, m);
    CHECK(m[0] == 1.0f && m[1] == 1.0f);
    lnd_channels_matrix(1, 6, LND_CHANNEL_MIX_MATRIX, m);
    CHECK(m[0] == 0.0f && m[1] == 0.0f && m[2] == 1.0f && m[3] == 0.0f);
    lnd_channels_matrix(2, 1, LND_CHANNEL_MIX_MATRIX, m);
    CHECK(m[0] == 0.5f && m[1] == 0.5f);
    lnd_channels_matrix(2, 4, LND_CHANNEL_MIX_SIMPLE, m);
    CHECK(m[0] == 1.0f && m[3] == 1.0f && m[4] == 0.0f);
    float six[12] = {1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f, 0, 0, 0, 0, 0, 0};
    float two[4];
    lnd_channels_matrix(6, 2, LND_CHANNEL_MIX_MATRIX, m);
    lnd_channels_apply(m, six, 6, two, 2, 2);
    CHECK_NEAR(two[0], 1.0 + 0.70710678 * 0.25 + 0.70710678 * 0.0625, 1e-5);
    CHECK_NEAR(two[1], 0.5 + 0.70710678 * 0.25 + 0.70710678 * 0.03125, 1e-5);
    float weights[2] = {0.5f, -0.25f};
    float input[66], mapped[66];
    for (unsigned i = 0; i < 66; i++) input[i] = (float)((int)i - 16) / 64;
    for (unsigned frames = 0; frames <= 32; frames++) {
        for (unsigned i = 0; i < 66; i++) mapped[i] = -7;
        lnd_channels_apply(weights, input, 1, mapped + 1, 2, frames);
        CHECK(mapped[0] == -7 && mapped[frames * 2 + 1] == -7);
        for (unsigned i = 0; i < frames; i++) {
            CHECK(mapped[1 + 2 * i] == input[i] * weights[0]);
            CHECK(mapped[2 + 2 * i] == input[i] * weights[1]);
        }
        for (unsigned i = 0; i < 66; i++) mapped[i] = -7;
        lnd_channels_apply(weights, input, 2, mapped + 1, 1, frames);
        CHECK(mapped[0] == -7 && mapped[frames + 1] == -7);
        for (unsigned i = 0; i < frames; i++) CHECK(mapped[i + 1] == input[2 * i] * weights[0] + input[2 * i + 1] * weights[1]);
    }
    CHECK(two[2] == 0.0f && two[3] == 0.0f);
}

static void test_buffers(void) {
    printf("buffers\n");
    LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("null"));
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    CHECK(LND_BufferCreate(LND_FORMAT_NONE, 1, 48000, 10) == nullptr);
    CHECK(LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    LND_BUFFER *a = LND_BufferCreate(LND_FORMAT_S16, 2, 48000, 1000);
    CHECK(a != nullptr);
    CHECK(LND_BufferGetBytes(a) == 4000);
    void *data = LND_BufferGetData(a);
    CHECK(((uintptr_t)data & 63) == 0);
    LND_BufferFree(a);
    LND_BUFFER *b = LND_BufferCreate(LND_FORMAT_F32, 1, 48000, 1000);
    CHECK(LND_BufferGetData(b) == data);
    LND_BUFFER *c = LND_BufferReuse(b, LND_FORMAT_S16, 1, 44100, 500);
    CHECK(c == b && LND_BufferGetData(c) == data);
    LND_BUFFER *d = LND_BufferReuse(c, LND_FORMAT_F32, 2, 48000, 100000);
    CHECK(d == c && LND_BufferGetData(d) != data);
    LND_SOURCE *s = LND_SourceCreateBuffer(d);
    CHECK(s != nullptr);
    LND_BUFFER *e = LND_BufferReuse(d, LND_FORMAT_F32, 2, 48000, 100);
    CHECK(e != d);
    LND_BufferFree(e);
    CHECK(LND_SourceFree(s) == LND_OK);
    LND_LibraryFree();
}

static void test_playback_buffer(void) {
    printf("playback buffer\n");
    setup_null(false, 2, 48000 * 4);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_DEVICE_INSTANCE *inst = LND_DeviceEnsureOutputInstance();
    CHECK(inst != nullptr);
    uint32_t sample_rate_hz = LND_DeviceInstanceGetSampleRateHz(inst);
    CHECK(sample_rate_hz == 48000 && LND_DeviceInstanceGetChannels(inst) == 2 && LND_DeviceInstanceGetFormat(inst) == LND_FORMAT_F32);
    CHECK(LND_DeviceInstanceGetLatencyFrames(inst) == LND_DeviceInstanceGetBufferFrames(inst));
    CHECK(LND_DeviceInstanceIsRunning(inst));
    LND_NODE *dest = LND_DeviceInstanceGetNode(inst);
    CHECK(LND_NodeGetDeviceInstance(dest) == inst && LND_NodeGetType(dest) == LND_NODE_DESTINATION);
    CHECK(LND_DeviceEnsureOutputNode() == dest);
    uint64_t frames = sample_rate_hz / 2;
    LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_S16, 1, sample_rate_hz, frames);
    fill_sine_s16(LND_BufferGetData(buffer), frames, sample_rate_hz, 440.0, 0.5);
    LND_SOURCE *src = LND_SourceCreateBuffer(buffer);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(s != nullptr && LND_SourceEnsureSound(src, nullptr) == s);
    CHECK(LND_SoundGetSource(s) == src);
    CHECK(LND_NodeEnsureSound(LND_SourceEnsureNode(src), nullptr) == s);
    CHECK(LND_NodeGetType(LND_SoundEnsureNode(s)) == LND_NODE_SOURCE);
    CHECK(LND_SoundGetLengthFrames(s) == frames);
    CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
    CHECK(LND_SoundPlay(s) == LND_OK);
    CHECK(LND_SoundGetState(s) == LND_SOUND_PLAYING);
    CHECK(LND_SoundGetOutput(s) == dest);
    CHECK(LND_NodeGetInputCount(dest) == 1 && LND_NodeGetInput(dest, 0) == LND_SoundEnsureNode(s));
    wait_state(s, LND_SOUND_STOPPED, 5000);
    CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
    CHECK(LND_SoundGetPositionFrames(s) == frames);
    CHECK(LND_SourceFree(src) == LND_OK);
    CHECK(LND_NodeGetInputCount(dest) == 0);
    LND_BufferFree(buffer);
    LND_LibraryFree();
    size_t start = capture_start();
    size_t skip = 1024;
    size_t usable = frames - skip * 2;
    CHECK(cap.frames >= start + skip + usable);
    if (cap.frames >= start + skip + usable) {
        stats l = analyze(cap.data + (start + skip) * 2, usable, 2, 0, sample_rate_hz);
        stats r = analyze(cap.data + (start + skip) * 2, usable, 2, 1, sample_rate_hz);
        CHECK_NEAR(l.rms, 0.5 / sqrt(2.0), 0.02);
        CHECK_NEAR(l.freq, 440.0, 2.0);
        CHECK_NEAR(r.rms, l.rms, 1e-6);
        CHECK(l.peak <= 0.5001);
        size_t tail = start + frames + 1024;
        if (tail + 4096 <= cap.frames) {
            stats t = analyze(cap.data + tail * 2, 4096, 2, 0, sample_rate_hz);
            CHECK(t.peak == 0.0);
        }
    }
}

typedef struct gen {
    double phase;
    double step;
    uint64_t pos;
    uint64_t length;
} gen;

static int64_t gen_read(void *user, void *dst, uint64_t frames) {
    gen *g = user;
    float *out = dst;
    uint64_t n = g->pos + frames > g->length ? g->length - g->pos : frames;
    for (uint64_t i = 0; i < n; i++) {
        float v = (float)(sin(g->phase) * 0.25);
        out[i * 2] = v;
        out[i * 2 + 1] = -v;
        g->phase += g->step;
    }
    g->pos += n;
    return n;
}

static int32_t gen_seek(void *user, uint64_t frame) {
    gen *g = user;
    g->pos = frame;
    g->phase = (double)frame * g->step;
    return LND_OK;
}

static void test_playback_proc(uint32_t flags) {
    printf("playback proc %s\n", flags & LND_GRAPH_SOURCE_DIRECT ? "direct" : "streamed");
    setup_null(true, 2, 48000 * 4);
    CHECK(LND_LibraryInit() == LND_OK);
    uint32_t sample_rate_hz = 48000;
    uint64_t frames = sample_rate_hz / 2;
    gen g = {.step = TWO_PI * 440.0 / sample_rate_hz, .length = frames};
    LND_SOURCE_PROCS procs = {.read = gen_read, .seek = gen_seek, .length_frames = frames};
    LND_SOURCE *src = LND_SourceCreateProc(&procs, &g, LND_FORMAT_F32, 2, sample_rate_hz, flags);
    CHECK(src != nullptr);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(LND_SoundPlay(s) == LND_OK);
    wait_state(s, LND_SOUND_STOPPED, 5000);
    CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
    LND_SourceFree(src);
    LND_LibraryFree();
    size_t start = capture_start();
    size_t skip = 1024;
    size_t usable = frames - skip * 2;
    CHECK(cap.frames >= start + skip + usable);
    if (cap.frames >= start + skip + usable) {
        stats l = analyze(cap.data + (start + skip) * 2, usable, 2, 0, sample_rate_hz);
        stats r = analyze(cap.data + (start + skip) * 2, usable, 2, 1, sample_rate_hz);
        CHECK_NEAR(l.rms, 0.25 / sqrt(2.0), 0.01);
        CHECK_NEAR(l.freq, 440.0, 2.0);
        CHECK_NEAR(r.rms, l.rms, 1e-6);
    }
}

static void test_resample(uint32_t quality, uint32_t src_rate) {
    printf("resample quality %u from %u\n", quality, src_rate);
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    LND_ConfigSet(LND_CFG_AUDIO_RESAMPLE_QUALITY, quality);
    capture_reset(2, 48000 * 4);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_BUFFER *buffer = make_sine(src_rate, 2, 440.0, 0.5, 0.5);
    uint64_t frames = LND_BufferGetFrames(buffer);
    LND_SOURCE *src = LND_SourceCreateBuffer(buffer);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    LND_NODE *mixer = LND_NodeCreateMixer(2, 48000, LND_MIX_AVAILABLE | LND_MIX_END);
    CHECK(mixer && LND_SoundSetOutput(s, mixer) == LND_OK);
    LND_RENDERER *renderer = LND_RendererCreateNode(mixer);
    CHECK(renderer);
    CHECK(LND_SoundPlay(s) == LND_OK);
    LND_PCM pcm = {.data = cap.data, .frames = cap.cap, .channels = 2, .format = LND_FORMAT_F32};
    int64_t got = LND_RendererReadPcm(renderer, &pcm, 0, cap.cap);
    CHECK(got > 0 && (uint64_t)got < cap.cap);
    cap.frames = got > 0 ? (size_t)got : 0;
    CHECK(LND_RendererReadPcm(renderer, &pcm, cap.cap - 1, 1) == 0);
    CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
    CHECK(LND_RendererFree(renderer) == LND_OK);
    LND_SourceFree(src);
    CHECK(LND_NodeFree(mixer) == LND_OK);
    LND_BufferFree(buffer);
    LND_LibraryFree();
    size_t start = capture_start();
    size_t out_frames = (size_t)((double)frames * 48000.0 / (double)src_rate);
    size_t skip = 2048;
    CHECK(cap.frames + 1 >= out_frames && cap.frames <= out_frames + 1);
    CHECK(cap.frames >= start + out_frames - skip);
    if (cap.frames >= start + out_frames - skip) {
        stats l = analyze(cap.data + (start + skip) * 2, out_frames - skip * 2, 2, 0, 48000);
        CHECK_NEAR(l.freq, 440.0, 1.5);
        CHECK_NEAR(l.rms, 0.5 / sqrt(2.0), 0.01);
        double max_step = 0.0;
        for (size_t i = start + skip + 1; i < start + out_frames - skip; i++)
            max_step = fmax(max_step, fabs(cap.data[i * 2] - cap.data[(i - 1) * 2]));
        CHECK(max_step < 0.5 * TWO_PI * 440.0 / 48000.0 * 1.2);
    }
}

static void test_control(void) {
    printf("control\n");
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_PERIOD_FRAMES, 240);
    CHECK(LND_LibraryInit() == LND_OK);
    uint32_t sample_rate_hz = 48000;
    uint64_t frames = sample_rate_hz * 4;
    LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_F32, 2, sample_rate_hz, frames);
    memset(LND_BufferGetData(buffer), 0, LND_BufferGetBytes(buffer));
    LND_SOURCE *src = LND_SourceCreateBuffer(buffer);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(LND_SoundSeekSeconds(s, 1.0f) == LND_OK);
    CHECK(LND_SoundGetPositionFrames(s) == sample_rate_hz);
    CHECK(LND_SourceGetPositionFrames(src) == sample_rate_hz);
    CHECK(LND_SoundPlay(s) == LND_OK);
    sleep_ms(120);
    uint64_t p1 = LND_SoundGetPositionFrames(s);
    CHECK(p1 > sample_rate_hz && p1 < sample_rate_hz + sample_rate_hz / 2);
    CHECK(LND_SoundSetPause(s, true) == LND_OK);
    CHECK(LND_SoundGetState(s) == LND_SOUND_PAUSED);
    sleep_ms(60);
    uint64_t p2 = LND_SoundGetPositionFrames(s);
    sleep_ms(60);
    uint64_t p3 = LND_SoundGetPositionFrames(s);
    CHECK(p2 == p3);
    CHECK(LND_SoundSeekSeconds(s, 2.0f) == LND_OK);
    CHECK(LND_SoundSetPause(s, false) == LND_OK);
    CHECK(LND_SoundGetState(s) == LND_SOUND_PLAYING);
    sleep_ms(120);
    uint64_t p4 = LND_SoundGetPositionFrames(s);
    CHECK(p4 > sample_rate_hz * 2 && p4 < sample_rate_hz * 2 + sample_rate_hz / 2);
    LND_ConfigSet(LND_CFG_GRAPH_POSITION_COMPENSATE, 1);
    uint64_t p5 = LND_SoundGetPositionFrames(s);
    LND_ConfigSet(LND_CFG_GRAPH_POSITION_COMPENSATE, 0);
    uint64_t p6 = LND_SoundGetPositionFrames(s);
    CHECK(p5 < p6 && p6 - p5 <= LND_DeviceInstanceGetLatencyFrames(LND_DeviceEnsureOutputInstance()) + 480);
    CHECK(LND_SoundPlay(s) == LND_OK);
    CHECK(LND_SoundSetGain(s, 0.5f) == LND_OK);
    CHECK(LND_NodeGetGain(LND_SoundEnsureNode(s)) == 0.5f);
    CHECK(LND_SoundSetLoop(s, true) == LND_OK);
    CHECK(LND_SoundGetLoop(s));
    CHECK(LND_SoundStop(s) == LND_OK);
    CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
    CHECK(LND_SoundGetPositionFrames(s) == 0);
    sleep_ms(40);
    CHECK(LND_SoundGetPositionFrames(s) == 0);
    CHECK(LND_SoundPlay(s) == LND_OK);
    sleep_ms(60);
    CHECK(LND_SoundGetPositionFrames(s) > 0);
    LND_SourceFree(src);
    LND_BufferFree(buffer);
    LND_LibraryFree();
}

static void test_loop_and_end(void) {
    printf("loop\n");
    setup_null(true, 2, 48000 * 2);
    CHECK(LND_LibraryInit() == LND_OK);
    uint32_t sample_rate_hz = 48000;
    LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_F32, 2, sample_rate_hz, sample_rate_hz / 20);
    memset(LND_BufferGetData(buffer), 0, LND_BufferGetBytes(buffer));
    LND_SOURCE *src = LND_SourceCreateBuffer(buffer);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(LND_SoundSetLoop(s, true) == LND_OK);
    CHECK(LND_SoundPlay(s) == LND_OK);
    sleep_ms(200);
    CHECK(LND_SoundGetState(s) == LND_SOUND_PLAYING);
    CHECK(LND_SoundSetLoop(s, false) == LND_OK);
    wait_state(s, LND_SOUND_STOPPED, 2000);
    CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
    LND_SourceFree(src);
    LND_BufferFree(buffer);
    LND_LibraryFree();
}

static void test_instances(void) {
    printf("instances\n");
    setup_null(true, 2, 48000 * 2);
    CHECK(LND_DeviceGetCount(LND_DEVICE_OUTPUT) == 1);
    CHECK(LND_DeviceGetCount(LND_DEVICE_INPUT) == 1);
    LND_DEVICE *d = LND_DeviceGet(LND_DEVICE_OUTPUT, 0);
    CHECK(d != nullptr);
    CHECK(LND_DeviceGetDefault(LND_DEVICE_OUTPUT) == d);
    CHECK(LND_DeviceFind(LND_DEVICE_OUTPUT, "null:output") == d);
    CHECK(LND_DeviceFind(LND_DEVICE_OUTPUT, "nope") == nullptr);
    CHECK(LND_DeviceIsDefault(d));
    CHECK(LND_DeviceGetFlags(d) & LND_DEVICE_FLAG_MULTI_INSTANCE);
    CHECK(LND_DeviceGetModeCount(d) == 3);
    CHECK(LND_DeviceInstanceOpen(d) == nullptr);
    CHECK(LND_ErrorGetLast() == LND_ERR_STATE);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_DEVICE_INSTANCE *a = LND_DeviceEnsureOutputInstance();
    CHECK(a != nullptr);
    CHECK(LND_DeviceGetInstanceCount(d) == 1);
    CHECK(LND_DeviceGetInstance(d, 0) == a);
    LND_ConfigSet(LND_CFG_GRAPH_SAMPLE_RATE_HZ, 44100);
    LND_DEVICE_INSTANCE *b = LND_DeviceInstanceOpen(d);
    CHECK(b != nullptr && b != a);
    CHECK(LND_DeviceInstanceGetSampleRateHz(b) == 44100);
    CHECK(LND_DeviceInstanceGetSampleRateHz(a) == 48000);
    CHECK(LND_NodeGetSampleRateHz(LND_DeviceInstanceGetNode(b)) == 44100);
    CHECK(LND_DeviceGetInstanceCount(d) == 2);
    LND_DEVICE *in = LND_DeviceGet(LND_DEVICE_INPUT, 0);
    CHECK(LND_DeviceInstanceOpen(in) == nullptr);
    CHECK(LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
    LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_F32, 2, 48000, 48000);
    memset(LND_BufferGetData(buffer), 0, LND_BufferGetBytes(buffer));
    LND_SOURCE *src = LND_SourceCreateBuffer(buffer);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    LND_NODE *nb = LND_DeviceInstanceGetNode(b);
    CHECK(LND_SoundSetOutput(s, nb) == LND_OK);
    CHECK(LND_SoundGetOutput(s) == nb);
    CHECK(LND_SoundPlay(s) == LND_OK);
    for (unsigned t = 0; t < 2000 && !LND_SoundGetPositionFrames(s); t += 2)
        sleep_ms(2);
    CHECK(LND_NodeGetInputCount(nb) == 1 && LND_NodeGetInputCount(LND_DeviceInstanceGetNode(a)) == 0);
    CHECK(LND_DeviceInstanceGetPositionFrames(b) > 0);
    CHECK(LND_SoundGetPositionFrames(s) > 0);
    CHECK(LND_SoundSetOutput(s, LND_DeviceInstanceGetNode(a)) == LND_OK);
    sleep_ms(30);
    CHECK(LND_NodeGetInputCount(nb) == 0 && LND_NodeGetInputCount(LND_DeviceInstanceGetNode(a)) == 1);
    CHECK(LND_SoundGetState(s) == LND_SOUND_PLAYING);
    CHECK(LND_SoundSetOutput(s, nb) == LND_OK);
    CHECK(LND_DeviceInstanceClose(b) == LND_OK);
    CHECK(LND_DeviceGetInstanceCount(d) == 1);
    CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
    CHECK(LND_SoundGetOutput(s) == nullptr);
    CHECK(LND_SoundPlay(s) == LND_OK);
    CHECK(LND_SoundGetOutput(s) == LND_DeviceInstanceGetNode(a));
    sleep_ms(30);
    LND_SourceFree(src);
    CHECK(LND_DeviceRefresh() == LND_OK);
    CHECK(LND_DeviceGet(LND_DEVICE_OUTPUT, 0) == d);
    CHECK(LND_DeviceGetInstanceCount(d) == 1);
    LND_BufferFree(buffer);
    LND_LibraryFree();
}

static void test_free_while_playing(void) {
    printf("free while playing\n");
    setup_null(true, 2, 48000 * 2);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_NODE *dest = LND_DeviceEnsureOutputNode();
    for (int i = 0; i < 20; i++) {
        LND_BUFFER *buffer = LND_BufferCreate(LND_FORMAT_S16, 1, 48000, 48000);
        fill_sine_s16(LND_BufferGetData(buffer), 48000, 48000, 440.0, 0.1);
        LND_SOURCE *src = LND_SourceCreateBuffer(buffer);
        LND_SoundPlay(LND_SourceEnsureSound(src, nullptr));
        if (i % 3 == 0) sleep_ms(5);
        LND_SourceFree(src);
        LND_BufferFree(buffer);
    }
    sleep_ms(50);
    CHECK(LND_NodeGetInputCount(dest) == 0);
    LND_LibraryFree();
}

static void test_graph_chain(void) {
    printf("graph chain\n");
    setup_null(false, 2, 48000 * 4);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_NODE *bus = LND_NodeCreateBus(0, 0);
    LND_NODE *master = LND_NodeCreateBus(2, 48000);
    CHECK(bus != nullptr && master != nullptr);
    CHECK(LND_NodeGetChannels(bus) == 2 && LND_NodeGetSampleRateHz(bus) == 48000 && LND_NodeGetType(bus) == LND_NODE_BUS);
    LND_BUFFER *b1 = make_sine(48000, 1, 440.0, 0.25, 0.5);
    LND_BUFFER *b2 = make_sine(48000, 2, 660.0, 0.25, 0.5);
    LND_SOURCE *s1 = LND_SourceCreateBuffer(b1);
    LND_SOURCE *s2 = LND_SourceCreateBuffer(b2);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(s1), bus) == LND_OK);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(s2), bus) == LND_OK);
    CHECK(LND_NodeConnect(bus, master) == LND_OK);
    CHECK(LND_NodeConnect(master, LND_DeviceEnsureOutputNode()) == LND_OK);
    CHECK(LND_NodeGetInputCount(bus) == 2 && LND_NodeGetOutputCount(bus) == 1 && LND_NodeGetOutput(bus, 0) == master);
    CHECK(LND_NodeSetGain(master, 0.5f) == LND_OK);
    CHECK(LND_NodeGetGain(master) == 0.5f);
    LND_SOUND *group = LND_NodeEnsureSound(bus, nullptr);
    CHECK(group != nullptr && LND_SoundEnsureNode(group) == bus);
    CHECK(LND_SoundGetSource(group) == nullptr);
    LND_SOURCE *view = LND_NodeEnsureSource(bus);
    CHECK(view && LND_SoundGetSource(group) == view);
    CHECK(LND_SoundGetLengthFrames(group) == 24000);
    CHECK(LND_SoundPlay(group) == LND_OK);
    CHECK(LND_SoundGetState(LND_SourceEnsureSound(s1, nullptr)) == LND_SOUND_PLAYING);
    CHECK(LND_SoundGetState(LND_SourceEnsureSound(s2, nullptr)) == LND_SOUND_PLAYING);
    CHECK(LND_NodeIsActive(bus) && LND_NodeIsActive(master));
    wait_state(LND_SourceEnsureSound(s1, nullptr), LND_SOUND_STOPPED, 5000);
    wait_state(LND_SourceEnsureSound(s2, nullptr), LND_SOUND_STOPPED, 5000);
    CHECK(LND_SoundGetState(group) == LND_SOUND_STOPPED);
    CHECK(LND_NodeGetPositionFrames(bus) >= 24000);
    CHECK(LND_NodeFree(LND_DeviceEnsureOutputNode()) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeFree(LND_SourceEnsureNode(s1)) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeFree(bus) == LND_OK);
    CHECK(LND_NodeGetInputCount(master) == 0);
    CHECK(LND_NodeFree(master) == LND_OK);
    LND_SourceFree(s1);
    LND_SourceFree(s2);
    LND_BufferFree(b1);
    LND_BufferFree(b2);
    LND_LibraryFree();
    size_t start = capture_start();
    size_t skip = 1024;
    size_t usable = 24000 - skip * 2;
    CHECK(cap.frames >= start + skip + usable);
    if (cap.frames >= start + skip + usable) {
        stats l = analyze(cap.data + (start + skip) * 2, usable, 2, 0, 48000);
        CHECK_NEAR(l.rms, 0.125, 0.01);
        CHECK(l.peak <= 0.2501 && l.peak > 0.2);
    }
}

static void test_graph_split(void) {
    printf("graph split\n");
    setup_null(true, 2, 48000 * 2);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_NODE *bus = LND_NodeCreateBus(2, 48000);
    LND_BUFFER *b = make_sine(48000, 2, 440.0, 0.5, 2.0);
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(LND_SoundSetOutput(s, bus) == LND_OK);
    CHECK(LND_NodeConnect(bus, LND_DeviceEnsureOutputNode()) == LND_OK);
    LND_SOURCE *tap = LND_NodeEnsureSource(bus);
    CHECK(tap != nullptr && LND_NodeEnsureSource(bus) == tap);
    CHECK(LND_SourceEnsureNode(tap) == bus && LND_SourceGetSampleRateHz(tap) == 48000 && LND_SourceGetChannels(tap) == 2);
    CHECK(LND_SourceFree(tap) == LND_ERR_INVALID_ARG);
    CHECK(LND_SourceSeekFrames(tap, 0) == LND_ERR_UNSUPPORTED);
    CHECK(LND_SoundPlay(s) == LND_OK);
    sleep_ms(30);
    enum { N = 4800 };
    static float buf[N * 2];
    uint64_t got = 0;
    for (int k = 0; k < 4; k++)
        got = LND_SourceRead(tap, buf, LND_FORMAT_F32, N);
    CHECK(got == N);
    stats st = analyze(buf, N, 2, 0, 48000);
    CHECK_NEAR(st.freq, 440.0, 3.0);
    CHECK_NEAR(st.rms, 0.5 / sqrt(2.0), 0.02);
    CHECK(LND_SourceGetPositionFrames(tap) >= N * 4);
    sleep_ms(30);
    CHECK(LND_SoundGetState(s) == LND_SOUND_PLAYING);
    LND_NODE *bus2 = LND_NodeCreateBus(2, 44100);
    CHECK(LND_NodeConnect(bus, bus2) == LND_OK);
    CHECK(LND_NodeGetOutputCount(bus) == 2);
    static float buf2[4410 * 2];
    CHECK(LND_SourceRead(LND_NodeEnsureSource(bus2), buf2, LND_FORMAT_F32, 4410) == 4410);
    stats st2 = analyze(buf2 + 2 * 512, 4410 - 1024, 2, 0, 44100);
    CHECK_NEAR(st2.freq, 440.0, 4.0);
    CHECK(LND_NodeFree(bus2) == LND_OK);
    CHECK(LND_NodeGetOutputCount(bus) == 1);
    LND_SourceFree(src);
    CHECK(LND_NodeFree(bus) == LND_OK);
    LND_BufferFree(b);
    LND_LibraryFree();
}

static void test_graph_loopback(void) {
    printf("graph loopback\n");
    setup_null(true, 2, 48000 * 2);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_NODE *master = LND_DeviceEnsureOutputNode();
    LND_BUFFER *b = make_sine(48000, 2, 440.0, 0.5, 2.0);
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(LND_SoundPlay(s) == LND_OK);
    sleep_ms(50);
    LND_SOURCE *tap = LND_NodeEnsureSource(master);
    enum { N = 9600 };
    static float buf[N * 2];
    CHECK(LND_SourceRead(tap, buf, LND_FORMAT_F32, N) == N);
    stats st = analyze(buf + 2 * 1024, N - 2048, 2, 0, 48000);
    CHECK_NEAR(st.freq, 440.0, 3.0);
    CHECK_NEAR(st.rms, 0.5 / sqrt(2.0), 0.03);
    static int16_t pcm[4800 * 2];
    CHECK(LND_SourceRead(tap, pcm, LND_FORMAT_S16, 4800) == 4800);
    LND_SOUND *ms = LND_NodeEnsureSound(master, nullptr);
    CHECK(ms != nullptr && LND_SoundGetState(ms) == LND_SOUND_PLAYING);
    CHECK(LND_SoundSetOutput(ms, nullptr) == LND_ERR_UNSUPPORTED);
    CHECK(LND_SoundStop(ms) == LND_OK);
    CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
    LND_SourceFree(src);
    LND_BufferFree(b);
    LND_LibraryFree();
}

static void test_graph_cycle(void) {
    printf("graph cycle\n");
    setup_null(true, 2, 48000 * 2);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_NODE *m1 = LND_NodeCreateBus(2, 48000);
    LND_NODE *m2 = LND_NodeCreateBus(2, 48000);
    LND_NODE *m3 = LND_NodeCreateBus(2, 48000);
    CHECK(LND_NodeConnect(m1, m1) != LND_OK && LND_ErrorGetLast() == LND_ERR_CYCLE);
    CHECK(LND_NodeConnect(m1, m2) == LND_OK);
    CHECK(LND_NodeConnect(m2, m3) == LND_OK);
    CHECK(LND_NodeConnect(m3, m1) != LND_OK && LND_ErrorGetLast() == LND_ERR_CYCLE);
    CHECK(LND_NodeConnect(m1, m2) == LND_OK);
    CHECK(LND_NodeGetOutputCount(m1) == 1);
    CHECK(LND_NodeConnect(m3, LND_DeviceEnsureOutputNode()) == LND_OK);
    LND_BUFFER *b = make_sine(48000, 2, 440.0, 0.5, 1.0);
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    CHECK(LND_SoundSetOutput(LND_SourceEnsureSound(src, nullptr), m1) == LND_OK);
    CHECK(LND_SoundPlay(LND_NodeEnsureSound(m3, nullptr)) == LND_OK);
    sleep_ms(50);
    CHECK(LND_NodeGetPositionFrames(m1) > 0 && LND_NodeGetPositionFrames(m2) > 0 && LND_NodeGetPositionFrames(m3) > 0);
    CHECK(LND_NodeDisconnect(m2, m3) == LND_OK);
    CHECK(LND_NodeDisconnect(m2, m3) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeGetOutputCount(m2) == 0);
    LND_SourceFree(src);
    CHECK(LND_NodeFree(m1) == LND_OK);
    CHECK(LND_NodeFree(m2) == LND_OK);
    CHECK(LND_NodeFree(m3) == LND_OK);
    LND_BufferFree(b);
    LND_LibraryFree();
}

static void test_graph_drain(void) {
    printf("graph drain\n");
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_GRAPH_DRAIN_TIMEOUT_MS, 50);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_NODE *bus = LND_NodeCreateBus(2, 48000);
    LND_BUFFER *b = make_sine(48000, 2, 440.0, 0.5, 1.0);
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(LND_SoundSetOutput(s, bus) == LND_OK);
    CHECK(LND_SoundPlay(s) == LND_OK);
    CHECK(LND_SoundGetState(s) == LND_SOUND_PLAYING);
    sleep_ms(120);
    CHECK(LND_SoundGetPositionFrames(s) == 0);
    CHECK(LND_SoundStop(s) == LND_OK);
    CHECK(LND_NodeConnect(bus, LND_DeviceEnsureOutputNode()) == LND_OK);
    CHECK(LND_SoundPlay(s) == LND_OK);
    sleep_ms(60);
    CHECK(LND_SoundGetPositionFrames(s) > 0);
    LND_SourceFree(src);
    CHECK(LND_NodeFree(bus) == LND_OK);
    LND_BufferFree(b);
    LND_LibraryFree();
}

static void test_soft_clip(void) {
    printf("soft clip\n");
    setup_null(false, 2, 48000 * 4);
    LND_ConfigSet(LND_CFG_GRAPH_CLIP_MODE, LND_CLIP_SOFT);
    CHECK(LND_LibraryInit() == LND_OK);
    CHECK(LND_NodeGetClipMode(LND_DeviceEnsureOutputNode()) == LND_CLIP_SOFT);
    LND_BUFFER *b = make_sine(48000, 2, 440.0, 1.5, 0.5);
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(LND_SoundPlay(s) == LND_OK);
    wait_state(s, LND_SOUND_STOPPED, 5000);
    LND_SourceFree(src);
    LND_BufferFree(b);
    LND_LibraryFree();
    size_t start = capture_start();
    CHECK(cap.frames >= start + 20000);
    if (cap.frames >= start + 20000) {
        stats st = analyze(cap.data + (start + 2048) * 2, 16000, 2, 0, 48000);
        CHECK(st.peak < 1.0 && st.peak > 0.9);
        CHECK_NEAR(st.freq, 440.0, 2.0);
    }
}

static uint32_t read_u32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void test_render(void) {
    printf("render\n");
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    uint32_t sample_rate_hz = 44100;
    uint64_t frames = 1000;
    LND_BUFFER *b = LND_BufferCreate(LND_FORMAT_S16, 2, sample_rate_hz, frames);
    int16_t *pcm = LND_BufferGetData(b);
    for (uint64_t i = 0; i < frames * 2; i++)
        pcm[i] = (int16_t)(i * 13 - 6000);
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    CHECK(LND_SourceGetLengthFrames(src) == frames && LND_SourceGetSampleRateHz(src) == sample_rate_hz);
    static int16_t out[2000];
    CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 300) == 300);
    CHECK(memcmp(out, pcm, 300 * 4) == 0);
    CHECK(LND_SourceGetPositionFrames(src) == 300);
    CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 1000) == 700);
    CHECK(memcmp(out, pcm + 600, 700 * 4) == 0);
    CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 10) == 0);
    CHECK(LND_SourceSeekFrames(src, 0) == LND_OK);
    const char *path = "lindar_test_render.wav";
    CHECK(LND_SourceRenderFile(src, path, LND_FORMAT_S16, 0) == frames);
    FILE *f = fopen(path, "rb");
    CHECK(f != nullptr);
    if (f) {
        static unsigned char data[8192];
        size_t n = fread(data, 1, sizeof data, f);
        fclose(f);
        CHECK(n == 44 + frames * 4);
        CHECK(memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0);
        CHECK(read_u32(data + 4) == n - 8);
        CHECK(read_u32(data + 24) == sample_rate_hz);
        CHECK(memcmp(data + 36, "data", 4) == 0 && read_u32(data + 40) == frames * 4);
        CHECK(memcmp(data + 44, pcm, frames * 4) == 0);
        remove(path);
    }
    CHECK(LND_SourceSeekFrames(src, 0) == LND_OK);
    CHECK(LND_SourceRenderFile(src, path, LND_FORMAT_F32, 100) == 100);
    remove(path);
    LND_NODE *bus = LND_NodeCreateBus(2, 48000);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(LND_SoundSetOutput(s, bus) == LND_OK);
    CHECK(LND_SourceSeekFrames(src, 0) == LND_OK);
    CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 10) == 10);
    CHECK(LND_SourceGetPositionFrames(src) == 10);
    CHECK(LND_SourceSeekFrames(src, 0) == LND_OK);
    CHECK(LND_SoundPlay(s) == LND_OK);
    CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 10) == LND_ERR_STATE && LND_ErrorGetLast() == LND_ERR_STATE);
    static float mixed[4096];
    CHECK(LND_SourceRead(LND_NodeEnsureSource(bus), mixed, LND_FORMAT_F32, 100) == 100);
    CHECK(fabsf(mixed[0]) > 0.0f || fabsf(mixed[199]) > 0.0f);
    while (LND_SoundGetState(s) == LND_SOUND_PLAYING)
        LND_SourceRead(LND_NodeEnsureSource(bus), mixed, LND_FORMAT_F32, 512);
    CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
    CHECK(LND_SourceRenderFile(LND_NodeEnsureSource(bus), path, LND_FORMAT_S16, 0) == LND_ERR_UNSUPPORTED && LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
    CHECK(LND_SourceRenderFile(LND_NodeEnsureSource(bus), path, LND_FORMAT_S16, 500) == 500);
    remove(path);
    LND_SourceFree(src);
    CHECK(LND_NodeFree(bus) == LND_OK);
    LND_BufferFree(b);
    LND_LibraryFree();
}

static LND_BUFFER *make_pattern(int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint64_t frames) {
    LND_BUFFER *f32 = make_sine(sample_rate_hz, channels, 440.0, 0.5, (double)frames / sample_rate_hz);
    if (format == LND_FORMAT_F32) return f32;
    LND_BUFFER *b = LND_BufferCreate(format, channels, sample_rate_hz, frames);
    lnd_pcm_from_f32(format, LND_BufferGetData(f32), LND_BufferGetData(b), (size_t)frames * channels);
    LND_BufferFree(f32);
    return b;
}

static void write_wav(const char *path, LND_BUFFER *b, int32_t format) {
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    CHECK(LND_SourceRenderFile(src, path, format, 0) == LND_BufferGetFrames(b));
    LND_SourceFree(src);
}

static bool same_pcm(const void *a, const void *b, size_t bytes) { return memcmp(a, b, bytes) == 0; }

static void test_file_wav(void) {
    printf("file wav\n");
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    CHECK(LND_CodecGetCount() >= 1);
    CHECK(strcmp(LND_CodecGet(0)->name, "wav") == 0);
    const char *path = "lindar_test_file.wav";
    const wchar_t *wpath = L"lindar_test_file.wav";
    uint32_t sample_rate_hz = 44100;
    uint64_t frames = 22050;
    LND_BUFFER *ref = make_pattern(LND_FORMAT_S16, 2, sample_rate_hz, frames);
    write_wav(path, ref, LND_FORMAT_S16);
    const int16_t *pcm = LND_BufferGetData(ref);
    static int16_t out[8192 * 2];
    const uint32_t variants[] = {0, LND_ENCODED_SOURCE_PRELOAD, LND_ENCODED_SOURCE_DIRECT};
    for (size_t v = 0; v < 3; v++) {
        LND_SOURCE *src = LND_SourceCreateFileWide(wpath, variants[v], nullptr);
        CHECK(src != nullptr);
        if (!src) continue;
        CHECK(LND_SourceGetCodec(src) != nullptr && strcmp(LND_SourceGetCodec(src)->name, "wav") == 0);
        CHECK(LND_SourceGetFormat(src) == LND_FORMAT_S16 && LND_SourceGetChannels(src) == 2 && LND_SourceGetSampleRateHz(src) == sample_rate_hz);
        CHECK(LND_SourceGetLengthFrames(src) == frames);
        CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 1000) == 1000);
        CHECK(same_pcm(out, pcm, 1000 * 4));
        CHECK(LND_SourceGetPositionFrames(src) == 1000);
        CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 3000) == 3000);
        CHECK(same_pcm(out, pcm + 1000 * 2, 3000 * 4));
        CHECK(LND_SourceSeekFrames(src, 5000) == LND_OK);
        CHECK(LND_SourceGetPositionFrames(src) == 5000);
        CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 500) == 500);
        CHECK(same_pcm(out, pcm + 5000 * 2, 500 * 4));
        CHECK(LND_SourceSeekFrames(src, 22000) == LND_OK);
        CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 1000) == 50);
        CHECK(same_pcm(out, pcm + 22000 * 2, 50 * 4));
        CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 10) == 0);
        CHECK(LND_SourceSeekFrames(src, 999999) == LND_OK);
        CHECK(LND_SourceGetPositionFrames(src) == frames);
        CHECK(LND_SourceSeekFrames(src, 0) == LND_OK);
        static float f32[1000 * 2];
        CHECK(LND_SourceRead(src, f32, LND_FORMAT_F32, 1000) == 1000);
        CHECK_NEAR(f32[10], pcm[10] / 32768.0, 1e-6);
        CHECK(LND_SourceFree(src) == LND_OK);
    }
    LND_SOURCE *utf8 = LND_SourceCreateFile(path, 0, nullptr);
    CHECK(utf8 != nullptr && LND_SourceGetLengthFrames(utf8) == frames);
    LND_SourceFree(utf8);
    FILE *f = fopen(path, "rb");
    CHECK(f != nullptr);
    if (f) {
        fseek(f, 0, SEEK_END);
        size_t size = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        void *bytes = malloc(size);
        CHECK(fread(bytes, 1, size, f) == size);
        fclose(f);
        LND_SOURCE *mem = LND_SourceCreateEncodedMemory(bytes, size, 0, nullptr);
        CHECK(mem != nullptr && LND_SourceGetLengthFrames(mem) == frames && LND_SourceGetFormat(mem) == LND_FORMAT_S16);
        CHECK(LND_SourceSeekFrames(mem, 100) == LND_OK);
        CHECK(LND_SourceRead(mem, out, LND_FORMAT_S16, 100) == 100);
        CHECK(same_pcm(out, pcm + 100 * 2, 100 * 4));
        LND_SourceFree(mem);
        LND_ENCODED_SOURCE_OPTIONS forced = {.codec_name = "wav"};
        LND_SOURCE *f2 = LND_SourceCreateEncodedMemory(bytes, size, 0, &forced);
        CHECK(f2 != nullptr);
        LND_SourceFree(f2);
        LND_ENCODED_SOURCE_OPTIONS bad = {.codec_name = "nope"};
        CHECK(LND_SourceCreateEncodedMemory(bytes, size, 0, &bad) == nullptr && LND_ErrorGetLast() == LND_ERR_FORMAT);
        char *padded = malloc(size + 64);
        memset(padded, 0xAB, 64);
        memcpy(padded + 64, bytes, size);
        LND_ENCODED_SOURCE_OPTIONS window = {.offset_bytes = 64, .length_bytes = size};
        LND_SOURCE *win = LND_SourceCreateEncodedMemory(padded, size + 64, 0, &window);
        CHECK(win != nullptr && LND_SourceGetLengthFrames(win) == frames);
        LND_SourceFree(win);
        free(padded);
        free(bytes);
    }
    CHECK(LND_SourceCreateFileWide(L"lindar_missing_file.wav", 0, nullptr) == nullptr && LND_ErrorGetLast() == LND_ERR_IO);
    FILE *junk = fopen("lindar_test_junk.bin", "wb");
    fwrite("hello world, this is not audio", 1, 30, junk);
    fclose(junk);
    CHECK(LND_SourceCreateFile("lindar_test_junk.bin", 0, nullptr) == nullptr && LND_ErrorGetLast() == LND_ERR_FORMAT);
    remove("lindar_test_junk.bin");
    LND_BufferFree(ref);
    remove(path);
    const struct {
        int32_t format;
        uint32_t channels;
        double tol;
    } cases[] = {
        {LND_FORMAT_U8, 1, 1.0 / 128.0}, {LND_FORMAT_S24, 6, 1.0 / 8388608.0}, {LND_FORMAT_S32, 2, 1.0 / 8388608.0}, {LND_FORMAT_F32, 3, 0.0},
        {LND_FORMAT_F64, 2, 0.0},
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        uint32_t ch = cases[k].channels;
        LND_BUFFER *orig = make_sine(sample_rate_hz, ch, 440.0, 0.5, 0.1);
        uint64_t n = LND_BufferGetFrames(orig);
        write_wav(path, orig, cases[k].format);
        LND_SOURCE *src = LND_SourceCreateFileWide(wpath, k & 1 ? LND_ENCODED_SOURCE_PRELOAD : 0, nullptr);
        CHECK(src != nullptr);
        if (src) {
            CHECK(LND_SourceGetFormat(src) == cases[k].format && LND_SourceGetChannels(src) == ch && LND_SourceGetLengthFrames(src) == n);
            float *got = malloc((size_t)n * ch * sizeof(float));
            CHECK(LND_SourceRead(src, got, LND_FORMAT_F32, n) == n);
            const float *want = LND_BufferGetData(orig);
            double err = 0.0;
            for (size_t i = 0; i < (size_t)n * ch; i++)
                err = fmax(err, fabs(got[i] - want[i]));
            CHECK(err <= cases[k].tol + 1e-7);
            free(got);
            LND_SourceFree(src);
        }
        LND_BufferFree(orig);
        remove(path);
    }
    LND_LibraryFree();
}

static void test_file_playback(void) {
    printf("file playback\n");
    setup_null(true, 2, 48000 * 4);
    CHECK(LND_LibraryInit() == LND_OK);
    const char *path = "lindar_test_play.wav";
    LND_BUFFER *b = make_sine(48000, 2, 440.0, 0.5, 0.5);
    write_wav(path, b, LND_FORMAT_S16);
    LND_BufferFree(b);
    LND_SOURCE *src = LND_SourceCreateFile(path, 0, nullptr);
    CHECK(src != nullptr);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(LND_SoundGetLengthFrames(s) == 24000);
    CHECK(LND_SoundPlay(s) == LND_OK);
    wait_state(s, LND_SOUND_STOPPED, 5000);
    CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
    LND_SourceFree(src);
    LND_LibraryFree();
    remove(path);
    size_t start = capture_start();
    size_t skip = 1024;
    size_t usable = 24000 - skip * 2;
    CHECK(cap.frames >= start + skip + usable);
    if (cap.frames >= start + skip + usable) {
        stats l = analyze(cap.data + (start + skip) * 2, usable, 2, 0, 48000);
        CHECK_NEAR(l.rms, 0.5 / sqrt(2.0), 0.02);
        CHECK_NEAR(l.freq, 440.0, 2.0);
    }
}

static void test_file_transport(void) {
    printf("file transport\n");
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_PERIOD_FRAMES, 240);
    CHECK(LND_LibraryInit() == LND_OK);
    const char *path = "lindar_test_transport.wav";
    uint32_t sample_rate_hz = 44100;
    LND_BUFFER *b = make_sine(sample_rate_hz, 2, 440.0, 0.3, 3.0);
    write_wav(path, b, LND_FORMAT_S16);
    LND_BufferFree(b);
    const uint32_t variants[] = {0, LND_ENCODED_SOURCE_PRELOAD, LND_ENCODED_SOURCE_DIRECT};
    for (size_t v = 0; v < 3; v++) {
        LND_SOURCE *src = LND_SourceCreateFile(path, variants[v], nullptr);
        CHECK(src != nullptr);
        if (!src) continue;
        LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
        CHECK(LND_SoundGetLengthFrames(s) == sample_rate_hz * 3 && LND_SoundGetSampleRateHz(s) == sample_rate_hz);
        CHECK(LND_SoundPlay(s) == LND_OK);
        sleep_ms(150);
        uint64_t p1 = LND_SoundGetPositionFrames(s);
        CHECK(p1 > sample_rate_hz / 20 && p1 < sample_rate_hz / 2);
        CHECK(LND_SoundSetPause(s, true) == LND_OK);
        sleep_ms(60);
        uint64_t p2 = LND_SoundGetPositionFrames(s);
        sleep_ms(60);
        CHECK(LND_SoundGetPositionFrames(s) == p2);
        CHECK(LND_SoundGetState(s) == LND_SOUND_PAUSED);
        CHECK(LND_SoundSeekSeconds(s, 2.0f) == LND_OK);
        CHECK(LND_SoundGetPositionFrames(s) == sample_rate_hz * 2);
        CHECK(LND_SoundSetPause(s, false) == LND_OK);
        sleep_ms(150);
        uint64_t p3 = LND_SoundGetPositionFrames(s);
        CHECK(p3 > sample_rate_hz * 2 && p3 < sample_rate_hz * 2 + sample_rate_hz / 2);
        CHECK(LND_SoundSeekSeconds(s, 2.9f) == LND_OK);
        wait_state(s, LND_SOUND_STOPPED, 2000);
        CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
        CHECK(LND_SoundGetPositionFrames(s) == LND_SoundGetLengthFrames(s));
        CHECK(LND_SoundPlay(s) == LND_OK);
        sleep_ms(80);
        CHECK(LND_SoundGetPositionFrames(s) > 0 && LND_SoundGetPositionFrames(s) < sample_rate_hz);
        CHECK(LND_SoundGetState(s) == LND_SOUND_PLAYING);
        CHECK(LND_SoundStop(s) == LND_OK);
        CHECK(LND_SoundGetPositionFrames(s) == 0);
        CHECK(LND_SourceSeekFrames(src, 1000) == LND_OK);
        static int16_t out[256 * 2];
        CHECK(LND_SourceRead(src, out, LND_FORMAT_S16, 256) == 256);
        CHECK(LND_SourceGetPositionFrames(src) == 1256);
        CHECK(LND_SoundPlay(s) == LND_OK);
        sleep_ms(30);
        CHECK(LND_SourceFree(src) == LND_OK);
    }
    LND_LibraryFree();
    remove(path);
}

typedef struct ramp_state {
    uint32_t pos;
    uint32_t length;
} ramp_state;

static int32_t ramp_probe(LND_IO *io) {
    char magic[4];
    return LND_IoRead(io, magic, 4) == 4 && memcmp(magic, "LNDR", 4) == 0 ? 100 : 0;
}

static int32_t ramp_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    unsigned char h[8];
    if (LND_IoRead(io, h, 8) != 8 || memcmp(h, "LNDR", 4) != 0) return LND_ERR_FORMAT;
    ramp_state *s = calloc(1, sizeof *s);
    s->length = (uint32_t)h[4] | (uint32_t)h[5] << 8;
    info->format = LND_FORMAT_F32;
    info->channels = 1;
    info->sample_rate_hz = 8000;
    info->length_frames = s->length;
    info->seekable = true;
    *state = s;
    return LND_OK;
}

static uint64_t ramp_read(void *state, void *dst, uint64_t frames) {
    ramp_state *s = state;
    float *out = dst;
    uint64_t n = s->pos + frames > s->length ? s->length - s->pos : frames;
    for (uint64_t i = 0; i < n; i++)
        out[i] = (float)(s->pos + i) / (float)s->length;
    s->pos += (uint32_t)n;
    return n;
}

static int32_t ramp_seek(void *state, uint64_t frame) {
    ramp_state *s = state;
    s->pos = frame > s->length ? s->length : (uint32_t)frame;
    return LND_OK;
}

static void ramp_close(void *state) { free(state); }

static const LND_CODEC ramp_codec = {
    .name = "ramp",
    .extensions = "ramp",
    .probe = ramp_probe,
    .open = ramp_open,
    .read = ramp_read,
    .seek = ramp_seek,
    .close = ramp_close,
};

static void test_bits(void) {
    printf("bits\n");
    const uint8_t data[] = {0xB3, 0x55, 0x10, 0xFF, 0x80, 0x00, 0x00, 0x01};
    lnd_bits b;
    lnd_bits_init(&b, data, sizeof data);
    CHECK(lnd_bits_remaining(&b) == 64);
    CHECK(lnd_bits_read(&b, 3) == 5);
    CHECK(lnd_bits_read(&b, 5) == 19);
    CHECK(lnd_bits_peek(&b, 8) == 0x55);
    CHECK(lnd_bits_read_signed(&b, 4) == 5);
    CHECK(lnd_bits_read_signed(&b, 4) == 5);
    CHECK(lnd_bits_unary(&b) == 3);
    lnd_bits_align(&b);
    CHECK(b.pos == 24);
    CHECK(lnd_bits_read_signed(&b, 8) == -1);
    CHECK(lnd_bits_read_signed(&b, 2) == -2);
    lnd_bits_align(&b);
    CHECK(lnd_bits_read(&b, 24) == 1);
    CHECK(lnd_bits_remaining(&b) == 0);
    lnd_bits_init(&b, data, 4);
    CHECK(lnd_bits_read(&b, 32) == 0xB35510FFu);
    const uint8_t be[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    CHECK(lnd_rd_u16be(be) == 0x0102 && lnd_rd_u16le(be) == 0x0201);
    CHECK(lnd_rd_u32be(be) == 0x01020304u && lnd_rd_u32le(be) == 0x04030201u);
    CHECK(lnd_rd_u64be(be) == 0x0102030405060708ull && lnd_rd_u64le(be) == 0x0807060504030201ull);
    const uint8_t neg_le[] = {0xFE, 0xFF, 0xFF};
    const uint8_t neg_be[] = {0xFF, 0xFF, 0xFE};
    const uint8_t min_be[] = {0x80, 0x00, 0x00};
    CHECK(lnd_rd_s24le(neg_le) == -2 && lnd_rd_s24be(neg_be) == -2);
    CHECK(lnd_rd_s24be(min_be) == -8388608 && lnd_rd_s24le(min_be) == 128);
    const uint8_t f80[] = {0x40, 0x0E, 0xAC, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    CHECK_NEAR(lnd_rd_f80be(f80), 44100.0, 1e-9);
    const uint8_t f80b[] = {0x40, 0x0E, 0xBB, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    CHECK_NEAR(lnd_rd_f80be(f80b), 48000.0, 1e-9);
    uint8_t swap[6] = {1, 2, 3, 4, 5, 6};
    lnd_swap_block(swap, 3, 2);
    CHECK(swap[0] == 3 && swap[2] == 1 && swap[3] == 6 && swap[5] == 4);
}

typedef struct aiff_writer {
    uint8_t *data;
    size_t size;
    size_t cap;
} aiff_writer;

static void aw_put(aiff_writer *w, const void *p, size_t n) {
    if (w->size + n > w->cap) {
        w->cap = (w->size + n) * 2;
        w->data = realloc(w->data, w->cap);
    }
    memcpy(w->data + w->size, p, n);
    w->size += n;
}

static void aw_u16(aiff_writer *w, uint16_t v) {
    uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    aw_put(w, b, 2);
}

static void aw_u32(aiff_writer *w, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    aw_put(w, b, 4);
}

static void aw_patch_u32(aiff_writer *w, size_t at, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    memcpy(w->data + at, b, 4);
}

static aiff_writer make_aiff(const float *samples, uint64_t frames, uint32_t channels, uint32_t sample_rate_hz, uint32_t bits, const char *compression) {
    aiff_writer w = {0};
    bool aifc = compression != nullptr;
    bool is_float = aifc && (strcmp(compression, "fl32") == 0 || strcmp(compression, "fl64") == 0);
    bool little = aifc && strcmp(compression, "sowt") == 0;
    uint32_t sample_bytes = is_float ? bits / 8 : (bits + 7) / 8;
    aw_put(&w, "FORM", 4);
    aw_u32(&w, 0);
    aw_put(&w, aifc ? "AIFC" : "AIFF", 4);
    if (aifc) {
        aw_put(&w, "FVER", 4);
        aw_u32(&w, 4);
        aw_u32(&w, 0xA2805140u);
    }
    aw_put(&w, "COMM", 4);
    size_t comm_size_at = w.size;
    aw_u32(&w, 0);
    size_t comm_start = w.size;
    aw_u16(&w, (uint16_t)channels);
    aw_u32(&w, (uint32_t)frames);
    aw_u16(&w, (uint16_t)bits);
    uint32_t e = 0;
    while ((sample_rate_hz >> e) > 1)
        e++;
    uint64_t mantissa = (uint64_t)sample_rate_hz << (63 - e);
    aw_u16(&w, (uint16_t)(16383 + e));
    aw_u32(&w, (uint32_t)(mantissa >> 32));
    aw_u32(&w, (uint32_t)mantissa);
    if (aifc) {
        aw_put(&w, compression, 4);
        uint8_t pstr[6] = {4, 'n', 'o', 'n', 'e', 0};
        aw_put(&w, pstr, 6);
    }
    aw_patch_u32(&w, comm_size_at, (uint32_t)(w.size - comm_start));
    aw_put(&w, "SSND", 4);
    size_t ssnd_size_at = w.size;
    aw_u32(&w, 0);
    size_t ssnd_start = w.size;
    aw_u32(&w, 0);
    aw_u32(&w, 0);
    for (uint64_t i = 0; i < frames * channels; i++) {
        float x = samples[i];
        if (is_float && bits == 32) {
            uint32_t u;
            memcpy(&u, &x, 4);
            aw_u32(&w, u);
        } else if (is_float) {
            double d = x;
            uint64_t u;
            memcpy(&u, &d, 8);
            aw_u32(&w, (uint32_t)(u >> 32));
            aw_u32(&w, (uint32_t)u);
        } else {
            int64_t max = (int64_t)1 << (bits - 1);
            int64_t v = llround((double)x * (double)max);
            if (v > max - 1) v = max - 1;
            if (v < -max) v = -max;
            uint32_t shifted = (uint32_t)((uint64_t)v << (sample_bytes * 8 - bits));
            uint8_t b[4];
            for (uint32_t k = 0; k < sample_bytes; k++)
                b[k] = little ? (uint8_t)(shifted >> (8 * k)) : (uint8_t)(shifted >> (8 * (sample_bytes - 1 - k)));
            aw_put(&w, b, sample_bytes);
        }
    }
    if (w.size & 1) aw_put(&w, "", 1);
    aw_patch_u32(&w, ssnd_size_at, (uint32_t)(w.size - ssnd_start));
    aw_patch_u32(&w, 4, (uint32_t)(w.size - 8));
    return w;
}

static void test_aiff(void) {
    printf("aiff\n");
    if (!LND_MODULE_AIFF) return;
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    uint32_t sample_rate_hz = 44100;
    const struct {
        uint32_t bits;
        const char *compression;
        int32_t format;
        uint32_t channels;
        double tol;
    } cases[] = {
        {16, nullptr, LND_FORMAT_S16, 2, 1.0 / 32768.0},
        {24, nullptr, LND_FORMAT_S24, 1, 1.0 / 8388608.0},
        {8, nullptr, LND_FORMAT_U8, 2, 1.0 / 128.0},
        {12, nullptr, LND_FORMAT_S16, 1, 1.0 / 2048.0},
        {32, nullptr, LND_FORMAT_S32, 2, 1.0 / 8388608.0},
        {16, "sowt", LND_FORMAT_S16, 2, 1.0 / 32768.0},
        {16, "NONE", LND_FORMAT_S16, 1, 1.0 / 32768.0},
        {32, "fl32", LND_FORMAT_F32, 2, 0.0},
        {64, "fl64", LND_FORMAT_F64, 1, 0.0},
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        uint32_t ch = cases[k].channels;
        LND_BUFFER *orig = make_sine(sample_rate_hz, ch, 440.0, 0.5, 0.05);
        uint64_t n = LND_BufferGetFrames(orig);
        aiff_writer w = make_aiff(LND_BufferGetData(orig), n, ch, sample_rate_hz, cases[k].bits, cases[k].compression);
        LND_SOURCE *src = LND_SourceCreateEncodedMemory(w.data, w.size, k & 1 ? LND_ENCODED_SOURCE_PRELOAD : 0, nullptr);
        CHECK(src != nullptr);
        if (src) {
            CHECK(strcmp(LND_SourceGetCodec(src)->name, "aiff") == 0);
            CHECK(LND_SourceGetFormat(src) == cases[k].format && LND_SourceGetChannels(src) == ch && LND_SourceGetSampleRateHz(src) == sample_rate_hz);
            CHECK(LND_SourceGetLengthFrames(src) == n);
            float *got = malloc((size_t)n * ch * sizeof(float));
            CHECK(LND_SourceRead(src, got, LND_FORMAT_F32, n) == n);
            const float *want = LND_BufferGetData(orig);
            double err = 0.0;
            for (size_t i = 0; i < (size_t)n * ch; i++)
                err = fmax(err, fabs(got[i] - want[i]));
            CHECK(err <= cases[k].tol + 1e-7);
            if (err > cases[k].tol + 1e-7) printf("  case %zu err %g\n", k, err);
            CHECK(LND_SourceSeekFrames(src, n / 2) == LND_OK);
            CHECK(LND_SourceRead(src, got, LND_FORMAT_F32, n) == n - n / 2);
            CHECK_NEAR(got[0], want[(n / 2) * ch], cases[k].tol + 1e-7);
            free(got);
            LND_SourceFree(src);
        }
        free(w.data);
        LND_BufferFree(orig);
    }
    uint8_t bad[] = {'F', 'O', 'R', 'M', 0, 0, 0, 4, 'A', 'I', 'F', 'F'};
    CHECK(LND_SourceCreateEncodedMemory(bad, sizeof bad, 0, nullptr) == nullptr && LND_ErrorGetLast() == LND_ERR_FORMAT);
    LND_LibraryFree();
}

static void test_codec_list(void) {
    printf("codec list\n");
    CHECK(LND_CodecGetCount() == LND_CODECS_COUNT);
    CHECK(LND_CODECS_COUNT >= 1);
    CHECK((LND_CodecGet(0) && strcmp(LND_CodecGet(0)->name, "wav") == 0) == 1);
    bool found_wav = false;
    for (uint32_t i = 0; i < LND_CodecGetCount(); i++) {
        const LND_CODEC *c = LND_CodecGet(i);
        CHECK(c && c->name && c->probe && c->open && c->read && c->close);
        if (strcmp(c->name, "wav") == 0) found_wav = true;
    }
    CHECK(found_wav);
    CHECK(strstr(LND_CODECS_LIST, "wav") != nullptr);
}

static const char *sample_path(const char *ext) {
    static char paths[8][256];
    static unsigned next;
    char *path = paths[next++ % 8];
    snprintf(path, 256, "audiosamples/tone.%s", ext);
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fclose(f);
    return path;
}

static void check_tone(LND_SOURCE *src, const char *label, double rms_tol, double length_tol) {
    uint32_t sample_rate_hz = LND_SourceGetSampleRateHz(src);
    uint32_t ch = LND_SourceGetChannels(src);
    CHECK((sample_rate_hz == 44100 || sample_rate_hz == 48000) && ch == 2);
    if ((sample_rate_hz != 44100 && sample_rate_hz != 48000) || ch != 2) return;
    double expected = 2.0 * sample_rate_hz;
    uint64_t length = LND_SourceGetLengthFrames(src);
    CHECK(fabs((double)length - expected) <= expected * length_tol);
    size_t cap = (size_t)(expected + sample_rate_hz);
    float *pcm = malloc(cap * ch * sizeof(float));
    uint64_t total = LND_SourceRead(src, pcm, LND_FORMAT_F32, cap);
    CHECK(fabs((double)total - expected) <= expected * length_tol);
    size_t skip = sample_rate_hz / 4;
    if (total > sample_rate_hz + skip) {
        stats l = analyze(pcm + skip * 2, sample_rate_hz, 2, 0, sample_rate_hz);
        stats r = analyze(pcm + skip * 2, sample_rate_hz, 2, 1, sample_rate_hz);
        CHECK_NEAR(l.freq, 440.0, 3.0);
        CHECK_NEAR(r.freq, 660.0, 4.0);
        CHECK_NEAR(l.rms, 0.5 / sqrt(2.0), rms_tol);
        CHECK_NEAR(r.rms, 0.5 / sqrt(2.0), rms_tol);
        if (fabs(l.freq - 440.0) > 3.0 || fabs(l.rms - 0.5 / sqrt(2.0)) > rms_tol)
            printf("  %s: freq %.2f/%.2f rms %.4f/%.4f total %llu\n", label, l.freq, r.freq, l.rms, r.rms, (unsigned long long)total);
    }
    CHECK(LND_SourceSeekFrames(src, sample_rate_hz) == LND_OK);
    CHECK(LND_SourceGetPositionFrames(src) == sample_rate_hz);
    uint64_t half = sample_rate_hz / 2;
    uint64_t got = LND_SourceRead(src, pcm, LND_FORMAT_F32, half);
    CHECK(got == half);
    if (got == half) {
        stats l = analyze(pcm + 1024 * 2, (size_t)half - 2048, 2, 0, sample_rate_hz);
        CHECK_NEAR(l.freq, 440.0, 3.0);
        CHECK_NEAR(l.rms, 0.5 / sqrt(2.0), rms_tol);
    }
    CHECK(LND_SourceGetPositionFrames(src) == sample_rate_hz + got);
    free(pcm);
}

static const char *expected_codec(const char *ext) {
    if (LND_MODULE_MP3 && strcmp(ext, "mp3") == 0) return "mp3";
    if (LND_MODULE_VORBIS && strcmp(ext, "ogg") == 0) return "vorbis";
    if (LND_MODULE_OPUS && strcmp(ext, "opus") == 0) return "opus";
    if (LND_MODULE_FLAC && strcmp(ext, "flac") == 0) return "flac";
    if (LND_MODULE_AAC && (strcmp(ext, "m4a") == 0 || strcmp(ext, "aac") == 0)) return "aac";
    return "mediafoundation";
}

static void test_internal_codec(const char *ext, const char *name, int32_t format, double rms_tol, double length_tol, double seek_tol) {
    printf("codec %s\n", name);
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    LND_ConfigSet(LND_CFG_CODECS_SYSTEM, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    const char *path = sample_path(ext);
    CHECK(path != nullptr);
    if (!path) {
        LND_LibraryFree();
        return;
    }
    const uint32_t variants[] = {0, LND_ENCODED_SOURCE_PRELOAD, LND_ENCODED_SOURCE_DIRECT};
    for (size_t v = 0; v < 3; v++) {
        LND_SOURCE *src = LND_SourceCreateFile(path, variants[v], nullptr);
        CHECK(src != nullptr);
        if (!src) {
            printf("  %s variant %zu: %s\n", ext, v, LND_ErrorGetString(LND_ErrorGetLast()));
            continue;
        }
        CHECK(strcmp(LND_SourceGetCodec(src)->name, name) == 0);
        CHECK(!(LND_SourceGetCodec(src)->flags & LND_CODEC_FLAG_SYSTEM));
        CHECK(LND_SourceGetFormat(src) == format);
        check_tone(src, name, rms_tol, length_tol);
        LND_SourceFree(src);
    }
    FILE *f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void *bytes = malloc(size);
    CHECK(fread(bytes, 1, size, f) == size);
    fclose(f);
    LND_SOURCE *mem = LND_SourceCreateEncodedMemory(bytes, size, 0, nullptr);
    CHECK(mem && strcmp(LND_SourceGetCodec(mem)->name, name) == 0);
    if (mem) {
        uint32_t sample_rate_hz = LND_SourceGetSampleRateHz(mem);
        uint32_t ch = LND_SourceGetChannels(mem);
        uint64_t len = LND_SourceGetLengthFrames(mem);
        float *all = malloc((size_t)(len + sample_rate_hz) * ch * sizeof(float));
        uint64_t total = LND_SourceRead(mem, all, LND_FORMAT_F32, len + sample_rate_hz);
        CHECK(fabs((double)total - (double)len) <= (double)len * length_tol + 1.0);
        uint64_t target = sample_rate_hz * 3 / 4;
        CHECK(LND_SourceSeekFrames(mem, target) == LND_OK);
        float chunk[2048 * 2];
        uint64_t got = LND_SourceRead(mem, chunk, LND_FORMAT_F32, 2048);
        CHECK(got == 2048);
        if (got == 2048 && total >= target + 2048) {
            double err = 0.0;
            for (size_t i = 0; i < 2048 * (size_t)ch; i++)
                err = fmax(err, fabs(chunk[i] - all[target * ch + i]));
            CHECK(err < seek_tol);
            if (err >= seek_tol) printf("  %s: seek mismatch %g\n", name, err);
        }
        free(all);
        LND_SourceFree(mem);
    }
    free(bytes);
    LND_ConfigSet(LND_CFG_CODECS_SYSTEM, 1);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 1);
    LND_SOURCE *play = LND_SourceCreateFile(path, 0, nullptr);
    CHECK(play != nullptr);
    if (play) {
        LND_SOUND *s = LND_SourceEnsureSound(play, nullptr);
        uint32_t sample_rate_hz = LND_SourceGetSampleRateHz(play);
        CHECK(LND_SoundPlay(s) == LND_OK);
        sleep_ms(300);
        uint64_t p = LND_SoundGetPositionFrames(s);
        CHECK(p > sample_rate_hz / 5 && p < sample_rate_hz);
        CHECK(LND_SoundSeekSeconds(s, 1.5f) == LND_OK);
        sleep_ms(200);
        CHECK(LND_SoundGetPositionFrames(s) > sample_rate_hz * 3 / 2);
        wait_state(s, LND_SOUND_STOPPED, 3000);
        CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
        LND_SourceFree(play);
    }
    LND_LibraryFree();
}

static void test_internal_codecs(void) {
    if (LND_MODULE_VORBIS) test_internal_codec("ogg", "vorbis", LND_FORMAT_F32, 0.04, 0.05, 1e-3);
    if (LND_MODULE_OPUS) test_internal_codec("opus", "opus", LND_FORMAT_F32, 0.04, 0.05, 0.1);
    if (LND_MODULE_FLAC) test_internal_codec("flac", "flac", LND_FORMAT_S16, 0.001, 0.0, 1e-6);
    if (LND_MODULE_AAC) {
        test_internal_codec("m4a", "aac", LND_FORMAT_S16, 0.04, 0.05, 1e-4);
        test_internal_codec("aac", "aac", LND_FORMAT_S16, 0.04, 0.06, 1e-4);
        test_internal_codec("he.aac", "aac", LND_FORMAT_S16, 0.06, 0.1, 1e-3);
    }
    if (LND_MODULE_FLAC) {
        printf("flac exact\n");
        setup_null(true, 2, 48000 * 2);
        LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
        CHECK(LND_LibraryInit() == LND_OK);
        const char *flac = sample_path("flac");
        const char *wav = sample_path("wav");
        LND_SOURCE *a = flac ? LND_SourceCreateFile(flac, 0, nullptr) : nullptr;
        LND_SOURCE *b = wav ? LND_SourceCreateFile(wav, 0, nullptr) : nullptr;
        CHECK(a && b);
        if (a && b) {
            CHECK(LND_SourceGetLengthFrames(a) == LND_SourceGetLengthFrames(b));
            static int16_t x[4096 * 2], y[4096 * 2];
            CHECK(LND_SourceSeekFrames(a, 12345) == LND_OK && LND_SourceSeekFrames(b, 12345) == LND_OK);
            CHECK(LND_SourceRead(a, x, LND_FORMAT_S16, 4096) == 4096);
            CHECK(LND_SourceRead(b, y, LND_FORMAT_S16, 4096) == 4096);
            CHECK(memcmp(x, y, sizeof x) == 0);
        }
        if (a) LND_SourceFree(a);
        if (b) LND_SourceFree(b);
        LND_LibraryFree();
    }
    if (LND_MODULE_AAC) {
        printf("aac aligned\n");
        setup_null(true, 2, 48000 * 2);
        LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
        CHECK(LND_LibraryInit() == LND_OK);
        const char *m4a = sample_path("m4a");
        const char *wav = sample_path("wav");
        LND_SOURCE *a = m4a ? LND_SourceCreateFile(m4a, 0, nullptr) : nullptr;
        LND_SOURCE *b = wav ? LND_SourceCreateFile(wav, 0, nullptr) : nullptr;
        CHECK(a && b);
        if (a && b) {
            CHECK(LND_SourceGetLengthFrames(a) == LND_SourceGetLengthFrames(b));
            static float x[4096 * 2], y[(4096 + 128) * 2];
            CHECK(LND_SourceSeekFrames(a, 24000) == LND_OK && LND_SourceSeekFrames(b, 24000 - 64) == LND_OK);
            CHECK(LND_SourceRead(a, x, LND_FORMAT_F32, 4096) == 4096);
            CHECK(LND_SourceRead(b, y, LND_FORMAT_F32, 4096 + 128) == 4096 + 128);
            double best = 1e9;
            int best_lag = 0;
            for (int lag = -64; lag <= 64; lag++) {
                double err = 0.0;
                for (size_t i = 0; i < 4096 * 2; i++)
                    err += fabs(x[i] - y[(size_t)(64 + lag) * 2 + i]);
                if (err < best) {
                    best = err;
                    best_lag = lag;
                }
            }
            double err = 0.0;
            for (size_t i = 0; i < 4096 * 2; i++)
                err = fmax(err, fabs(x[i] - y[64 * 2 + i]));
            CHECK(best_lag == 0);
            CHECK(err < 0.05);
            if (best_lag != 0 || err >= 0.05) printf("  aac lag %d, max error %g\n", best_lag, err);
        }
        if (a) LND_SourceFree(a);
        if (b) LND_SourceFree(b);
        LND_LibraryFree();
    }
}

static void test_ffmpeg(void) {
    printf("ffmpeg\n");
    if (!LND_MODULE_FFMPEG) return;
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    const char *exts[] = {"mp3", "wma", "m4a", "flac", "ogg", "opus", "aac", "wav"};
    LND_ENCODED_SOURCE_OPTIONS forced = {.codec_name = "ffmpeg"};
    for (size_t k = 0; k < sizeof exts / sizeof exts[0]; k++) {
        const char *path = sample_path(exts[k]);
        if (!path) continue;
        LND_SOURCE *src = LND_SourceCreateFile(path, k & 1 ? LND_ENCODED_SOURCE_PRELOAD : 0, &forced);
        CHECK(src != nullptr);
        if (!src) {
            printf("  %s: %s\n", exts[k], LND_ErrorGetString(LND_ErrorGetLast()));
            continue;
        }
        CHECK(strcmp(LND_SourceGetCodec(src)->name, "ffmpeg") == 0);
        bool lossless = strcmp(exts[k], "flac") == 0 || strcmp(exts[k], "wav") == 0;
        check_tone(src, exts[k], lossless ? 0.002 : 0.04, lossless ? 0.001 : 0.05);
        LND_SourceFree(src);
    }
    const char *wav = sample_path("wav");
    if (wav) {
        LND_SOURCE *src = LND_SourceCreateFile(wav, 0, nullptr);
        CHECK(src && strcmp(LND_SourceGetCodec(src)->name, "wav") == 0);
        LND_SourceFree(src);
    }
    LND_LibraryFree();
}

static void test_system_codecs(void) {
    printf("system codecs\n");
    if (!LND_MODULE_MEDIAFOUNDATION) return;
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    const struct {
        const char *ext;
        double rms_tol;
        double length_tol;
        bool required;
    } cases[] = {
        {"mp3", 0.04, 0.05, true},  {"wma", 0.04, 0.05, true},  {"m4a", 0.04, 0.05, true},
        {"flac", 0.01, 0.01, true}, {"ogg", 0.04, 0.05, false}, {"opus", 0.04, 0.05, false},
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        const char *path = sample_path(cases[k].ext);
        CHECK(path != nullptr);
        if (!path) continue;
        LND_SOURCE *src = LND_SourceCreateFile(path, k & 1 ? LND_ENCODED_SOURCE_PRELOAD : 0, nullptr);
        if (!src) {
            if (cases[k].required)
                CHECK(src != nullptr);
            else
                printf("  %s: not supported by the system decoder\n", cases[k].ext);
            continue;
        }
        const char *expected = expected_codec(cases[k].ext);
        CHECK(strcmp(LND_SourceGetCodec(src)->name, expected) == 0);
        if (strcmp(LND_SourceGetCodec(src)->name, expected) != 0) printf("  %s: got codec %s\n", cases[k].ext, LND_SourceGetCodec(src)->name);
        if (strcmp(expected, "mediafoundation") == 0) CHECK(LND_SourceGetCodec(src)->flags & LND_CODEC_FLAG_SYSTEM);
        check_tone(src, cases[k].ext, cases[k].rms_tol, cases[k].length_tol);
        LND_SourceFree(src);
    }
    const char *wav = sample_path("wav");
    const char *aiff = sample_path("aiff");
    if (wav) {
        LND_SOURCE *src = LND_SourceCreateFile(wav, 0, nullptr);
        CHECK(src && strcmp(LND_SourceGetCodec(src)->name, "wav") == 0);
        if (src) check_tone(src, "wav", 0.001, 0.0);
        LND_SourceFree(src);
        LND_ENCODED_SOURCE_OPTIONS forced = {.codec_name = "mediafoundation"};
        LND_SOURCE *sys = LND_SourceCreateFile(wav, 0, &forced);
        CHECK(sys && strcmp(LND_SourceGetCodec(sys)->name, "mediafoundation") == 0);
        if (sys) check_tone(sys, "wav via mf", 0.002, 0.001);
        LND_SourceFree(sys);
    }
    if (aiff && LND_MODULE_AIFF) {
        LND_SOURCE *src = LND_SourceCreateFile(aiff, 0, nullptr);
        CHECK(src && strcmp(LND_SourceGetCodec(src)->name, "aiff") == 0);
        if (src) check_tone(src, "aiff", 0.001, 0.0);
        LND_SourceFree(src);
    }
    const char *mp3 = sample_path("mp3");
    if (mp3) {
        FILE *f = fopen(mp3, "rb");
        fseek(f, 0, SEEK_END);
        size_t size = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        void *bytes = malloc(size);
        CHECK(fread(bytes, 1, size, f) == size);
        fclose(f);
        LND_ENCODED_SOURCE_OPTIONS force_mf = {.codec_name = "mediafoundation"};
        LND_SOURCE *mem = LND_SourceCreateEncodedMemory(bytes, size, 0, &force_mf);
        CHECK(mem && strcmp(LND_SourceGetCodec(mem)->name, "mediafoundation") == 0);
        if (mem) check_tone(mem, "mp3 memory via mf", 0.04, 0.05);
        LND_SourceFree(mem);
        free(bytes);
        LND_ConfigSet(LND_CFG_CODECS_SYSTEM, 0);
        const char *wma = sample_path("wma");
        LND_SOURCE *no_system = LND_SourceCreateFile(wma, 0, nullptr);
        if (LND_MODULE_FFMPEG) {
            CHECK(no_system && strcmp(LND_SourceGetCodec(no_system)->name, "ffmpeg") == 0);
        } else {
            CHECK(no_system == nullptr && LND_ErrorGetLast() == LND_ERR_FORMAT);
        }
        if (no_system) LND_SourceFree(no_system);
        CHECK(LND_SourceCreateFile(mp3, 0, &force_mf) == nullptr && LND_ErrorGetLast() == LND_ERR_FORMAT);
        LND_SOURCE *still = LND_SourceCreateFile(wav, 0, nullptr);
        CHECK(still != nullptr);
        LND_SourceFree(still);
        LND_ConfigSet(LND_CFG_CODECS_SYSTEM, 1);
        LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 1);
        LND_SOURCE *play = LND_SourceCreateFile(mp3, 0, nullptr);
        CHECK(play != nullptr);
        if (play) {
            LND_SOUND *s = LND_SourceEnsureSound(play, nullptr);
            CHECK(LND_SoundPlay(s) == LND_OK);
            sleep_ms(300);
            uint64_t p = LND_SoundGetPositionFrames(s);
            CHECK(p > 44100 / 5 && p < 44100);
            CHECK(LND_SoundSeekSeconds(s, 1.5f) == LND_OK);
            sleep_ms(200);
            CHECK(LND_SoundGetPositionFrames(s) > 44100 * 3 / 2);
            CHECK(LND_SoundSetPause(s, true) == LND_OK);
            sleep_ms(50);
            uint64_t p2 = LND_SoundGetPositionFrames(s);
            sleep_ms(50);
            CHECK(LND_SoundGetPositionFrames(s) == p2);
            CHECK(LND_SoundStop(s) == LND_OK);
            CHECK(LND_SoundGetPositionFrames(s) == 0);
            LND_SourceFree(play);
        }
    }
    LND_LibraryFree();
}

static void test_mp3(void) {
    printf("mp3\n");
    if (!LND_MODULE_MP3) return;
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    const char *path = sample_path("mp3");
    CHECK(path != nullptr);
    if (!path) {
        LND_LibraryFree();
        return;
    }
    const uint32_t variants[] = {0, LND_ENCODED_SOURCE_PRELOAD, LND_ENCODED_SOURCE_DIRECT};
    for (size_t v = 0; v < 3; v++) {
        LND_SOURCE *src = LND_SourceCreateFile(path, variants[v], nullptr);
        CHECK(src != nullptr);
        if (!src) continue;
        CHECK(strcmp(LND_SourceGetCodec(src)->name, "mp3") == 0);
        CHECK(!(LND_SourceGetCodec(src)->flags & LND_CODEC_FLAG_SYSTEM));
        CHECK(LND_SourceGetFormat(src) == LND_FORMAT_F32);
        check_tone(src, "mp3 internal", 0.04, 0.05);
        LND_SourceFree(src);
    }
    FILE *f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void *bytes = malloc(size);
    CHECK(fread(bytes, 1, size, f) == size);
    fclose(f);
    LND_SOURCE *mem = LND_SourceCreateEncodedMemory(bytes, size, 0, nullptr);
    CHECK(mem && strcmp(LND_SourceGetCodec(mem)->name, "mp3") == 0);
    if (mem) {
        uint64_t len = LND_SourceGetLengthFrames(mem);
        float *all = malloc((size_t)(len + 4096) * 2 * sizeof(float));
        uint64_t total = LND_SourceRead(mem, all, LND_FORMAT_F32, len + 4096);
        CHECK(total == len);
        CHECK(LND_SourceSeekFrames(mem, 30000) == LND_OK);
        float chunk[4096 * 2];
        CHECK(LND_SourceRead(mem, chunk, LND_FORMAT_F32, 4096) == 4096);
        double err = 0.0;
        for (size_t i = 0; i < 4096 * 2; i++)
            err = fmax(err, fabs(chunk[i] - all[30000 * 2 + i]));
        CHECK(err < 1e-4);
        free(all);
        LND_SourceFree(mem);
    }
    free(bytes);
    LND_ConfigSet(LND_CFG_CODECS_SYSTEM, 0);
    LND_SOURCE *internal = LND_SourceCreateFile(path, 0, nullptr);
    CHECK(internal && strcmp(LND_SourceGetCodec(internal)->name, "mp3") == 0);
    LND_SourceFree(internal);
    LND_ConfigSet(LND_CFG_CODECS_SYSTEM, 1);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 1);
    LND_SOURCE *play = LND_SourceCreateFile(path, 0, nullptr);
    if (play) {
        LND_SOUND *s = LND_SourceEnsureSound(play, nullptr);
        CHECK(LND_SoundPlay(s) == LND_OK);
        sleep_ms(300);
        uint64_t p = LND_SoundGetPositionFrames(s);
        CHECK(p > 44100 / 5 && p < 44100);
        CHECK(LND_SoundSeekSeconds(s, 1.5f) == LND_OK);
        sleep_ms(200);
        CHECK(LND_SoundGetPositionFrames(s) > 44100 * 3 / 2);
        wait_state(s, LND_SOUND_STOPPED, 3000);
        CHECK(LND_SoundGetState(s) == LND_SOUND_STOPPED);
        LND_SourceFree(play);
    }
    LND_LibraryFree();
}

static void test_custom_codec(void) {
    printf("custom codec\n");
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    uint32_t before = LND_CodecGetCount();
    CHECK(LND_CodecRegister(&ramp_codec) == LND_OK);
    CHECK(LND_CodecRegister(&ramp_codec) == LND_OK);
    CHECK(LND_CodecGetCount() == before + 1);
    unsigned char file[8] = {'L', 'N', 'D', 'R', 200, 0, 0, 0};
    LND_SOURCE *src = LND_SourceCreateEncodedMemory(file, sizeof file, 0, nullptr);
    CHECK(src != nullptr);
    if (src) {
        CHECK(LND_SourceGetCodec(src) == &ramp_codec);
        CHECK(LND_SourceGetLengthFrames(src) == 200 && LND_SourceGetSampleRateHz(src) == 8000 && LND_SourceGetChannels(src) == 1);
        float out[200];
        CHECK(LND_SourceRead(src, out, LND_FORMAT_F32, 200) == 200);
        CHECK_NEAR(out[0], 0.0, 1e-7);
        CHECK_NEAR(out[100], 0.5, 1e-7);
        CHECK_NEAR(out[199], 199.0 / 200.0, 1e-7);
        CHECK(LND_SourceSeekFrames(src, 150) == LND_OK);
        CHECK(LND_SourceRead(src, out, LND_FORMAT_F32, 100) == 50);
        CHECK_NEAR(out[0], 0.75, 1e-7);
        LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
        CHECK(LND_SoundGetLengthFrames(s) == 200);
        LND_SourceFree(src);
    }
    LND_SOURCE *pre = LND_SourceCreateEncodedMemory(file, sizeof file, LND_ENCODED_SOURCE_PRELOAD, nullptr);
    CHECK(pre != nullptr);
    if (pre) {
        float out[50];
        CHECK(LND_SourceSeekFrames(pre, 100) == LND_OK);
        CHECK(LND_SourceRead(pre, out, LND_FORMAT_F32, 50) == 50);
        CHECK_NEAR(out[0], 0.5, 1e-7);
        LND_SourceFree(pre);
    }
    CHECK(LND_CodecUnregister(&ramp_codec) == LND_OK);
    CHECK(LND_CodecUnregister(&ramp_codec) == LND_ERR_INVALID_ARG);
    CHECK(LND_SourceCreateEncodedMemory(file, sizeof file, 0, nullptr) == nullptr && LND_ErrorGetLast() == LND_ERR_FORMAT);
    LND_LibraryFree();
}

static void test_sound_data(void) {
    printf("sound data\n");
    setup_null(true, 2, 48000 * 4);
    CHECK(LND_LibraryInit() == LND_OK);
    const uint32_t sample_rate_hz = 48000, frames = 48000;
    LND_BUFFER *b = LND_BufferCreate(LND_FORMAT_F32, 2, sample_rate_hz, frames);
    float *pcm = LND_BufferGetData(b);
    for (uint32_t i = 0; i < frames; i++) {
        pcm[i * 2] = (float)(0.5 * sin(6.283185307179586 * 440.0 * i / sample_rate_hz));
        pcm[i * 2 + 1] = (float)(0.5 * sin(6.283185307179586 * 660.0 * i / sample_rate_hz));
    }
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(s && LND_SourceEnsureSound(src, nullptr) == s && LND_NodeEnsureSound(LND_SourceEnsureNode(src), nullptr) == s);
    CHECK(LND_SoundGetSampleRateHz(s) == sample_rate_hz && LND_SoundGetChannels(s) == 2);
    CHECK(LND_SourceEnsureSound(src, &(LND_SOUND_CONFIG){.channels = 33}) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_SourceEnsureSound(src, &(LND_SOUND_CONFIG){.flags = 8}) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_SourceEnsureSound(src, &(LND_SOUND_CONFIG){.flags = LND_SOUND_RESAMPLE_SINC32 + 1}) == nullptr);
    CHECK(LND_SoundReadF32(nullptr, pcm, 1) == LND_ERR_INVALID_ARG && LND_SoundReadF32(s, nullptr, 1) == LND_ERR_INVALID_ARG);
    static float out[9600 * 2];
    CHECK(LND_SoundReadF32(s, out, 1000) == 1000);
    CHECK(memcmp(out, pcm, 1000 * 2 * sizeof(float)) == 0);
    CHECK(LND_SoundGetPositionFrames(s) == 1000 && LND_SourceGetPositionFrames(src) == 1000 && LND_SoundGetState(s) == LND_SOUND_STOPPED);
    CHECK(LND_SoundReadF32(s, out, 1000) == 1000);
    CHECK(memcmp(out, pcm + 1000 * 2, 1000 * 2 * sizeof(float)) == 0);
    CHECK(LND_SoundSeekFrames(s, 47000) == LND_OK);
    CHECK(LND_SoundReadF32(s, out, 2000) == 1000);
    CHECK(memcmp(out, pcm + 47000 * 2, 1000 * 2 * sizeof(float)) == 0);
    CHECK(LND_SoundGetPositionFrames(s) == frames);
    CHECK(LND_SoundSetLoop(s, true) == LND_OK);
    CHECK(LND_SoundSeekFrames(s, 47000) == LND_OK);
    CHECK(LND_SoundReadF32(s, out, 2000) == 2000);
    CHECK(memcmp(out, pcm + 47000 * 2, 1000 * 2 * sizeof(float)) == 0 && memcmp(out + 1000 * 2, pcm, 1000 * 2 * sizeof(float)) == 0);
    CHECK(LND_SoundGetPositionFrames(s) == 1000);
    CHECK(LND_SoundSetLoop(s, false) == LND_OK);
    CHECK(LND_SoundSetConfig(s, &(LND_SOUND_CONFIG){.channels = 1}) == LND_OK && LND_SoundGetChannels(s) == 1 && LND_SoundGetSampleRateHz(s) == sample_rate_hz);
    CHECK(LND_SoundSeekFrames(s, 0) == LND_OK);
    CHECK(LND_SoundReadF32(s, out, 1000) == 1000);
    double err = 0.0;
    for (uint32_t i = 0; i < 1000; i++)
        err = fmax(err, fabs(out[i] - 0.5f * (pcm[i * 2] + pcm[i * 2 + 1])));
    CHECK(err < 1e-6);
    const float swap[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    CHECK(LND_SoundSetConfig(s, &(LND_SOUND_CONFIG){.channels = 2, .channel_matrix = swap}) == LND_OK);
    LND_SOUND *swapped = LND_SourceGetSound(src);
    CHECK(swapped == s && LND_SoundGetChannels(s) == 2 && LND_SourceEnsureSound(src, &(LND_SOUND_CONFIG){.channels = 2, .channel_matrix = swap}) == s);
    CHECK(LND_SoundSeekFrames(s, 0) == LND_OK);
    CHECK(LND_SoundReadF32(s, out, 1000) == 1000);
    err = 0.0;
    for (uint32_t i = 0; i < 1000; i++)
        err = fmax(err, fmax(fabs(out[i * 2] - pcm[i * 2 + 1]), fabs(out[i * 2 + 1] - pcm[i * 2])));
    CHECK(err < 1e-6);
    CHECK(LND_SoundSetConfig(s, &(LND_SOUND_CONFIG){.sample_rate_hz = 96000, .flags = LND_SOUND_RESAMPLE_SINC32}) == LND_OK);
    CHECK(LND_SoundGetSampleRateHz(s) == 96000 && LND_SoundGetChannels(s) == 2 && LND_SoundGetLengthFrames(s) == 96000);
    CHECK(LND_SoundSeekFrames(s, 0) == LND_OK);
    CHECK(LND_SoundReadF32(s, out, 9600) == 9600);
    stats st = analyze(out + 256 * 2, 9600 - 512, 2, 0, 96000);
    CHECK_NEAR(st.rms, 0.5 / sqrt(2.0), 0.01);
    CHECK_NEAR(st.freq, 440.0, 2.0);
    st = analyze(out + 256 * 2, 9600 - 512, 2, 1, 96000);
    CHECK_NEAR(st.freq, 660.0, 2.0);
    CHECK(LND_SoundGetPositionFrames(s) == 9600 && LND_SourceGetPositionFrames(src) == 4800);
    CHECK(LND_SoundReadF32(s, out, 9600) == 9600);
    st = analyze(out + 256 * 2, 9600 - 512, 2, 0, 96000);
    CHECK_NEAR(st.freq, 440.0, 2.0);
    CHECK(LND_SoundGetPositionFrames(s) == 19200);
    CHECK(LND_SoundSeekSeconds(s, 0.5f) == LND_OK);
    CHECK(LND_SoundGetPositionFrames(s) == 48000);
    CHECK(LND_SoundSetConfig(s, nullptr) == LND_OK);
    CHECK(LND_SoundGetPositionFrames(s) == 24000 && LND_SoundGetLengthFrames(s) == frames);
    CHECK(LND_SoundSetConfig(s, &(LND_SOUND_CONFIG){.sample_rate_hz = 44100, .flags = LND_SOUND_RESAMPLE_LINEAR}) == LND_OK);
    CHECK(LND_SoundSeekFrames(s, 0) == LND_OK);
    CHECK(LND_SoundReadF32(s, out, 4410) == 4410);
    st = analyze(out + 64 * 2, 4410 - 128, 2, 1, 44100);
    CHECK_NEAR(st.freq, 660.0, 3.0);
    CHECK(LND_SoundGetPositionFrames(s) == 4410 && LND_SourceGetPositionFrames(src) == 4800);
    CHECK(LND_SoundPlay(s) == LND_OK);
    sleep_ms(100);
    uint64_t before = LND_SoundGetPositionFrames(s);
    CHECK(before >= 4410 && before < 4410 + 44100 / 2);
    CHECK(LND_SoundReadF32(s, out, 4410) == 4410);
    uint64_t after = LND_SoundGetPositionFrames(s);
    CHECK(after >= before + 4410 && LND_SoundGetState(s) == LND_SOUND_PLAYING);
    CHECK(LND_SoundStop(s) == LND_OK);
    for (int i = 0; i < 200 && LND_NodeIsActive(LND_SourceEnsureNode(src)); i++)
        sleep_ms(5);
    CHECK(LND_SoundSetConfig(s, nullptr) == LND_OK);
    LND_NODE *bus = LND_NodeCreateBus(2, 48000);
    CHECK(bus && LND_SoundSetOutput(s, bus) == LND_OK && LND_NodeConnect(bus, LND_DeviceEnsureOutputNode()) == LND_OK);
    LND_SOUND *group = LND_NodeEnsureSound(bus, &(LND_SOUND_CONFIG){.channels = 1});
    CHECK(group && group != s && LND_SoundGetChannels(group) == 1 && LND_SoundGetSampleRateHz(group) == 48000);
    CHECK(LND_SoundReadF32(group, out, 1000) == 1000);
    stats silent = analyze(out, 1000, 1, 0, 48000);
    CHECK(silent.peak == 0.0);
    CHECK(LND_SoundPlay(group) == LND_OK);
    CHECK(LND_SoundReadF32(group, out, 4800) == 4800);
    stats mixed = analyze(out + 1024, 4800 - 1024, 1, 0, 48000);
    CHECK(mixed.rms > 0.15);
    CHECK(LND_SoundStop(group) == LND_OK);
    LND_SourceFree(src);
    LND_BufferFree(b);
    CHECK(LND_NodeFree(bus) == LND_OK);
    const char *wav = sample_path("wav");
    LND_SOURCE *f1 = wav ? LND_SourceCreateFile(wav, 0, nullptr) : nullptr;
    LND_SOURCE *f2 = wav ? LND_SourceCreateFile(wav, 0, nullptr) : nullptr;
    CHECK(f1 && f2);
    if (f1 && f2) {
        static float a[4800 * 2], c[4800 * 2];
        CHECK(LND_SourceSeekFrames(f1, 12000) == LND_OK && LND_SourceRead(f1, a, LND_FORMAT_F32, 4800) == 4800);
        LND_SOUND *fs = LND_SourceEnsureSound(f2, nullptr);
        CHECK(fs && LND_SoundSeekFrames(fs, 12000) == LND_OK && LND_SoundReadF32(fs, c, 4800) == 4800);
        CHECK(memcmp(a, c, sizeof a) == 0);
        CHECK(LND_SoundGetPositionFrames(fs) == 16800);
        CHECK(LND_SoundReadF32(fs, c, 4800) == 4800 && LND_SourceRead(f1, a, LND_FORMAT_F32, 4800) == 4800);
        CHECK(memcmp(a, c, sizeof a) == 0);
    }
    if (f1) LND_SourceFree(f1);
    if (f2) LND_SourceFree(f2);
    LND_LibraryFree();
}

typedef struct gen_state {
    double phase;
    uint32_t sample_rate_hz;
    uint64_t frames;
} gen_state;

static void gen_proc(void *user, void *data, uint64_t frames) {
    gen_state *g = user;
    float *out = data;
    double step = 6.283185307179586 * 440.0 / g->sample_rate_hz;
    for (uint64_t i = 0; i < frames; i++) {
        float v = (float)sin(g->phase);
        out[i * 2] = 0.5f * v;
        out[i * 2 + 1] = 0.25f * v;
        g->phase += step;
        if (g->phase > 6.283185307179586) g->phase -= 6.283185307179586;
    }
    g->frames += frames;
}

static void test_capture(void) {
    printf("capture\n");
    setup_null(true, 2, 48000 * 4);
    gen_state capture_gen = {.sample_rate_hz = 48000};
    LND_NullSetInputCallback(gen_proc, &capture_gen);
    CHECK(LND_LibraryInit() == LND_OK);
    LND_DEVICE *in = LND_DeviceGetDefault(LND_DEVICE_INPUT);
    LND_DEVICE *out = LND_DeviceGetDefault(LND_DEVICE_OUTPUT);
    CHECK(in && LND_DeviceGetType(in) == LND_DEVICE_INPUT && !(LND_DeviceGetFlags(in) & LND_DEVICE_FLAG_LOOPBACK));
    CHECK(out && (LND_DeviceGetFlags(out) & LND_DEVICE_FLAG_LOOPBACK));
    CHECK(LND_SourceCreateDevice(nullptr, 33, 0, 0) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_SourceCreateDevice(nullptr, 0, 0, 1u << 8) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_SourceCreateDevice(nullptr, 0, 0, LND_CAPTURE_EXCLUSIVE) == nullptr && LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
    LND_SOURCE *mic = LND_SourceCreateDevice(nullptr, 0, 0, 0);
    CHECK(mic != nullptr);
    if (!mic) {
        LND_LibraryFree();
        return;
    }
    CHECK(LND_SourceGetSampleRateHz(mic) == 48000 && LND_SourceGetChannels(mic) == 2 && LND_SourceGetLengthFrames(mic) == 0 && LND_SourceGetFormat(mic) == LND_FORMAT_F32);
    CHECK(LND_SourceGetCodec(mic) == nullptr);
    CHECK(LND_DeviceGetInstanceCount(in) == 1);
    LND_DEVICE_INSTANCE *ci = LND_DeviceGetInstance(in, 0);
    CHECK(ci && LND_DeviceInstanceGetNode(ci) == nullptr && LND_DeviceInstanceGetSampleRateHz(ci) == 48000 && LND_DeviceInstanceGetChannels(ci) == 2 &&
          LND_DeviceInstanceIsRunning(ci));
    CHECK(LND_DeviceInstanceGetDevice(ci) == in);
    CHECK(LND_SourceSeekFrames(mic, 0) == LND_ERR_UNSUPPORTED);
    static float buf[9600 * 2];
    CHECK(LND_SourceRead(mic, buf, LND_FORMAT_F32, 9600) == 9600);
    stats l = analyze(buf, 9600, 2, 0, 48000);
    stats r = analyze(buf, 9600, 2, 1, 48000);
    CHECK_NEAR(l.rms, 0.5 / sqrt(2.0), 0.02);
    CHECK_NEAR(r.rms, 0.25 / sqrt(2.0), 0.02);
    CHECK_NEAR(l.freq, 440.0, 2.0);
    CHECK(LND_SourceGetPositionFrames(mic) == 9600);
    CHECK(LND_DeviceInstanceGetPositionFrames(ci) >= 9600);
    static int16_t pcm16[4800 * 2];
    CHECK(LND_SourceRead(mic, pcm16, LND_FORMAT_S16, 4800) == 4800);
    CHECK(LND_SourceGetPositionFrames(mic) == 14400);
    gen_state generators[3] = {{.sample_rate_hz = 48000}, {.sample_rate_hz = 48000}, {.sample_rate_hz = 48000}};
    CHECK(LND_NullSetInputCallback(gen_proc, &generators[0]) == LND_OK);
    LND_SOURCE *m2 = LND_SourceCreateDevice(in, 1, 44100, LND_CAPTURE_RESAMPLE | LND_CAPTURE_NONBLOCKING);
    CHECK(m2 && LND_SourceGetSampleRateHz(m2) == 44100 && LND_SourceGetChannels(m2) == 1);
    CHECK(LND_DeviceGetInstanceCount(in) == 2);
    sleep_ms(200);
    uint64_t got = LND_SourceRead(m2, buf, LND_FORMAT_F32, 9600);
    CHECK(got >= 4410 && got <= 9600);
    if (got >= 4410) {
        stats m = analyze(buf + 256, (size_t)got - 256, 1, 0, 44100);
        CHECK_NEAR(m.rms, 0.375 / sqrt(2.0), 0.03);
    }
    CHECK(LND_SourceGetPositionFrames(m2) == got);
    CHECK(LND_NullSetInputCallback(gen_proc, &generators[1]) == LND_OK);
    LND_SOURCE *m3 = LND_SourceCreateDevice(in, 0, 44100, 0);
    CHECK(m3 && LND_SourceGetSampleRateHz(m3) == 48000 && LND_SourceGetChannels(m3) == 2);
    LND_SourceFree(m3);
    LND_SourceFree(m2);
    CHECK(LND_DeviceGetInstanceCount(in) == 1);
    LND_SOURCE *loop = LND_SourceCreateDevice(out, 0, 0, 0);
    CHECK(loop && LND_SourceGetSampleRateHz(loop) == 48000 && LND_SourceGetChannels(loop) == 2);
    CHECK(LND_DeviceGetInstanceCount(out) == 2);
    CHECK(LND_SourceRead(loop, buf, LND_FORMAT_F32, 4800) == 4800);
    CHECK(LND_SourceCreateDevice(out, 0, 0, LND_CAPTURE_EXCLUSIVE) == nullptr && LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
    LND_SourceFree(loop);
    CHECK(LND_DeviceGetInstanceCount(out) == 1);
    CHECK(LND_NullSetInputCallback(gen_proc, &generators[2]) == LND_OK);
    LND_SOURCE *m4 = LND_SourceCreateDevice(in, 0, 0, 0);
    CHECK(m4 != nullptr);
    LND_DEVICE_INSTANCE *i4 = LND_DeviceGetInstance(in, LND_DeviceGetInstanceCount(in) - 1);
    CHECK(i4 && i4 != ci && LND_DeviceInstanceClose(i4) == LND_OK);
    CHECK(LND_SourceRead(m4, buf, LND_FORMAT_F32, 9600) < 9600);
    LND_SourceFree(m4);
    CHECK(LND_DeviceGetInstanceCount(in) == 1);
    float *drain = malloc(16384 * 2 * sizeof(float));
    CHECK(LND_SourceRead(mic, drain, LND_FORMAT_F32, 16384) == 16384);
    free(drain);
    LND_SOUND *s = LND_SourceEnsureSound(mic, nullptr);
    CHECK(s && LND_SoundGetLengthFrames(s) == 0 && LND_SoundGetSampleRateHz(s) == 48000);
    LND_CAPTURE_INFO info = {0};
    for (unsigned retry = 0; retry < 500; retry++) {
        CHECK(LND_SourceGetCaptureInfo(mic, &info) == LND_OK);
        if (info.buffered_frames >= 9600) break;
        sleep_ms(2);
    }
    CHECK(info.buffered_frames >= 9600);
    uint64_t before_play = LND_SoundGetPositionFrames(s);
    CHECK(LND_SoundPlay(s) == LND_OK);
    sleep_ms(500);
    CHECK(LND_SoundGetState(s) == LND_SOUND_PLAYING);
    CHECK(LND_SoundGetPositionFrames(s) > before_play + 9600);
    CHECK(LND_SoundReadF32(s, buf, 4800) == 4800);
    CHECK(LND_SoundStop(s) == LND_OK);
    for (int k = 0; k < 200 && LND_NodeIsActive(LND_SourceEnsureNode(mic)); k++)
        sleep_ms(5);
    CHECK(LND_SoundReadF32(s, buf, 4800) == 4800);
    stats g = analyze(buf + 256 * 2, 4800 - 256, 2, 0, 48000);
    CHECK_NEAR(g.freq, 440.0, 2.0);
    CHECK_NEAR(g.rms, 0.5 / sqrt(2.0), 0.02);
    size_t start = capture_start();
    CHECK(cap.frames > start + 9600 + 4800);
    if (cap.frames > start + 9600 + 4800) {
        stats o = analyze(cap.data + (start + 9600) * 2, 4800, 2, 0, 48000);
        if (fabs(o.freq - 440.0) > 3.0) printf("  capture frequency %.3f, RMS %.6f, frames %zu\n", o.freq, o.rms, cap.frames);
        CHECK_NEAR(o.freq, 440.0, 3.0);
        CHECK(o.rms > 0.2);
    }
    LND_SOURCE *left = LND_SourceCreateDevice(nullptr, 0, 0, 0);
    CHECK(left != nullptr);
    LND_SourceFree(mic);
    CHECK(LND_DeviceGetInstanceCount(in) == 1);
    LND_LibraryFree();
    LND_NullSetInputCallback(nullptr, nullptr);
}

static void test_split_mix(void) {
    printf("splitter mixer\n");
    setup_null(true, 2, 48000 * 6);
    gen_state capture_gen = {.sample_rate_hz = 48000};
    LND_NullSetInputCallback(gen_proc, &capture_gen);
    CHECK(LND_LibraryInit() == LND_OK);
    const uint32_t sample_rate_hz = 48000, frames = 48000;
    LND_BUFFER *b = LND_BufferCreate(LND_FORMAT_F32, 2, sample_rate_hz, frames);
    LND_BUFFER *b2 = LND_BufferCreate(LND_FORMAT_F32, 2, sample_rate_hz, frames);
    float *pcm = LND_BufferGetData(b);
    float *dc = LND_BufferGetData(b2);
    for (uint32_t i = 0; i < frames; i++) {
        pcm[i * 2] = (float)(0.5 * sin(6.283185307179586 * 440.0 * i / sample_rate_hz));
        pcm[i * 2 + 1] = (float)(0.25 * sin(6.283185307179586 * 660.0 * i / sample_rate_hz));
        dc[i * 2] = 0.25f;
        dc[i * 2 + 1] = -0.125f;
    }
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    LND_NODE *split = LND_NodeCreateSplitter(2, sample_rate_hz, 2);
    CHECK(split && LND_NodeGetType(split) == LND_NODE_SPLITTER && LND_NodeGetSplitterOutputCount(split) == 2);
    CHECK(LND_NodeCreateSplitter(2, sample_rate_hz, 0) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    LND_NODE *a = LND_NodeGetSplitterOutput(split, 0);
    LND_NODE *bb = LND_NodeGetSplitterOutput(split, 1);
    CHECK(a && bb && a != bb && LND_NodeGetType(a) == LND_NODE_SPLIT && LND_NodeGetSplitterOutput(split, 2) == nullptr);
    CHECK(LND_NodeGetChannels(a) == 2 && LND_NodeGetSampleRateHz(a) == sample_rate_hz && LND_NodeGetInputCount(a) == 0);
    CHECK(LND_NodeFree(a) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(src), a) != LND_OK && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeConnect(split, LND_DeviceEnsureOutputNode()) != LND_OK && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(src), split) == LND_OK);
    LND_SOUND *sa = LND_NodeEnsureSound(a, nullptr);
    LND_SOUND *sb = LND_NodeEnsureSound(bb, nullptr);
    CHECK(sa && sb && sa != sb && LND_SoundGetLengthFrames(sa) == frames && LND_SoundGetState(sa) == LND_SOUND_STOPPED);
    CHECK(LND_SoundGetSampleRateHz(sa) == sample_rate_hz && LND_SoundGetChannels(sb) == 2);
    CHECK(LND_SoundSeekFrames(sa, 10) == LND_ERR_UNSUPPORTED);
    static float out[9600 * 2];
    CHECK(LND_SoundReadF32(sb, out, 1000) == 0);
    stats z = analyze(out, 1000, 2, 0, sample_rate_hz);
    CHECK(z.peak == 0.0);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(src, nullptr)) == LND_OK);
    CHECK(LND_SoundGetOutput(LND_SourceEnsureSound(src, nullptr)) == split);
    CHECK(LND_SoundReadF32(sb, out, 1000) == 1000);
    CHECK(memcmp(out + 256 * 2, pcm + 256 * 2, (1000 - 256) * 2 * sizeof(float)) == 0);
    CHECK(LND_SoundReadF32(sa, out, 1000) == 1000);
    CHECK(memcmp(out + 256 * 2, pcm + 256 * 2, (1000 - 256) * 2 * sizeof(float)) == 0);
    CHECK(LND_SoundReadF32(sa, out, 500) == 500);
    CHECK(memcmp(out, pcm + 1000 * 2, 500 * 2 * sizeof(float)) == 0);
    CHECK(LND_SoundGetPositionFrames(sa) == 1500 && LND_SoundGetPositionFrames(sb) == 1000);
    CHECK(LND_SoundReadF32(sb, out, 1500) == 1500);
    CHECK(memcmp(out, pcm + 1000 * 2, 1500 * 2 * sizeof(float)) == 0);
    CHECK(LND_SourceGetPositionFrames(src) == 2500);
    CHECK(LND_NodeConnect(a, LND_DeviceEnsureOutputNode()) == LND_OK);
    CHECK(LND_SoundPlay(sa) == LND_OK);
    sleep_ms(200);
    CHECK(LND_SoundGetState(sa) == LND_SOUND_PLAYING && LND_SoundGetState(sb) == LND_SOUND_STOPPED);
    CHECK(LND_SoundGetPositionFrames(sa) > 1500 + 4800);
    CHECK(LND_SoundSetPause(sa, true) == LND_OK && LND_SoundGetState(sa) == LND_SOUND_PAUSED);
    sleep_ms(50);
    uint64_t paused_pos = LND_SoundGetPositionFrames(sa);
    sleep_ms(100);
    CHECK(LND_SoundGetPositionFrames(sa) == paused_pos);
    CHECK(LND_SoundReadF32(sb, out, 4800) == 4800);
    stats live = analyze(out + 64 * 2, 4800 - 64, 2, 0, sample_rate_hz);
    CHECK_NEAR(live.freq, 440.0, 2.0);
    CHECK(LND_SoundGetState(LND_SourceEnsureSound(src, nullptr)) == LND_SOUND_PLAYING);
    CHECK(LND_SoundSetPause(sa, false) == LND_OK && LND_SoundGetState(sa) == LND_SOUND_PLAYING);
    sleep_ms(100);
    CHECK(LND_SoundGetPositionFrames(sa) > paused_pos);
    CHECK(LND_SoundStop(sa) == LND_OK && LND_SoundGetState(sa) == LND_SOUND_STOPPED && LND_SoundGetPositionFrames(sa) == 0);
    CHECK(LND_SoundGetState(LND_SourceEnsureSound(src, nullptr)) == LND_SOUND_PLAYING);
    CHECK(LND_SoundReadF32(sa, out, 1000) == 1000 && LND_SoundGetPositionFrames(sa) == 1000);
    size_t start = capture_start();
    CHECK(cap.frames > start + 4800);
    if (cap.frames > start + 4800) {
        stats o = analyze(cap.data + (start + 1024) * 2, 4800 - 1024, 2, 0, sample_rate_hz);
        CHECK_NEAR(o.freq, 440.0, 3.0);
    }
    LND_SourceFree(src);
    CHECK(LND_NodeFree(split) == LND_OK);
    LND_SOURCE *s1 = LND_SourceCreateBuffer(b);
    LND_SOURCE *s2 = LND_SourceCreateBuffer(b2);
    LND_NODE *mix = LND_NodeCreateMixer(2, sample_rate_hz, LND_MIX_CONTINUOUS);
    CHECK(mix && LND_NodeGetType(mix) == LND_NODE_MIXER && LND_NodeGetMixMode(mix) == LND_MIX_CONTINUOUS);
    CHECK(LND_NodeCreateMixer(2, sample_rate_hz, 7) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(s1), mix) == LND_OK && LND_NodeConnect(LND_SourceEnsureNode(s2), mix) == LND_OK);
    LND_SOUND *sm = LND_NodeEnsureSound(mix, nullptr);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(s1, nullptr)) == LND_OK && LND_SoundPlay(LND_SourceEnsureSound(s2, nullptr)) == LND_OK);
    CHECK(LND_SoundReadF32(sm, out, 1000) == 1000);
    double err = 0.0;
    for (size_t i = 256 * 2; i < 1000 * 2; i++)
        err = fmax(err, fabs(out[i] - (pcm[i] + dc[i])));
    CHECK(err < 1e-6);
    CHECK(LND_SourceGetPositionFrames(s1) == 1000 && LND_SourceGetPositionFrames(s2) == 1000);
    LND_SOURCE *mic = LND_SourceCreateDevice(nullptr, 2, sample_rate_hz, 0);
    LND_SOURCE *s3 = LND_SourceCreateBuffer(b2);
    LND_NODE *lock = LND_NodeCreateMixer(2, sample_rate_hz, LND_MIX_LOCKSTEP);
    CHECK(mic && s3 && lock && LND_NodeGetMixMode(lock) == LND_MIX_LOCKSTEP);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(mic), lock) == LND_OK && LND_NodeConnect(LND_SourceEnsureNode(s3), lock) == LND_OK);
    LND_SOUND *sl = LND_NodeEnsureSound(lock, nullptr);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(mic, nullptr)) == LND_OK && LND_SoundPlay(LND_SourceEnsureSound(s3, nullptr)) == LND_OK);
    uint64_t received = 0;
    for (unsigned retry = 0; received < 9600 && retry < 1000; retry++) {
        received += LND_SoundReadF32(sl, out + received * 2, 9600 - received);
        if (received < 9600) sleep_ms(1);
    }
    CHECK(received == 9600);
    CHECK(LND_SourceGetPositionFrames(s3) == 9600 && LND_SourceGetPositionFrames(mic) == 9600);
    if (LND_SourceGetPositionFrames(s3) != 9600 || LND_SourceGetPositionFrames(mic) != 9600)
        printf("  lockstep: received %llu, buffer %llu, capture %llu\n", (unsigned long long)received, (unsigned long long)LND_SourceGetPositionFrames(s3),
               (unsigned long long)LND_SourceGetPositionFrames(mic));
    stats lk = analyze(out, 9600, 2, 1, sample_rate_hz);
    CHECK(lk.peak > 0.2);
    lnd_capture *input2 = lnd_capture_new(2, sample_rate_hz, 8192);
    lnd_capture *input3 = lnd_capture_new(2, sample_rate_hz, 8192);
    CHECK(input2 && input3);
    LND_SOURCE *mic2 = (LND_SOURCE *)lnd_source_obj_create(lnd_capture_source_create(input2, 2, sample_rate_hz, 0), 0, LND_FORMAT_F32);
    LND_SOURCE *mic3 = (LND_SOURCE *)lnd_source_obj_create(lnd_capture_source_create(input3, 2, sample_rate_hz, 0), 0, LND_FORMAT_F32);
    LND_NODE *avail = LND_NodeCreateMixer(2, sample_rate_hz, LND_MIX_AVAILABLE);
    LND_NODE *cont = LND_NodeCreateMixer(2, sample_rate_hz, LND_MIX_CONTINUOUS);
    CHECK(mic2 && mic3 && avail && cont);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(mic2), avail) == LND_OK && LND_NodeConnect(LND_SourceEnsureNode(mic3), cont) == LND_OK);
    LND_SOUND *sv = LND_NodeEnsureSound(avail, nullptr);
    LND_SOUND *sc = LND_NodeEnsureSound(cont, nullptr);
    CHECK(LND_SoundPlay(LND_SourceEnsureSound(mic2, nullptr)) == LND_OK && LND_SoundPlay(LND_SourceEnsureSound(mic3, nullptr)) == LND_OK);
    CHECK(LND_SoundReadF32(sc, out, 9600) == 9600);
    lnd_capture_push(input2, pcm, LND_FORMAT_F32, 4096);
    uint64_t got1 = LND_SoundReadF32(sv, out, 9600);
    CHECK(got1 == 4096);
    uint64_t got2 = LND_SoundReadF32(sv, out, 9600);
    CHECK(got2 == 0);
    CHECK(LND_NodeConnect(lock, LND_DeviceEnsureOutputNode()) == LND_OK);
    sleep_ms(300);
    uint64_t p3 = LND_SourceGetPositionFrames(s3), pm = LND_SourceGetPositionFrames(mic);
    CHECK(p3 > 9600 && pm > 9600 && (p3 > pm ? p3 - pm : pm - p3) <= 2 * 480);
    LND_NODE *leftover = LND_NodeCreateSplitter(0, 0, 3);
    CHECK(leftover && LND_NodeConnect(LND_SourceEnsureNode(mic2), leftover) == LND_OK);
    CHECK(LND_NodeFree(mix) == LND_OK);
    LND_SourceFree(s1);
    LND_SourceFree(s2);
    LND_BufferFree(b);
    LND_BufferFree(b2);
    LND_LibraryFree();
    LND_NullSetInputCallback(nullptr, nullptr);
}

typedef struct mem_sink {
    uint8_t *data;
    size_t size;
    size_t cap;
} mem_sink;

static size_t mem_sink_write(void *user, const void *data, size_t size) {
    mem_sink *m = user;
    if (m->size + size > m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 65536;
        while (cap < m->size + size)
            cap *= 2;
        m->data = realloc(m->data, cap);
        m->cap = cap;
    }
    memcpy(m->data + m->size, data, size);
    m->size += size;
    return size;
}

typedef struct output_case {
    const char *ext;
    const char *enc;
    int32_t format;
    uint32_t quality;
    uint32_t bitrate_kbps;
    double rms_tol;
    double len_tol;
    bool exact;
} output_case;

static void test_output(void) {
    printf("output\n");
    setup_null(true, 2, 48000 * 2);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    const char *wav = sample_path("wav");
    CHECK(wav != nullptr);
    if (!wav) {
        LND_LibraryFree();
        return;
    }
    CHECK(LND_EncoderGetCount() == LND_ENCODERS_COUNT);
    CHECK(LND_EncoderFind("wav") != nullptr && LND_EncoderFind("nope") == nullptr);
    LND_SOURCE *src = LND_SourceCreateFile(wav, LND_ENCODED_SOURCE_PRELOAD, nullptr);
    CHECK(src != nullptr);
    uint32_t sample_rate_hz = LND_SourceGetSampleRateHz(src), ch = LND_SourceGetChannels(src);
    uint64_t len = LND_SourceGetLengthFrames(src);
    float *ref = malloc((size_t)len * ch * sizeof(float));
    CHECK(LND_SourceRead(src, ref, LND_FORMAT_F32, len) == len);
    LND_ENCODER_PARAMS bad = {.channels = 0, .sample_rate_hz = sample_rate_hz};
    CHECK(LND_OutputCreateFile("out_bad.wav", &bad) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    LND_ENCODER_PARAMS plain = {.channels = ch, .sample_rate_hz = sample_rate_hz};
    CHECK(LND_OutputCreateFile("out_test.xyz", &plain) == nullptr && LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
    CHECK(LND_OutputCreateFile("out_noext", &plain) == nullptr && LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
    LND_ENCODER_PARAMS badq = {.channels = ch, .sample_rate_hz = sample_rate_hz, .quality = 101};
    CHECK(LND_OutputCreateFile("out_test.wav", &badq) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    const output_case cases[] = {
        {"wav", "wav", LND_FORMAT_S24, 0, 0, 0.001, 0.0, false},
        {"flac", "flac", LND_FORMAT_S16, 80, 0, 0.001, 0.0, true},
        {"ogg", "vorbis", 0, 60, 0, 0.04, 0.05, false},
        {"opus", "opus", 0, 0, 96, 0.05, 0.05, false},
        {"mp3", "mp3", 0, 0, 128, 0.05, 0.08, false},
        {"aac", "aac", 0, 0, 0, 0.05, 0.08, false},
        {"m4a", "aac", 0, 50, 0, 0.05, 0.02, false},
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        const output_case *c = &cases[k];
        if (!LND_EncoderFind(c->enc)) continue;
        char path[64];
        snprintf(path, sizeof path, "out_test.%s", c->ext);
        LND_ENCODER_PARAMS q = {.channels = ch, .sample_rate_hz = sample_rate_hz, .format = c->format, .quality = c->quality, .bitrate_kbps = c->bitrate_kbps};
        LND_OUTPUT *o = LND_OutputCreateFile(path, &q);
        CHECK(o != nullptr);
        if (!o) {
            printf("  %s: %s\n", path, LND_ErrorGetString(LND_ErrorGetLast()));
            continue;
        }
        CHECK(strcmp(LND_OutputGetEncoder(o)->name, c->enc) == 0 && LND_OutputGetSampleRateHz(o) == sample_rate_hz && LND_OutputGetChannels(o) == ch);
        CHECK(LND_SourceSeekFrames(src, 0) == LND_OK);
        CHECK(LND_OutputWriteSource(o, src, 0) == len);
        CHECK(LND_OutputGetFrames(o) == len && LND_OutputGetBytes(o) > 0);
        CHECK(LND_OutputFree(o) == LND_OK);
        LND_SOURCE *back = LND_SourceCreateFile(path, LND_ENCODED_SOURCE_PRELOAD, nullptr);
        CHECK(back != nullptr);
        if (back) {
            CHECK(strcmp(LND_SourceGetCodec(back)->name, c->enc) == 0 && LND_SourceGetChannels(back) == ch);
            double expect = (double)len * LND_SourceGetSampleRateHz(back) / sample_rate_hz;
            CHECK(fabs((double)LND_SourceGetLengthFrames(back) - expect) <= expect * c->len_tol + 1.0);
            check_tone(back, path, c->rms_tol, c->len_tol);
            if (c->exact) {
                float *dec = malloc((size_t)len * ch * sizeof(float));
                CHECK(LND_SourceSeekFrames(back, 0) == LND_OK && LND_SourceRead(back, dec, LND_FORMAT_F32, len) == len);
                CHECK(memcmp(dec, ref, (size_t)len * ch * sizeof(float)) == 0);
                free(dec);
            }
            LND_SourceFree(back);
        }
        remove(path);
    }
    if (LND_EncoderFind("ffmpeg")) {
        LND_ENCODER_PARAMS w = {.channels = ch, .sample_rate_hz = sample_rate_hz, .bitrate_kbps = 128};
        LND_OUTPUT *o = LND_OutputCreateFile("out_test.wma", &w);
        CHECK(o && strcmp(LND_OutputGetEncoder(o)->name, "ffmpeg") == 0);
        if (o) {
            CHECK(LND_SourceSeekFrames(src, 0) == LND_OK && LND_OutputWriteSource(o, src, 0) == len);
            CHECK(LND_OutputFree(o) == LND_OK);
            LND_SOURCE *back = LND_SourceCreateFile("out_test.wma", 0, nullptr);
            CHECK(back != nullptr);
            if (back) {
                check_tone(back, "wma", 0.05, 0.1);
                LND_SourceFree(back);
            }
            remove("out_test.wma");
        }
    }
    if (LND_EncoderFind("vorbis")) {
        mem_sink sink = {0};
        LND_IO_OUTPUT_PROCS procs = {.write = mem_sink_write};
        LND_ENCODER_PARAMS noenc = {.channels = ch, .sample_rate_hz = sample_rate_hz};
        CHECK(LND_OutputCreateProc(&procs, &sink, &noenc) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
        LND_ENCODER_PARAMS v = {.encoder_name = "vorbis", .channels = ch, .sample_rate_hz = sample_rate_hz, .flags = LND_OUTPUT_STREAMING};
        LND_OUTPUT *o = LND_OutputCreateProc(&procs, &sink, &v);
        CHECK(o != nullptr);
        if (o) {
            CHECK(LND_SourceSeekFrames(src, 0) == LND_OK && LND_OutputWriteSource(o, src, 0) == len);
            static int16_t s16[1000 * 2];
            for (size_t i = 0; i < 1000 * ch; i++)
                s16[i] = (int16_t)lrintf(ref[i] * 32767.0f);
            CHECK(LND_OutputWrite(o, s16, LND_FORMAT_S16, 1000) == LND_OK);
            CHECK(LND_OutputFlush(o) == LND_OK && LND_OutputGetFrames(o) == len + 1000);
            size_t before_free = sink.size;
            CHECK(LND_OutputFree(o) == LND_OK && sink.size >= before_free);
            LND_SOURCE *back = LND_SourceCreateEncodedMemory(sink.data, sink.size, 0, nullptr);
            CHECK(back && strcmp(LND_SourceGetCodec(back)->name, "vorbis") == 0);
            if (back) {
                CHECK(fabs((double)LND_SourceGetLengthFrames(back) - (double)(len + 1000)) <= (double)len * 0.05);
                LND_SourceFree(back);
            }
        }
        free(sink.data);
    }
    LND_OUTPUT *left = LND_OutputCreateFile("out_left.wav", &plain);
    CHECK(left != nullptr);
    free(ref);
    LND_SourceFree(src);
    LND_LibraryFree();
    remove("out_left.wav");
}

typedef struct proc_state {
    float factor;
    int params;
    int released;
} proc_state;

static void proc_process(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz) {
    proc_state *p = user;
    (void)sample_rate_hz;
    for (uint32_t i = 0; i < frames * channels; i++)
        pcm[i] *= p->factor;
}

static void proc_param(void *user, int32_t param, float value) {
    proc_state *p = user;
    if (param == LND_PARAM_USER) p->factor = value;
    p->params++;
}

static void proc_release(void *user) { ((proc_state *)user)->released++; }

static void test_effects(void) {
    printf("effects\n");
    setup_null(false, 2, 1024);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    const uint32_t sample_rate_hz = 48000, frames = 48000;
    LND_BUFFER *b = LND_BufferCreate(LND_FORMAT_F32, 2, sample_rate_hz, frames);
    float *pcm = LND_BufferGetData(b);
    for (uint32_t i = 0; i < frames; i++) {
        pcm[i * 2] = (float)(0.5 * sin(TWO_PI * 100.0 * i / sample_rate_hz));
        pcm[i * 2 + 1] = (float)(0.5 * sin(TWO_PI * 5000.0 * i / sample_rate_hz));
    }
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    proc_state st = {.factor = 2.0f};
    LND_PROCESSOR_PROCS procs = {.process = proc_process, .param = proc_param, .release = proc_release};
    LND_PROCESSOR_PROCS bad = {.param = proc_param};
    CHECK(LND_NodeCreateProcessor(nullptr, &st, 2, sample_rate_hz) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeCreateProcessor(&bad, &st, 2, sample_rate_hz) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    LND_NODE *proc = LND_NodeCreateProcessor(&procs, &st, 2, sample_rate_hz);
    CHECK(proc && LND_NodeGetType(proc) == LND_NODE_PROCESSOR && LND_NodeGetProcessorUser(proc) == &st && LND_NodeGetChannels(proc) == 2);
    CHECK(LND_NodeGetProcessorProcs(proc) && LND_NodeGetProcessorProcs(proc)->process == proc_process && LND_NodeGetProcessorProcs(LND_SourceEnsureNode(src)) == nullptr);
    CHECK(LND_NodeGetProcessorUser(LND_SourceEnsureNode(src)) == nullptr);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(src), proc) == LND_OK);
    LND_SOUND *s = LND_NodeEnsureSound(proc, nullptr);
    CHECK(s && LND_SoundPlay(LND_SourceEnsureSound(src, nullptr)) == LND_OK);
    static float out[9600 * 2];
    CHECK(LND_SoundReadF32(s, out, 4800) == 4800);
    double err = 0.0;
    for (uint32_t i = 256 * 2; i < 4800 * 2; i++)
        err = fmax(err, fabs(out[i] - 2.0f * pcm[i]));
    CHECK(err < 1e-6);
    CHECK(LND_NodeSetParam(proc, LND_PARAM_USER + LND_PARAM_USER_COUNT, 1.0f) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(proc, LND_PARAM_USER - 1, 1.0f) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(LND_SourceEnsureNode(src), LND_PARAM_USER, 1.0f) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetParam(proc, LND_PARAM_USER, 0.5f) == LND_OK);
    CHECK_NEAR(LND_NodeGetParam(proc, LND_PARAM_USER), 0.5, 1e-9);
    CHECK(LND_NodeGetParam(proc, LND_PARAM_GAIN) == 1.0f && LND_NodeGetParam(proc, LND_PARAM_USER + 1) == 0.0f);
    CHECK(LND_SoundReadF32(s, out, 4800) == 4800);
    err = 0.0;
    for (uint32_t i = 0; i < 4800 * 2; i++)
        err = fmax(err, fabs(out[i] - 0.5f * pcm[4800 * 2 + i]));
    CHECK(err < 1e-6 && st.params == 1 && st.factor == 0.5f);
    CHECK(LND_NodeFree(proc) == LND_OK && st.released == 1);
#if LND_MODULE_DSP
    CHECK(LND_NodeCreateBiquad(2, sample_rate_hz, &(LND_BIQUAD_CONFIG){.type = 99, .frequency_hz = 1000.0f, .q = 0.7f, .gain_db = 0.0f}) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeCreateBiquad(2, sample_rate_hz, &(LND_BIQUAD_CONFIG){.type = LND_BIQUAD_LOWPASS, .frequency_hz = 0.0f, .q = 0.7f, .gain_db = 0.0f}) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    LND_NODE *lp = LND_NodeCreateBiquad(2, sample_rate_hz, &(LND_BIQUAD_CONFIG){.type = LND_BIQUAD_LOWPASS, .frequency_hz = 1000.0f, .q = 0.7071f, .gain_db = 0.0f});
    CHECK(lp && LND_NodeGetType(lp) == LND_NODE_PROCESSOR && LND_NodeGetChannels(lp) == 2);
    CHECK_NEAR(LND_NodeGetParam(lp, LND_DSP_PARAM_FREQUENCY_HZ), 1000.0, 1e-3);
    CHECK(LND_NodeGetParam(lp, LND_DSP_PARAM_TYPE) == (float)LND_BIQUAD_LOWPASS);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(src), lp) == LND_OK);
    LND_SOUND *ls = LND_NodeEnsureSound(lp, nullptr);
    CHECK(ls && LND_SoundPlay(LND_SourceEnsureSound(src, nullptr)) == LND_OK);
    CHECK(LND_SoundReadF32(ls, out, 4800) == 4800 && LND_SoundReadF32(ls, out, 4800) == 4800);
    stats l = analyze(out, 4800, 2, 0, sample_rate_hz), r = analyze(out, 4800, 2, 1, sample_rate_hz);
    CHECK_NEAR(l.rms, 0.5 / sqrt(2.0), 0.02);
    CHECK_NEAR(l.freq, 100.0, 2.0);
    CHECK(r.rms < 0.5 / sqrt(2.0) * 0.06);
    CHECK(LND_NodeSetParam(lp, LND_DSP_PARAM_TYPE, (float)LND_BIQUAD_HIGHPASS) == LND_OK);
    CHECK(LND_SoundReadF32(ls, out, 4800) == 4800 && LND_SoundReadF32(ls, out, 4800) == 4800);
    l = analyze(out, 4800, 2, 0, sample_rate_hz);
    r = analyze(out, 4800, 2, 1, sample_rate_hz);
    CHECK(l.rms < 0.5 / sqrt(2.0) * 0.06);
    CHECK_NEAR(r.rms, 0.5 / sqrt(2.0), 0.02);
    CHECK_NEAR(r.freq, 5000.0, 20.0);
    CHECK(LND_NodeSetParam(lp, LND_DSP_PARAM_TYPE, (float)LND_BIQUAD_PEAKING) == LND_OK);
    CHECK(LND_NodeSetParam(lp, LND_DSP_PARAM_FREQUENCY_HZ, 5000.0f) == LND_OK && LND_NodeSetParam(lp, LND_DSP_PARAM_Q, 2.0f) == LND_OK &&
          LND_NodeSetParam(lp, LND_DSP_PARAM_GAIN_DB, -20.0f) == LND_OK);
    CHECK(LND_SoundReadF32(ls, out, 4800) == 4800 && LND_SoundReadF32(ls, out, 4800) == 4800);
    l = analyze(out, 4800, 2, 0, sample_rate_hz);
    r = analyze(out, 4800, 2, 1, sample_rate_hz);
    CHECK_NEAR(l.rms, 0.5 / sqrt(2.0), 0.02);
    CHECK_NEAR(r.rms, 0.5 / sqrt(2.0) * 0.1, 0.01);
    CHECK(LND_NodeFree(lp) == LND_OK);

    LND_BUFFER *ib = LND_BufferCreate(LND_FORMAT_F32, 1, sample_rate_hz, frames);
    float *imp = LND_BufferGetData(ib);
    memset(imp, 0, frames * sizeof(float));
    imp[1000] = 1.0f;
    LND_SOURCE *isrc = LND_SourceCreateBuffer(ib);
    CHECK(LND_NodeCreateDelay(1, sample_rate_hz, &(LND_DELAY_CONFIG){.max_delay_ms = 100.0f, .delay_ms = 200.0f, .feedback = 0.5f, .mix = 1.0f}) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeCreateDelay(1, sample_rate_hz, &(LND_DELAY_CONFIG){.max_delay_ms = 100.0f, .delay_ms = 10.0f, .feedback = 1.0f, .mix = 1.0f}) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    LND_NODE *dl = LND_NodeCreateDelay(1, sample_rate_hz, &(LND_DELAY_CONFIG){.max_delay_ms = 100.0f, .delay_ms = 10.0f, .feedback = 0.5f, .mix = 1.0f});
    CHECK(dl && LND_NodeGetChannels(dl) == 1 && LND_NodeGetParam(dl, LND_DSP_PARAM_DELAY_MS) == 10.0f);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(isrc), dl) == LND_OK);
    LND_SOUND *ds = LND_NodeEnsureSound(dl, nullptr);
    CHECK(ds && LND_SoundPlay(LND_SourceEnsureSound(isrc, nullptr)) == LND_OK && LND_SoundReadF32(ds, out, 3000) == 3000);
    CHECK_NEAR(out[1000], 0.0, 1e-6);
    CHECK_NEAR(out[1479], 0.0, 1e-4);
    CHECK_NEAR(out[1480], 1.0, 1e-4);
    CHECK_NEAR(out[1481], 0.0, 1e-4);
    CHECK_NEAR(out[1960], 0.5, 1e-4);
    CHECK_NEAR(out[2440], 0.25, 1e-4);
    CHECK(LND_NodeSetParam(dl, LND_DSP_PARAM_MIX, 0.5f) == LND_OK && LND_NodeSetParam(dl, LND_DSP_PARAM_FEEDBACK, 0.0f) == LND_OK);
    CHECK(LND_SourceSeekFrames(isrc, 0) == LND_OK);
    CHECK(LND_SoundReadF32(ds, out, 3000) == 3000);
    CHECK_NEAR(out[1000], 0.5, 1e-3);
    CHECK_NEAR(out[1480], 0.5, 1e-3);
    CHECK_NEAR(out[1960], 0.0, 1e-3);
    CHECK(LND_NodeFree(dl) == LND_OK);

    LND_BUFFER *mb = make_sine(sample_rate_hz, 1, 440.0, 0.5, 1.0);
    LND_SOURCE *msrc = LND_SourceCreateBuffer(mb);
    CHECK(LND_NodeCreatePanner(sample_rate_hz, 2.0f, LND_PAN_MONO) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    LND_NODE *pan = LND_NodeCreatePanner(sample_rate_hz, -1.0f, LND_PAN_MONO);
    CHECK(pan && LND_NodeGetChannels(pan) == 2 && LND_NodeGetParam(pan, LND_DSP_PARAM_PAN) == -1.0f);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(msrc), pan) == LND_OK);
    LND_SOUND *ps = LND_NodeEnsureSound(pan, nullptr);
    CHECK(ps && LND_SoundPlay(LND_SourceEnsureSound(msrc, nullptr)) == LND_OK && LND_SoundReadF32(ps, out, 4800) == 4800);
    l = analyze(out, 4800, 2, 0, sample_rate_hz);
    r = analyze(out, 4800, 2, 1, sample_rate_hz);
    CHECK_NEAR(l.rms, 0.5 / sqrt(2.0), 0.01);
    CHECK(r.peak < 1e-6);
    CHECK(LND_NodeSetParam(pan, LND_DSP_PARAM_PAN, 0.0f) == LND_OK);
    CHECK(LND_SoundReadF32(ps, out, 4800) == 4800 && LND_SoundReadF32(ps, out, 4800) == 4800);
    l = analyze(out, 4800, 2, 0, sample_rate_hz);
    r = analyze(out, 4800, 2, 1, sample_rate_hz);
    CHECK_NEAR(l.rms, 0.5 / sqrt(2.0) * sqrt(0.5), 0.01);
    CHECK_NEAR(r.rms, 0.5 / sqrt(2.0) * sqrt(0.5), 0.01);
    CHECK(LND_NodeSetParam(pan, LND_DSP_PARAM_PAN_MODE, (float)LND_PAN_STEREO) == LND_OK && LND_NodeSetParam(pan, LND_DSP_PARAM_PAN, 1.0f) == LND_OK);
    CHECK(LND_SoundReadF32(ps, out, 4800) == 4800 && LND_SoundReadF32(ps, out, 4800) == 4800);
    l = analyze(out, 4800, 2, 0, sample_rate_hz);
    r = analyze(out, 4800, 2, 1, sample_rate_hz);
    CHECK(l.peak < 1e-3);
    CHECK_NEAR(r.rms, 2.0 * 0.5 / sqrt(2.0), 0.02);
    CHECK(LND_NodeFree(pan) == LND_OK);

    LND_BUFFER *sb = make_sine(sample_rate_hz, 2, 440.0, 0.5, 1.0);
    LND_SOURCE *ssrc = LND_SourceCreateBuffer(sb);
    LND_NODE *meter = LND_NodeCreateMeter(2, sample_rate_hz, 300.0f);
    CHECK(meter && LND_NodeGetMeterPeak(meter, 0) == 0.0f && LND_NodeGetMeterRms(meter, 1) == 0.0f && LND_NodeGetMeterPeak(meter, 2) == 0.0f);
    CHECK(LND_NodeGetMeterPeak(LND_SourceEnsureNode(ssrc), 0) == 0.0f);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(ssrc), meter) == LND_OK);
    LND_SOUND *mtr = LND_NodeEnsureSound(meter, nullptr);
    CHECK(mtr && LND_SoundPlay(LND_SourceEnsureSound(ssrc, nullptr)) == LND_OK && LND_SoundReadF32(mtr, out, 4800) == 4800);
    CHECK_NEAR(LND_NodeGetMeterPeak(meter, 0), 0.5, 0.01);
    CHECK_NEAR(LND_NodeGetMeterRms(meter, 1), 0.5 / sqrt(2.0), 0.02);
    LND_SOUND *ssnd = LND_SourceEnsureSound(ssrc, nullptr);
    CHECK(LND_SoundSeekFrames(ssnd, 47000) == LND_OK);
    CHECK(LND_SoundReadF32(mtr, out, 9600) == 9600);
    CHECK(LND_NodeGetMeterPeak(meter, 0) < 0.5 * 0.85 && LND_NodeGetMeterPeak(meter, 0) > 0.5 * 0.5);
    CHECK(LND_NodeFree(meter) == LND_OK);
    LND_SourceFree(isrc);
    LND_SourceFree(msrc);
    LND_SourceFree(ssrc);
    LND_BufferFree(ib);
    LND_BufferFree(mb);
    LND_BufferFree(sb);
#endif
    LND_SourceFree(src);
    LND_BufferFree(b);
    LND_LibraryFree();
}

static void test_channel_nodes(void) {
    printf("channel splitter merger\n");
    setup_null(false, 2, 1024);
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    CHECK(LND_LibraryInit() == LND_OK);
    const uint32_t sample_rate_hz = 48000, frames = 48000;
    LND_BUFFER *b = LND_BufferCreate(LND_FORMAT_F32, 2, sample_rate_hz, frames);
    float *pcm = LND_BufferGetData(b);
    for (uint32_t i = 0; i < frames; i++) {
        pcm[i * 2] = (float)(0.5 * sin(TWO_PI * 440.0 * i / sample_rate_hz));
        pcm[i * 2 + 1] = 0.25f;
    }
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    LND_NODE *cs = LND_NodeCreateChannelSplitter(2, sample_rate_hz);
    CHECK(cs && LND_NodeGetType(cs) == LND_NODE_CHANNEL_SPLITTER && LND_NodeGetSplitterOutputCount(cs) == 2 && LND_NodeGetChannels(cs) == 2);
    LND_NODE *c0 = LND_NodeGetSplitterOutput(cs, 0), *c1 = LND_NodeGetSplitterOutput(cs, 1);
    CHECK(c0 && c1 && c0 != c1 && LND_NodeGetType(c0) == LND_NODE_CHANNEL && LND_NodeGetChannels(c0) == 1 && LND_NodeGetSampleRateHz(c1) == sample_rate_hz);
    CHECK(LND_NodeGetSplitterOutput(cs, 2) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeFree(c0) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(src), c0) != LND_OK && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    LND_NODE *m = LND_NodeCreateChannelMerger(2, sample_rate_hz);
    CHECK(m && LND_NodeGetType(m) == LND_NODE_CHANNEL_MERGER && LND_NodeGetChannels(m) == 2);
    CHECK(LND_NodeConnect(cs, m) != LND_OK && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(src), cs) == LND_OK);
    CHECK(LND_NodeSetChannelMergerInput(m, 2, c0) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetChannelMergerInput(LND_SourceEnsureNode(src), 0, c0) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetChannelMergerInput(m, 0, cs) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetChannelMergerInput(m, 0, c1) == LND_OK && LND_NodeSetChannelMergerInput(m, 1, c0) == LND_OK);
    CHECK(LND_NodeGetInputCount(m) == 2 && LND_NodeGetOutputCount(c0) == 1);
    LND_SOUND *ms = LND_NodeEnsureSound(m, nullptr);
    CHECK(ms && LND_SoundPlay(LND_SourceEnsureSound(src, nullptr)) == LND_OK);
    static float out[4800 * 2];
    CHECK(LND_SoundReadF32(ms, out, 4800) == 4800);
    double err = 0.0;
    for (uint32_t i = 256; i < 4800; i++)
        err = fmax(err, fmax(fabs(out[i * 2] - pcm[i * 2 + 1]), fabs(out[i * 2 + 1] - pcm[i * 2])));
    CHECK(err < 1e-6);
    CHECK(LND_NodeSetChannelMergerInput(m, 0, c0) == LND_OK && LND_NodeGetInputCount(m) == 2);
    CHECK(LND_SoundReadF32(ms, out, 4800) == 4800);
    err = 0.0;
    for (uint32_t i = 0; i < 4800; i++) {
        const float *in = pcm + (4800 + i) * 2;
        err = fmax(err, fmax(fabs(out[i * 2] - (in[0] + in[1])), fabs(out[i * 2 + 1])));
    }
    CHECK(err < 1e-6);
    LND_NODE *m2 = LND_NodeCreateChannelMerger(2, sample_rate_hz);
    CHECK(m2 && LND_NodeConnect(c0, m2) == LND_OK && LND_NodeConnect(c1, m2) == LND_OK);
    LND_SOUND *ms2 = LND_NodeEnsureSound(m2, nullptr);
    CHECK(ms2 && LND_SoundReadF32(ms2, out, 4800) == 4800);
    err = 0.0;
    for (uint32_t i = 0; i < 4800 * 2; i++)
        err = fmax(err, fabs(out[i] - pcm[9600 * 2 + i]));
    CHECK(err < 1e-6);
    LND_BUFFER *b3 = LND_BufferCreate(LND_FORMAT_F32, 2, sample_rate_hz, frames);
    float *dc = LND_BufferGetData(b3);
    for (uint32_t i = 0; i < frames; i++) {
        dc[i * 2] = 0.5f;
        dc[i * 2 + 1] = 0.25f;
    }
    LND_SOURCE *src3 = LND_SourceCreateBuffer(b3);
    LND_NODE *m3 = LND_NodeCreateChannelMerger(2, sample_rate_hz);
    CHECK(m3 && LND_NodeSetChannelMergerInput(m3, 1, LND_SourceEnsureNode(src3)) == LND_OK);
    LND_SOUND *ms3 = LND_NodeEnsureSound(m3, nullptr);
    CHECK(ms3 && LND_SoundPlay(LND_SourceEnsureSound(src3, nullptr)) == LND_OK && LND_SoundReadF32(ms3, out, 4800) == 4800);
    err = 0.0;
    for (uint32_t i = 256; i < 4800; i++)
        err = fmax(err, fmax(fabs(out[i * 2]), fabs(out[i * 2 + 1] - 0.375f)));
    CHECK(err < 1e-5);
    CHECK(LND_NodeFree(cs) == LND_OK);
    CHECK(LND_NodeGetInputCount(m) == 0 && LND_NodeGetInputCount(m2) == 0);
    CHECK(LND_NodeFree(m) == LND_OK && LND_NodeFree(m2) == LND_OK && LND_NodeFree(m3) == LND_OK);
    LND_SourceFree(src);
    LND_SourceFree(src3);
    LND_BufferFree(b);
    LND_BufferFree(b3);
    LND_LibraryFree();
}

#if LND_MODULE_SINK
static void test_sink(void) {
    printf("sink\n");
    setup_null(true, 2, 48000 * 4);
    CHECK(LND_LibraryInit() == LND_OK);
    const uint32_t sample_rate_hz = 48000;
    LND_BUFFER *b = LND_BufferCreate(LND_FORMAT_F32, 2, sample_rate_hz, sample_rate_hz * 3);
    float *pcm = LND_BufferGetData(b);
    for (uint32_t i = 0; i < sample_rate_hz * 3; i++) {
        pcm[i * 2] = (float)(0.5 * sin(TWO_PI * 440.0 * i / sample_rate_hz));
        pcm[i * 2 + 1] = (float)(0.5 * sin(TWO_PI * 660.0 * i / sample_rate_hz));
    }
    LND_SOURCE *src = LND_SourceCreateBuffer(b);
    CHECK(LND_NodeCreateSink(nullptr, nullptr) == nullptr && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeGetSinkOutput(LND_DeviceEnsureOutputNode()) == nullptr);
    LND_ENCODER_PARAMS p = {.channels = 2, .sample_rate_hz = 44100};
    LND_OUTPUT *o = LND_OutputCreateFile("sink_test.wav", &p);
    CHECK(o != nullptr);
    LND_NODE *sink = LND_NodeCreateSink(o, nullptr);
    CHECK(sink && LND_NodeGetType(sink) == LND_NODE_TERMINAL && LND_NodeGetChannels(sink) == 2 && LND_NodeGetSampleRateHz(sink) == 44100 && LND_NodeGetSinkOutput(sink) == o);
    CHECK(LND_NodeConnect(sink, LND_DeviceEnsureOutputNode()) != LND_OK && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(src), sink) == LND_OK);
    CHECK(LND_NodeConnect(LND_SourceEnsureNode(src), LND_DeviceEnsureOutputNode()) == LND_OK);
    sleep_ms(150);
    uint64_t silent = LND_OutputGetFrames(o);
    CHECK(silent > 44100 / 20 && silent < 44100);
    LND_SOUND *s = LND_SourceEnsureSound(src, nullptr);
    CHECK(LND_SoundPlay(s) == LND_OK);
    sleep_ms(500);
    uint64_t during = LND_OutputGetFrames(o);
    CHECK(during > silent + 44100 / 4 && during < silent + 44100);
    CHECK(LND_NodeGetPositionFrames(sink) >= during);
    CHECK(LND_NodeFree(sink) == LND_OK);
    uint64_t total = LND_OutputGetFrames(o);
    CHECK(total >= during);
    CHECK(LND_OutputFree(o) == LND_OK);
    CHECK(cap.frames > 0);
    LND_SOURCE *back = LND_SourceCreateFile("sink_test.wav", LND_ENCODED_SOURCE_PRELOAD, nullptr);
    CHECK(back && LND_SourceGetSampleRateHz(back) == 44100 && LND_SourceGetChannels(back) == 2 && LND_SourceGetLengthFrames(back) == total);
    if (back) {
        float *rec = malloc((size_t)total * 2 * sizeof(float));
        CHECK(LND_SourceRead(back, rec, LND_FORMAT_F32, total) == total);
        size_t start = 0;
        while (start < total && fabsf(rec[start * 2]) < 1e-4f)
            start++;
        CHECK(start >= silent / 2 && start + 44100 * 3 / 10 < total);
        if (!(start >= silent / 2 && start + 44100 * 3 / 10 < total))
            printf("  sink: start %zu silent %llu during %llu total %llu\n", start, (unsigned long long)silent, (unsigned long long)during,
                   (unsigned long long)total);
        if (start + 44100 * 3 / 10 < total) {
            stats l = analyze(rec + (start + 2205) * 2, 11025, 2, 0, 44100);
            stats r = analyze(rec + (start + 2205) * 2, 11025, 2, 1, 44100);
            CHECK_NEAR(l.freq, 440.0, 3.0);
            CHECK_NEAR(r.freq, 660.0, 4.0);
            CHECK_NEAR(l.rms, 0.5 / sqrt(2.0), 0.02);
            if (fabs(l.freq - 440.0) > 3.0)
                printf("  sink: freq %.2f/%.2f rms %.4f total %llu start %zu\n", l.freq, r.freq, l.rms, (unsigned long long)total, start);
        }
        free(rec);
        LND_SourceFree(back);
    }
    remove("sink_test.wav");
    LND_ENCODER_PARAMS p2 = {.channels = 2, .sample_rate_hz = sample_rate_hz};
    LND_OUTPUT *o2 = LND_OutputCreateFile("sink_test2.wav", &p2);
    LND_NODE *sink2 = LND_NodeCreateSink(o2, LND_DeviceEnsureOutputInstance());
    CHECK(o2 && sink2 && LND_NodeConnect(LND_SourceEnsureNode(src), sink2) == LND_OK);
    sleep_ms(100);
    CHECK(LND_OutputGetFrames(o2) > 0);
    CHECK(LND_OutputFree(o2) == LND_OK && LND_NodeGetSinkOutput(sink2) == nullptr);
    sleep_ms(50);
    CHECK(LND_NodeFree(sink2) == LND_OK);
    remove("sink_test2.wav");
    LND_OUTPUT *o3 = LND_OutputCreateFile("sink_test3.wav", &p);
    LND_NODE *sink3 = LND_NodeCreateSink(o3, nullptr);
    CHECK(o3 && sink3 && LND_NodeConnect(LND_SourceEnsureNode(src), sink3) == LND_OK);
    sleep_ms(50);
    LND_SourceFree(src);
    LND_BufferFree(b);
    LND_LibraryFree();
    remove("sink_test3.wav");
}

#endif

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "split_mix") == 0) {
        test_split_mix();
        free(cap.data);
        printf("%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    if (argc == 2 && strcmp(argv[1], "capture") == 0) {
        test_capture();
        free(cap.data);
        printf("%d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    test_config();
    test_ring();
    test_convert();
    test_simd();
    test_channels();
    test_buffers();
    test_playback_buffer();
    test_playback_proc(0);
    test_playback_proc(LND_GRAPH_SOURCE_DIRECT);
    test_resample(0, 44100);
    test_resample(1, 44100);
    test_resample(2, 44100);
    test_resample(3, 96000);
    test_control();
    test_loop_and_end();
    test_instances();
    test_free_while_playing();
    test_graph_chain();
    test_graph_split();
    test_graph_loopback();
    test_graph_cycle();
    test_graph_drain();
    test_soft_clip();
    test_render();
    test_sound_data();
    test_capture();
    test_split_mix();
    test_output();
    test_effects();
    test_channel_nodes();
#if LND_MODULE_SINK
    test_sink();
#endif
    test_bits();
    test_codec_list();
    test_file_wav();
    test_file_playback();
    test_file_transport();
    test_aiff();
    test_mp3();
    test_internal_codecs();
    test_ffmpeg();
    test_system_codecs();
    test_custom_codec();
    free(cap.data);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
