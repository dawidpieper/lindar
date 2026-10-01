#include "codec_test.h"

static void test_frame(const char *name, uint32_t frame, uint32_t bad_frame, const char *options) {
    static test_output output;
    output = (test_output){0};
    LND_ENCODER_PARAMS params = {.encoder_name = name, .channels = 1, .sample_rate_hz = 48000, .frame_size_frames = frame, .options = options};
    const LND_ENCODER *encoder = LND_EncoderFind(name);
    CHECK(encoder && (encoder->flags & LND_ENCODER_FLAG_FRAME_SIZE));
    LND_OUTPUT *o = LND_OutputCreateProc(&output_procs, &output, &params);
    CHECK(o != nullptr);
    if (o) {
        int16_t samples[1100];
        for (unsigned i = 0; i < 1100; i++) samples[i] = (int16_t)((i % 93) * 500 - 23000);
        CHECK(LND_OutputWrite(o, samples, LND_FORMAT_S16, 1100) == LND_OK);
        CHECK(LND_OutputFree(o) == LND_OK);
        CHECK(output.size > 32);
    }
    params.frame_size_frames = bad_frame;
    unsigned baseline = live_allocations;
    o = LND_OutputCreateProc(&output_procs, &output, &params);
    CHECK(!o);
    if (o) LND_OutputFree(o);
    CHECK(live_allocations == baseline);
}

static void test_allocations(void) {
    static const char *names[] = {
#if LND_MODULE_WAV_ENCODER
        "wav",
#endif
#if LND_MODULE_ADPCM_ENCODER
        "adpcm",
#endif
#if LND_MODULE_FLAC_ENCODER
        "flac",
#endif
#if LND_MODULE_MP3_ENCODER
        "mp3",
#endif
#if LND_MODULE_OPUS_ENCODER
        "opus",
#endif
#if LND_MODULE_VORBIS_ENCODER
        "vorbis",
#endif
#if LND_MODULE_AAC_ENCODER
        "aac",
#endif
        nullptr,
    };
    static test_output buffer;
    int16_t samples[64] = {0};
    for (unsigned i = 0; names[i]; i++) {
        LND_ENCODER_PARAMS params = {.encoder_name = names[i], .channels = 1, .sample_rate_hz = 48000};
        unsigned baseline = live_allocations;
        bool opened = false;
        for (int failure = 0; failure < 16; failure++) {
            buffer = (test_output){0};
            fail_allocation = failure;
            LND_OUTPUT *output = LND_OutputCreateProc(&output_procs, &buffer, &params);
            if (output) {
                opened = true;
                LND_OutputWrite(output, samples, LND_FORMAT_S16, 64);
                LND_OutputFree(output);
            }
            CHECK(live_allocations == baseline);
        }
        fail_allocation = -1;
        CHECK(opened);
    }
}

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    test_allocations();
#if LND_MODULE_ADPCM_ENCODER
    test_frame("adpcm", 249, 250, "variant=ima");
    test_frame("adpcm", 244, 245, "variant=ms");
#endif
#if LND_MODULE_OPUS_ENCODER
    test_frame("opus", 120, 121, nullptr);
#endif
#if LND_MODULE_AAC_ENCODER
    test_frame("aac", 1024, 123, "aot=lc;container=adts");
#endif
#if LND_MODULE_WAV_ENCODER
    test_output output = {0};
    LND_ENCODER_PARAMS params = {.encoder_name = "wav", .channels = 1, .sample_rate_hz = 48000, .frame_size_frames = 123};
    CHECK(!LND_OutputCreateProc(&output_procs, &output, &params));
    CHECK(LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
#endif
    LND_LibraryFree();
    CHECK(!live_allocations);
    printf("encoder options: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
