#include "lindar_output.h"
#include "lindar.h"
#include "lindar_codecs.h"
#include "pcm_fixture.h"

#include <math.h>

enum { FRAMES = 5003, CHANNELS = 3 };
static float expected[FRAMES * CHANNELS];
static size_t position, limit;
static int32_t write_error;
static LND_OUTPUT *current;

static int32_t encoder_open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state) {
    (void)io;
    (void)extension;
    CHECK(params->channels == CHANNELS && params->sample_rate_hz == 48000);
    position = 0;
    *state = &position;
    return LND_OK;
}

static int32_t encoder_write(void *state, const float *pcm, uint64_t frames) {
    CHECK(state == &position && position + frames <= limit);
    CHECK(LND_OutputWrite(current, nullptr, LND_FORMAT_F32, 0) == LND_ERR_BUSY);
    if (write_error) return write_error;
    if (position + frames <= limit)
        for (size_t i = 0; i < frames * CHANNELS; i++)
            CHECK(pcm[i] == expected[position * CHANNELS + i]);
    position += (size_t)frames;
    return LND_OK;
}

static int32_t encoder_close(void *state) {
    CHECK(state == &position);
    return LND_OK;
}

static size_t discard(void *user, const void *data, size_t bytes) {
    (void)user;
    (void)data;
    return bytes;
}

static const LND_ENCODER encoder = {.name = "planar-test", .open = encoder_open, .write = encoder_write, .close = encoder_close};
static const LND_IO_OUTPUT_PROCS procs = {.write = discard};

static void run(int32_t format, int32_t layout) {
    pcm_fixture input;
    fixture_init(&input, CHANNELS, FRAMES + 2, format, layout, 3);
    float values[FRAMES * CHANNELS];
    for (size_t i = 0; i < FRAMES * CHANNELS; i++)
        values[i] = (float)((int)(i % 121) - 60) / 64;
    LND_PCM packed = {.data = values, .frames = FRAMES, .channels = CHANNELS, .format = LND_FORMAT_F32};
    CHECK(LND_PcmConvert(&input.pcm, 1, &packed, 0, FRAMES) == LND_OK);
    packed.data = expected;
    CHECK(LND_PcmConvert(&packed, 0, &input.pcm, 1, FRAMES) == LND_OK);
    LND_ENCODER_PARAMS params = {.encoder_name = encoder.name, .channels = CHANNELS, .sample_rate_hz = 48000};
    current = LND_OutputCreateProc(&procs, nullptr, &params);
    CHECK(current != nullptr);
    limit = FRAMES;
    deny_alloc = true;
    CHECK(LND_OutputWritePcm(current, &input.pcm, FRAMES + 1, 3) == LND_ERR_INVALID_ARG);
    CHECK(LND_OutputWritePcm(current, &input.pcm, SIZE_MAX, 1) == LND_ERR_INVALID_ARG);
    LND_PCM invalid = input.pcm;
    invalid.channels = 2;
    CHECK(LND_OutputWritePcm(current, &invalid, 1, FRAMES) == LND_ERR_INVALID_ARG);
    invalid = input.pcm;
    invalid.stride_bytes = SIZE_MAX;
    CHECK(LND_OutputWritePcm(current, &invalid, 1, FRAMES) == LND_ERR_INVALID_ARG);
    invalid = input.pcm;
    invalid.data = nullptr;
    invalid.planes = nullptr;
    CHECK(LND_OutputWritePcm(current, &invalid, 1, FRAMES) == LND_ERR_INVALID_ARG);
    CHECK(position == 0 && LND_OutputGetFrames(current) == 0);
    CHECK(LND_OutputWritePcm(current, &input.pcm, 1, FRAMES) == LND_OK);
    CHECK(position == FRAMES && LND_OutputGetFrames(current) == FRAMES);
    CHECK(LND_OutputFinish(current) == LND_OK);
    CHECK(LND_OutputWritePcm(current, &input.pcm, 0, 0) == LND_ERR_STATE);
    CHECK(LND_OutputFree(current) == LND_OK);
    deny_alloc = false;
    fixture_guard(&input, 1, FRAMES);
    fixture_free(&input);
}

static void aliases_and_errors(void) {
    float mono[] = {0.25f, -0.5f, 0.75f};
    void *planes[] = {mono, mono, mono};
    LND_PCM pcm = {.planes = planes, .frames = 3, .channels = 3, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_PLANAR};
    for (size_t f = 0; f < 3; f++)
        for (size_t c = 0; c < 3; c++)
            expected[f * 3 + c] = mono[f];
    LND_ENCODER_PARAMS params = {.encoder_name = encoder.name, .channels = CHANNELS, .sample_rate_hz = 48000};
    limit = 3;
    for (int error = 0; error < 3; error++) {
        current = LND_OutputCreateProc(&procs, nullptr, &params);
        CHECK(current != nullptr);
        write_error = error == 1 ? LND_ERR_FORMAT : error == 2 ? LND_SOURCE_WAITING : 0;
        int32_t result = error == 1 ? LND_ERR_FORMAT : error == 2 ? LND_ERR_IO : LND_OK;
        deny_alloc = true;
        CHECK(LND_OutputWritePcm(current, &pcm, 0, 3) == result);
        CHECK(LND_OutputGetFrames(current) == (error ? 0 : 3));
        CHECK(mono[0] == 0.25f && mono[1] == -0.5f && mono[2] == 0.75f);
        CHECK(LND_OutputFree(current) == result);
        deny_alloc = false;
    }
    write_error = 0;
}

