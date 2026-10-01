#include "codec_test.h"
#include <opusfile.h>
#include <math.h>

typedef struct reference_input {
    const uint8_t *data;
    size_t size, pos;
} reference_input;
static int ref_read(void *user, unsigned char *dst, int size) {
    reference_input *s = user;
    if (size <= 0) return 0;
    size_t n = s->size - s->pos;
    if (n > (size_t)size) n = (size_t)size;
    memcpy(dst, s->data + s->pos, n);
    s->pos += n;
    return (int)n;
}
static int ref_seek(void *user, opus_int64 offset, int whence) {
    reference_input *s = user;
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int64_t)s->pos : (int64_t)s->size;
    int64_t target = base + offset;
    if (target < 0 || (uint64_t)target > s->size) return -1;
    s->pos = (size_t)target;
    return 0;
}
static opus_int64 ref_tell(void *user) { return (opus_int64)((reference_input *)user)->pos; }
static const OpusFileCallbacks ref_procs = {.read = ref_read, .seek = ref_seek, .tell = ref_tell};

static void test_decode(const uint8_t *file, size_t bytes) {
    reference_input ref = {.data = file, .size = bytes};
    int error = 0;
    OggOpusFile *of = op_open_callbacks(&ref, &ref_procs, nullptr, 0, &error);
    CHECK(of != nullptr);
    if (!of) return;
    uint32_t channels = (uint32_t)op_channel_count(of, -1);
    uint64_t length = (uint64_t)op_pcm_total(of, -1);
    test_input input = {.data = file, .size = bytes, .limit = 17};
    LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "opus", .block_frames = 127};
    LND_SOURCE *source = LND_SourceCreateEncodedInput(&input_procs, &input, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
    CHECK(source != nullptr);
    if (!source) {
        op_free(of);
        return;
    }
    CHECK(LND_SourceGetLengthFrames(source) == length && LND_SourceGetSampleRateHz(source) == 48000 && LND_SourceGetChannels(source) == channels);
    CHECK(!strcmp(LND_SourceGetCodec(source)->name, "opus"));
    int16_t actual[512], expected[512];
    uint64_t position = 0;
    unsigned before = allocations;
    while (position < length) {
        uint32_t want = (uint32_t)(position % 211) + 1;
        if (want > length - position) want = (uint32_t)(length - position);
        int count = 0;
        while (count < (int)want) {
#if LND_TEST_OPUS_INTEGER
            int got = op_read(of, expected + count * channels, ((int)want - count) * (int)channels, nullptr);
#else
            float samples[512];
            int got = op_read_float(of, samples, ((int)want - count) * (int)channels, nullptr);
            if (got > 0) {
                LND_PCM src = {.data = samples, .frames = (size_t)got, .channels = channels, .format = LND_FORMAT_F32};
                LND_PCM dst = {.data = expected + count * channels, .frames = (size_t)got, .channels = channels, .format = LND_FORMAT_S16};
                CHECK(LND_PcmConvert(&dst, 0, &src, 0, (size_t)got) == LND_OK);
            }
#endif
            if (got == OP_HOLE) continue;
            CHECK(got > 0);
            if (got <= 0) break;
            count += got;
        }
        uint64_t got = LND_SourceRead(source, actual, LND_FORMAT_S16, want);
        CHECK(got == want && count == (int)want);
        if (!got) break;
        CHECK(!memcmp(actual, expected, (size_t)got * channels * 2));
        position += got;
    }
    CHECK(position == length && LND_SourceRead(source, actual, LND_FORMAT_S16, 1) == 0 && allocations == before);
    CHECK(LND_SourceSeekFrames(source, 0) == LND_OK && LND_SourceRead(source, actual, LND_FORMAT_S16, 100) == (length < 100 ? length : 100));
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_RENDERER_CONFIG config = {.render = LND_SoundRenderPcm, .user = sound, .channels = channels, .sample_rate_hz = 48000, .block_frames = 31};
    LND_RENDERER *renderer = LND_RendererCreateProc(&config);
    CHECK(sound && renderer && LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundSeekFrames(sound, length - 10) == LND_OK);
    before = allocations;
    LND_PCM pcm = {.data = actual, .channels = channels, .frames = 100, .format = LND_FORMAT_S16};
    CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 100) == LND_OK && LND_SourceGetPositionFrames(source) == 90 && allocations == before);
    CHECK(LND_RendererFree(renderer) == LND_OK && LND_SourceFree(source) == LND_OK && input.closes == 1);
    op_free(of);
}

