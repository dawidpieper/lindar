#include "../codec_test.h"
#define lnd_time_ns mock_time_ns
#include "formats/codecs/mediacodec/decoder.c"
#undef lnd_time_ns

static unsigned objects, calls, fail_at, releases, queued_eos, output_events, corrupt;
static int64_t duration = 1249;
static int api = 35, encoding = 2, changed;
uint64_t mock_time_ns(void) {
    static uint64_t ticks;
    return ticks += UINT64_C(100000000);
}
static bool fail(void) { return ++calls == fail_at; }
const char *AMEDIAFORMAT_KEY_CHANNEL_COUNT = "channels", *AMEDIAFORMAT_KEY_SAMPLE_RATE = "rate", *AMEDIAFORMAT_KEY_PCM_ENCODING = "encoding",
           *AMEDIAFORMAT_KEY_MIME = "mime", *AMEDIAFORMAT_KEY_DURATION = "durationUs";
struct AMediaDataSource {
    void *user;
    AMediaDataSourceReadAt read;
    AMediaDataSourceGetSize size;
    AMediaDataSourceClose close;
};
struct AMediaExtractor {
    AMediaDataSource *input;
    unsigned sample;
};
struct AMediaFormat {
    bool video;
    int32_t sample_rate_hz;
};
struct AMediaCodec {
    unsigned output, queued, events;
    bool held, eos;
    uint8_t input[8];
    alignas(8) uint8_t buffer[64];
};
static void *object(size_t size) {
    if (fail()) return nullptr;
    objects++;
    return calloc(1, size);
}
static void destroy(void *p) {
    CHECK(p != nullptr && objects > 0);
    objects--;
    free(p);
}
int android_get_device_api_level(void) { return api; }
AMediaDataSource *AMediaDataSource_new(void) { return object(sizeof(AMediaDataSource)); }
void AMediaDataSource_delete(AMediaDataSource *s) {
    if (s->close) s->close(s->user);
    destroy(s);
}
void AMediaDataSource_setUserdata(AMediaDataSource *s, void *user) { s->user = user; }
void AMediaDataSource_setReadAt(AMediaDataSource *s, AMediaDataSourceReadAt p) { s->read = p; }
void AMediaDataSource_setGetSize(AMediaDataSource *s, AMediaDataSourceGetSize p) { s->size = p; }
void AMediaDataSource_setClose(AMediaDataSource *s, AMediaDataSourceClose p) { s->close = p; }
AMediaExtractor *AMediaExtractor_new(void) { return object(sizeof(AMediaExtractor)); }
media_status_t AMediaExtractor_delete(AMediaExtractor *s) {
    destroy(s);
    return AMEDIA_OK;
}
media_status_t AMediaExtractor_setDataSourceCustom(AMediaExtractor *s, AMediaDataSource *input) {
    if (fail()) return -1;
    s->input = input;
    CHECK(input->size(input->user) == 8);
    char bytes[8];
    CHECK(input->read(input->user, 0, bytes, 8) == 8 && !memcmp(bytes, "testdata", 8));
    CHECK(input->read(input->user, -1, bytes, 8) == -1);
    CHECK(input->read(input->user, 4, bytes, 8) == 4 && !memcmp(bytes, "data", 4));
    CHECK(input->read(input->user, 8, bytes, 4) == 0);
    CHECK(input->read(input->user, 9, bytes, 4) == 0);
    CHECK(input->read(input->user, 8, bytes, 0) == 0);
    return AMEDIA_OK;
}
size_t AMediaExtractor_getTrackCount(AMediaExtractor *s) { return 2; }
AMediaFormat *AMediaExtractor_getTrackFormat(AMediaExtractor *s, size_t index) {
    AMediaFormat *f = object(sizeof *f);
    if (f) f->video = index == 0;
    return f;
}
media_status_t AMediaExtractor_selectTrack(AMediaExtractor *s, size_t i) {
    CHECK(i == 1);
    return fail() ? -1 : AMEDIA_OK;
}
media_status_t AMediaExtractor_unselectTrack(AMediaExtractor *s, size_t i) { return AMEDIA_OK; }
ssize_t AMediaExtractor_getSampleSize(AMediaExtractor *s) { return s->sample < 2 ? 2 : -1; }
ssize_t AMediaExtractor_readSampleData(AMediaExtractor *s, uint8_t *dst, size_t capacity) {
    CHECK(capacity >= 2);
    dst[0] = (uint8_t)s->sample;
    return 2;
}
uint32_t AMediaExtractor_getSampleFlags(AMediaExtractor *s) { return corrupt == 1 ? AMEDIAEXTRACTOR_SAMPLE_FLAG_ENCRYPTED : 0; }
int64_t AMediaExtractor_getSampleTime(AMediaExtractor *s) { return s->sample * 625; }
bool AMediaExtractor_advance(AMediaExtractor *s) { return ++s->sample < 2; }
media_status_t AMediaExtractor_seekTo(AMediaExtractor *s, int64_t time, SeekMode mode) {
    CHECK(mode == AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    if (corrupt == 2) return -1;
    s->sample = 0;
    return AMEDIA_OK;
}
AMediaCodec *AMediaCodec_createDecoderByType(const char *mime) {
    CHECK(!strcmp(mime, "audio/mock"));
    return object(sizeof(AMediaCodec));
}
media_status_t AMediaCodec_configure(AMediaCodec *s, const AMediaFormat *f, void *window, void *crypto, uint32_t flags) {
    CHECK(!window && !crypto && !flags && !f->video);
    return fail() ? -1 : AMEDIA_OK;
}
media_status_t AMediaCodec_start(AMediaCodec *s) { return fail() ? -1 : AMEDIA_OK; }
media_status_t AMediaCodec_stop(AMediaCodec *s) {
    CHECK(!s->held);
    return AMEDIA_OK;
}
media_status_t AMediaCodec_delete(AMediaCodec *s) {
    CHECK(!s->held);
    destroy(s);
    return AMEDIA_OK;
}
media_status_t AMediaCodec_flush(AMediaCodec *s) {
    CHECK(!s->held);
    memset(s, 0, sizeof *s);
    return AMEDIA_OK;
}
uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *s, size_t index, size_t *size) {
    *size = sizeof s->input;
    return s->input;
}
uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *s, size_t index, size_t *size) {
    CHECK(s->held);
    *size = api >= 36 ? sizeof s->buffer : 0;
    return s->buffer;
}
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec *s, int64_t timeout) { return s->eos ? -1 : 0; }
media_status_t AMediaCodec_queueInputBuffer(AMediaCodec *s, size_t index, off64_t offset, size_t size, uint64_t time, uint32_t flags) {
    if (flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
        CHECK(!s->eos && size == 0);
        queued_eos++;
        s->eos = true;
    } else {
        CHECK(size == 2 && offset == 0);
        s->queued++;
    }
    return AMEDIA_OK;
}
ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *s, AMediaCodecBufferInfo *info, int64_t timeout) {
    CHECK(!s->held);
    output_events++;
    if (corrupt == 4) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    if (s->events++ == 0) return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;
    if (s->events == 2) return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;
    if (s->events == 3) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    if (changed && s->output == 1 && s->events == 4) return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;
    if (s->output >= s->queued) return AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    size_t offset = api >= 36 ? 8 : 0;
    if (encoding == 4) {
        float *pcm = (float *)(s->buffer + offset);
        for (unsigned i = 0; i < 10; i++)
            pcm[i] = (float)(10 * s->output + i);
    } else {
        int16_t *pcm = (int16_t *)(s->buffer + offset);
        for (unsigned i = 0; i < 10; i++)
            pcm[i] = (int16_t)(10 * s->output + i);
    }
    *info = (AMediaCodecBufferInfo){.offset = api >= 36 ? 8 : 999,
                                    .size = encoding == 4 ? 40 : 20,
                                    .presentationTimeUs = s->output * 625,
                                    .flags = s->output == 1 ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0};
    if (corrupt == 3 || corrupt == 5) info->size = 999;
    if (corrupt == 5 || corrupt == 6) info->flags = AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
    if (corrupt == 6) info->size = 0;
    s->held = true;
    s->output++;
    return 0;
}
media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec *s, size_t index, bool render) {
    CHECK(s->held && !render);
    s->held = false;
    releases++;
    return AMEDIA_OK;
}
AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *s) {
    AMediaFormat *format = object(sizeof *format);
    if (format) format->sample_rate_hz = s->events == 1 ? 4000 : 8000;
    return format;
}
media_status_t AMediaFormat_delete(AMediaFormat *f) {
    destroy(f);
    return AMEDIA_OK;
}
bool AMediaFormat_getInt64(AMediaFormat *f, const char *key, int64_t *value) {
    CHECK(key == AMEDIAFORMAT_KEY_DURATION && !f->video);
    if (duration == INT64_MIN) return false;
    *value = duration;
    return true;
}
bool AMediaFormat_getInt32(AMediaFormat *f, const char *key, int32_t *value) {
    *value = key == AMEDIAFORMAT_KEY_SAMPLE_RATE ? (changed ? 16000 : f->sample_rate_hz) : key == AMEDIAFORMAT_KEY_CHANNEL_COUNT ? 2 : encoding;
    return true;
}
bool AMediaFormat_getString(AMediaFormat *f, const char *key, const char **value) {
    *value = f->video ? "video/mock" : "audio/mock";
    return true;
}