#if LND_MODULE_WAV_ENCODER && LND_MODULE_WAV_DECODER
typedef struct memory_output {
    unsigned char data[65536];
    size_t size, position;
} memory_output;

static size_t memory_write(void *user, const void *data, size_t bytes) {
    memory_output *output = user;
    if (bytes > sizeof output->data - output->position) return 0;
    memcpy(output->data + output->position, data, bytes);
    output->position += bytes;
    if (output->position > output->size) output->size = output->position;
    return bytes;
}

static int32_t memory_seek(void *user, uint64_t position) {
    memory_output *output = user;
    if (position > output->size) return LND_ERR_IO;
    output->position = (size_t)position;
    return LND_OK;
}

static void wave(void) {
    memory_output memory = {0};
    LND_IO_OUTPUT_PROCS callbacks = {.write = memory_write, .seek = memory_seek};
    LND_ENCODER_PARAMS params = {.encoder_name = "wav", .channels = 2, .sample_rate_hz = 48000, .format = LND_FORMAT_S24};
    LND_OUTPUT *output = LND_OutputCreateProc(&callbacks, &memory, &params);
    pcm_fixture input, decoded;
    fixture_init(&input, 2, 515, LND_FORMAT_S24, LND_LAYOUT_PLANAR, 2);
    fixture_init(&decoded, 2, 515, LND_FORMAT_S24, LND_LAYOUT_PLANAR, 5);
    int32_t values[1026], restored[1026];
    for (size_t i = 0; i < 1026; i++)
        values[i] = ((int32_t)i - 513) * 1048576;
    LND_PCM packed = {.data = values, .frames = 513, .channels = 2, .format = LND_FORMAT_S32};
    CHECK(LND_PcmConvert(&input.pcm, 1, &packed, 0, 513) == LND_OK);
    CHECK(output != nullptr);
    deny_alloc = true;
    CHECK(LND_OutputWritePcm(output, &input.pcm, 1, 513) == LND_OK);
    CHECK(LND_OutputFree(output) == LND_OK);
    deny_alloc = false;
    LND_SOURCE *source = LND_SourceCreateEncodedMemory(memory.data, memory.size, 0, nullptr);
    CHECK(source != nullptr);
    deny_alloc = true;
    CHECK(LND_SourceReadPcm(source, &decoded.pcm, 1, 513) == 513);
    packed.data = restored;
    CHECK(LND_PcmConvert(&packed, 0, &decoded.pcm, 1, 513) == LND_OK);
    CHECK(memcmp(values, restored, sizeof values) == 0);
    CHECK(LND_SourceFree(source) == LND_OK);
    deny_alloc = false;
    fixture_guard(&input, 1, 513);
    fixture_guard(&decoded, 1, 513);
    fixture_free(&input);
    fixture_free(&decoded);
}
#endif

int main(void) {
    for (int layout = 0; layout < 2; layout++) {
        begin(LND_FORMAT_S16, layout);
        CHECK(LND_EncoderRegister(&encoder) == LND_OK);
        CHECK(LND_EncoderRegister(&encoder) == LND_OK);
        LND_ENCODER duplicate = encoder;
        CHECK(LND_EncoderRegister(&duplicate) == LND_ERR_INVALID_ARG);
        duplicate.name = "";
        CHECK(LND_EncoderRegister(&duplicate) == LND_ERR_INVALID_ARG);
        CHECK(LND_EncoderFind("planar-test") == &encoder);
        CHECK(!strcmp(LND_EncoderGetName(&encoder), "planar-test"));
        CHECK(!LND_EncoderGetExtensions(&encoder) && !LND_EncoderGetFlags(&encoder));
        CHECK(!LND_EncoderGetName(nullptr) && !LND_EncoderGetExtensions(nullptr) && !LND_EncoderGetFlags(nullptr));
        for (int format = LND_FORMAT_U8; format <= LND_FORMAT_F64; format++)
            for (int input = 0; input < 2; input++)
                run(format, input);
        aliases_and_errors();
        CHECK(LND_EncoderUnregister(&encoder) == LND_OK);
        CHECK(!LND_EncoderFind("planar-test"));
#if LND_MODULE_WAV_ENCODER && LND_MODULE_WAV_DECODER
        wave();
#endif
        finish();
    }
    return report();
}