#if LND_MODULE_OPUS_ENCODER
static void test_encode(void) {
    static test_output output;
    uint32_t frame_sizes[] = {120, 240, 480, 960, 1920, 2880};
    uint32_t lengths[] = {0, 1, 119, 120, 121, 960, 1003, 4096};
    float input[8192];
    for (size_t i = 0; i < 8192; i++) input[i] = (float)(0.5 * sin((double)(i / 2) * 0.07));
    for (size_t b = 0; b < sizeof frame_sizes / sizeof frame_sizes[0]; b++) {
        for (size_t n = 0; n < sizeof lengths / sizeof lengths[0]; n++) {
            output = (test_output){0};
            LND_ENCODER_PARAMS params = {.encoder_name = "opus", .channels = 2, .sample_rate_hz = 48000, .bitrate_kbps = 64, .frame_size_frames = frame_sizes[b]};
            LND_OUTPUT *o = LND_OutputCreateProc(&output_procs, &output, &params);
            CHECK(o != nullptr);
            if (!o) continue;
            CHECK(LND_OutputWrite(o, input, LND_FORMAT_F32, lengths[n]) == LND_OK);
            CHECK(LND_OutputFree(o) == LND_OK);
            CHECK(output.size > 100 && !memcmp(output.data, "OggS", 4));
            LND_SOURCE *source = LND_SourceCreateEncodedMemory(output.data, output.size, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
            CHECK(source != nullptr);
            if (source) {
                CHECK(LND_SourceGetLengthFrames(source) == lengths[n]);
                int16_t decoded[8194];
                CHECK(LND_SourceRead(source, decoded, LND_FORMAT_S16, lengths[n] + 1) == lengths[n]);
                CHECK(LND_SourceFree(source) == LND_OK);
            }
        }
    }
    LND_ENCODER_PARAMS invalid = {.encoder_name = "opus", .channels = 2, .sample_rate_hz = 48000, .frame_size_frames = 123};
    CHECK(!LND_OutputCreateProc(&output_procs, &output, &invalid) && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    invalid.frame_size_frames = 960;
    invalid.bitrate_kbps = UINT32_MAX;
    CHECK(!LND_OutputCreateProc(&output_procs, &output, &invalid) && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
}
#endif

int main(void) {
    size_t bytes;
    uint8_t *file = read_file("audiosamples/mcu.opus", &bytes);
    if (!file) return 1;
    for (int layout = LND_LAYOUT_INTERLEAVED; layout <= LND_LAYOUT_PLANAR; layout++) {
        test_init(layout);
        test_decode(file, bytes);
        uint8_t *chain = malloc(bytes * 2);
        memcpy(chain, file, bytes);
        memcpy(chain + bytes, file, bytes);
        test_decode(chain, bytes * 2);
        free(chain);
#if LND_MODULE_OPUS_ENCODER
        test_encode();
#endif
        LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "opus"};
        for (size_t size = 1; size < 47; size++) {
            CHECK(!LND_SourceCreateEncodedMemory(file, size, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options));
        }
        unsigned baseline = live_allocations;
        for (int failure = 0; failure < 9; failure++) {
            fail_allocation = failure;
            LND_SOURCE *source = LND_SourceCreateEncodedMemory(file, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
            if (source) LND_SourceFree(source);
            CHECK(live_allocations == baseline);
        }
        fail_allocation = -1;
        LND_LibraryFree();
        CHECK(!live_allocations);
    }
    free(file);
    printf("Ogg Opus files: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