typedef struct test_stream {
    size_t position, size;
    int32_t read_error, seek_error;
} test_stream;

static int64_t stream_read(void *user, void *dst, size_t size) {
    test_stream *s = user;
    if (s->read_error) return s->read_error;
    if (s->position >= s->size) return LND_READ_EOF;
    size_t take = LND_MIN(size, LND_MIN((size_t)2, s->size - s->position));
    static const char data[] = "testdata";
    memcpy(dst, data + s->position, take);
    s->position += take;
    return (int64_t)take;
}

static int32_t stream_seek(void *user, uint64_t position) {
    test_stream *s = user;
    if (s->seek_error) return s->seek_error;
    s->position = (size_t)position;
    return LND_OK;
}

static void test_input_callback(void) {
    test_stream input = {.size = 8};
    lnd_mc_state s = {.io = LND_IoCreateStream(&(LND_IO_STREAM_INPUT_PROCS){.read = stream_read, .seek = stream_seek}, &input, 8)};
    CHECK(s.io != nullptr);
    if (!s.io) return;
    lnd_mutex_init(&s.io_lock);
    char data[8];
    CHECK(lnd_mc_input(&s, 4, data, sizeof data) == 2 && !memcmp(data, "da", 2));
    CHECK(lnd_mc_input(&s, 6, data, sizeof data) == 2 && !memcmp(data, "ta", 2));
    CHECK(lnd_mc_input(&s, 8, data, sizeof data) == 0);
    input.size = 6;
    CHECK(lnd_mc_input(&s, 6, data, sizeof data) == 0);
    CHECK(lnd_mc_input(&s, -1, data, 1) == -1);
    CHECK(lnd_mc_input(&s, 0, data, (size_t)PTRDIFF_MAX + 1) == -1);
    input.read_error = LND_ERR_IO;
    CHECK(lnd_mc_input(&s, 0, data, sizeof data) < 0);
    input.read_error = 0;
    input.seek_error = LND_ERR_IO;
    CHECK(lnd_mc_input(&s, 4, data, sizeof data) < 0);
    input.seek_error = 0;
    CHECK(lnd_mc_input(&s, 0, data, sizeof data) == 2);
    lnd_mc_input_close(&s);
    CHECK(lnd_mc_input(&s, 0, data, sizeof data) < 0);
    CHECK(lnd_mc_input(&s, 8, data, sizeof data) < 0);
    CHECK(lnd_mc_input(&s, 0, data, 0) == 0);
    lnd_mutex_free(&s.io_lock);
    CHECK(LND_IoFree(s.io) == LND_OK);
}

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    test_input_callback();
    LND_IO *io = lnd_io_open_memory("xxxtestdatazzz", 14);
    CHECK(lnd_io_window(io, 3, 8) == LND_OK);
    const LND_CODEC *codec = LND_MediaCodecGetCodec();
    for (api = 33; api <= 36; api++) {
        for (encoding = 2; encoding <= 4; encoding += 2) {
            void *state = nullptr;
            LND_CODEC_INFO info;
            CHECK(codec->open(io, &info, &state) == LND_OK && state != nullptr);
            CHECK(info.sample_rate_hz == 8000 && info.channels == 2 && info.format == (encoding == 2 ? LND_FORMAT_S16 : LND_FORMAT_F32));
            CHECK(info.length_frames == 10 && info.length_estimated);
            alignas(8) uint8_t pcm[128];
            CHECK(codec->read(state, pcm, 3) == 3);
            CHECK(codec->seek(state, 3) == LND_OK);
            CHECK(codec->read(state, pcm, 12) == 7);
            CHECK(encoding == 2 ? ((int16_t *)pcm)[0] == 6 && ((int16_t *)pcm)[13] == 19 : ((float *)pcm)[0] == 6 && ((float *)pcm)[13] == 19);
            CHECK(codec->read(state, pcm, 1) == 0);
            CHECK(codec->seek(state, UINT64_MAX) == LND_ERR_INVALID_ARG);
            CHECK(codec->seek(state, 0) == LND_OK);
            CHECK(codec->read(state, pcm, 10) == 10);
            CHECK(encoding == 2 ? ((int16_t *)pcm)[0] == 0 : ((float *)pcm)[0] == 0);
            CHECK(codec->seek(state, 0) == LND_OK);
            changed = 1;
            CHECK(codec->read(state, pcm, 1) == LND_CODEC_READ_ERROR);
            changed = 0;
            CHECK(codec->seek(state, 0) == LND_OK);
            CHECK(codec->read(state, pcm, 1) == 1);
            corrupt = 2;
            CHECK(codec->seek(state, 0) == LND_ERR_IO);
            CHECK(codec->read(state, pcm, 1) == LND_CODEC_READ_ERROR);
            corrupt = 0;
            codec->close(state);
            CHECK(objects == 0);
        }
    }
    api = 36;
    encoding = 2;
    const int64_t durations[] = {INT64_MIN, -1, 0, 1, 1250, 1251, INT64_MAX};
    const uint64_t lengths[] = {0, 0, 0, 1, 10, 11, UINT64_C(73786976294838207)};
    for (unsigned i = 0; i < sizeof durations / sizeof *durations; i++) {
        duration = durations[i];
        void *state = nullptr;
        LND_CODEC_INFO info;
        CHECK(codec->open(io, &info, &state) == LND_OK);
        CHECK(info.length_frames == lengths[i] && info.length_estimated);
        codec->close(state);
        CHECK(objects == 0);
    }
    duration = 1249;
    CHECK(lnd_system_frames_ceil(UINT64_MAX, UINT32_MAX, 1000000) == UINT64_MAX);
    CHECK(lnd_system_frames_ceil(1000001, 8000, 1000000) == 8001);
    api = 36;
    encoding = 2;
    for (unsigned failure = 1; failure <= 10; failure++) {
        calls = 0;
        fail_at = failure;
        void *state = nullptr;
        LND_CODEC_INFO info;
        int32_t result = codec->open(io, &info, &state);
        if (state)
            codec->close(state);
        else
            CHECK(result != LND_OK);
        CHECK(objects == 0);
    }
    fail_at = 0;
    for (corrupt = 1; corrupt <= 5; corrupt++) {
        if (corrupt == 2) continue;
        void *state = nullptr;
        LND_CODEC_INFO info;
        CHECK(codec->open(io, &info, &state) != LND_OK && state == nullptr);
        CHECK(objects == 0);
    }
    corrupt = 6;
    void *state = nullptr;
    LND_CODEC_INFO info;
    CHECK(codec->open(io, &info, &state) == LND_OK);
    int16_t pcm[2];
    CHECK(codec->read(state, pcm, 1) == 0);
    codec->close(state);
    CHECK(objects == 0);
    fail_allocation = 0;
    state = nullptr;
    CHECK(codec->open(io, &info, &state) == LND_ERR_OUT_OF_MEMORY && !state);
    fail_allocation = -1;
    CHECK(queued_eos > 0 && releases > 0 && output_events > releases);
    lnd_io_close(io);
    LND_LibraryFree();
    CHECK(live_allocations == 0);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
