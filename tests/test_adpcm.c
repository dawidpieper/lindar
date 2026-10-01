#include "codec_test.h"

static void test_decode(const char *variant, uint32_t channels) {
    char path[128];
    snprintf(path, sizeof path, "audiosamples/adpcm/%s%u.wav", variant, channels);
    size_t bytes = 0, reference_bytes = 0;
    uint8_t *file = read_file(path, &bytes);
    snprintf(path, sizeof path, "audiosamples/adpcm/%s%u-reference.s16", variant, channels);
    uint8_t *reference = read_file(path, &reference_bytes);
    if (!file || !reference) {
        free(file);
        free(reference);
        return;
    }
    test_input input = {.data = file, .size = bytes, .limit = 7};
    LND_ENCODED_SOURCE_OPTIONS options = {.block_frames = 31};
    LND_SOURCE *source = LND_SourceCreateEncodedInput(&input_procs, &input, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
    CHECK(source != nullptr);
    if (!source) {
        free(file);
        free(reference);
        return;
    }
    CHECK(!strcmp(LND_SourceGetCodec(source)->name, "adpcm"));
    CHECK(LND_SourceGetLengthFrames(source) == 1003 && LND_SourceGetSampleRateHz(source) == 16000 && LND_SourceGetChannels(source) == channels);
    CHECK(reference_bytes >= 1003 * channels * 2);
    int16_t pcm[130];
    uint64_t position = 0;
    unsigned before = allocations;
    while (position < 1003) {
        uint64_t want = 1 + position % 61;
        uint64_t got = LND_SourceRead(source, pcm, LND_FORMAT_S16, want);
        CHECK(got == (want < 1003 - position ? want : 1003 - position));
        if (!got) break;
        CHECK(!memcmp(pcm, reference + position * channels * 2, (size_t)got * channels * 2));
        position += got;
    }
    CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 1) == 0 && allocations == before);
    uint64_t seeks[] = {0, 1, 2, 240, 248, 249, 497, 1002, 1003, UINT64_MAX};
    for (size_t i = 0; i < sizeof seeks / sizeof seeks[0]; i++) {
        uint64_t at = seeks[i] < 1003 ? seeks[i] : 1003;
        CHECK(LND_SourceSeekFrames(source, seeks[i]) == LND_OK);
        uint64_t got = LND_SourceRead(source, pcm, LND_FORMAT_S16, 17);
        CHECK(got == (17 < 1003 - at ? 17 : 1003 - at));
        CHECK(!memcmp(pcm, reference + at * channels * 2, (size_t)got * channels * 2));
    }
    CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    LND_RENDERER_CONFIG config = {.render = LND_SoundRenderPcm, .user = sound, .channels = channels, .sample_rate_hz = 16000, .block_frames = 13};
    LND_RENDERER *renderer = LND_RendererCreateProc(&config);
    CHECK(renderer && sound && LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundSeekFrames(sound, 995) == LND_OK);
    LND_PCM output = {.data = pcm, .frames = 65, .channels = channels, .format = LND_FORMAT_S16};
    before = allocations;
    CHECK(LND_RendererFillPcm(renderer, &output, 0, 65) == LND_OK);
    for (size_t f = 0; f < 65; f++) CHECK(!memcmp(pcm + f * channels, reference + ((995 + f) % 1003) * channels * 2, channels * 2));
    CHECK(allocations == before);
    CHECK(LND_SoundSetPause(sound, true) == LND_OK && LND_RendererFillPcm(renderer, &output, 0, 65) == LND_OK);
    for (size_t i = 0; i < 65 * channels; i++) CHECK(pcm[i] == 0);
    CHECK(LND_RendererFree(renderer) == LND_OK && LND_SourceFree(source) == LND_OK && input.closes == 1);
    unsigned baseline = live_allocations;
    options.codec_name = "adpcm";
    for (int failure = 0; failure < 12; failure++) {
        fail_allocation = failure;
        source = LND_SourceCreateEncodedMemory(file, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
        if (source) CHECK(LND_SourceFree(source) == LND_OK);
        CHECK(live_allocations == baseline);
    }
    fail_allocation = -1;
    for (size_t size = 1; size < bytes; size += 13) {
        source = LND_SourceCreateEncodedMemory(file, size, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
        CHECK(!source);
        if (source) LND_SourceFree(source);
    }
    uint8_t saved = file[32], saved_high = file[33];
    file[33] = 0;
    file[32] = 0;
    CHECK(!LND_SourceCreateEncodedMemory(file, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options));
    file[32] = saved;
    file[33] = saved_high;
    size_t data_offset = !strcmp(variant, "ima") ? 60 : 90;
    saved = file[data_offset + (!strcmp(variant, "ima") ? 2 : 0)];
    file[data_offset + (!strcmp(variant, "ima") ? 2 : 0)] = 255;
    source = LND_SourceCreateEncodedMemory(file, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
    CHECK(source != nullptr);
    if (source) {
        CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 1) == 0);
        CHECK(LND_ErrorGetLast() == LND_ERR_FORMAT);
        LND_SourceFree(source);
    }
    file[data_offset + (!strcmp(variant, "ima") ? 2 : 0)] = saved;
    CHECK(live_allocations == baseline);
    free(file);
    free(reference);
}

#if LND_MODULE_ADPCM_ENCODER
static void test_encode(const char *variant, uint32_t channels) {
    char path[128];
    snprintf(path, sizeof path, "audiosamples/adpcm/%s%u-input.s16", variant, channels);
    size_t bytes;
    uint8_t *input = read_file(path, &bytes);
    if (!input) return;
    char options[32];
    snprintf(options, sizeof options, "variant=%s", variant);
    uint32_t blocks[] = {0, !strcmp(variant, "ima") ? 9 : 10, !strcmp(variant, "ima") ? 249 : 244};
    uint32_t lengths[] = {0, 1, 2, 9, 243, 244, 249, 505, 1003};
    static test_output output;
    for (size_t b = 0; b < sizeof blocks / sizeof blocks[0]; b++) {
        for (size_t n = 0; n < sizeof lengths / sizeof lengths[0]; n++) {
            output = (test_output){0};
            LND_ENCODER_PARAMS params = {.encoder_name = "adpcm", .channels = channels, .sample_rate_hz = 16000, .options = options, .frame_size_frames = blocks[b]};
            LND_OUTPUT *o = LND_OutputCreateProc(&output_procs, &output, &params);
            CHECK(o != nullptr);
            if (!o) continue;
            unsigned before = allocations;
            uint32_t frames = lengths[n];
            for (uint32_t f = 0; f < frames;) {
                uint32_t chunk = frames - f < 19 ? frames - f : 19;
                CHECK(LND_OutputWrite(o, input + f * channels * 2, LND_FORMAT_S16, chunk) == LND_OK);
                f += chunk;
            }
            CHECK(LND_OutputFree(o) == LND_OK && allocations == before);
            LND_SOURCE *source = LND_SourceCreateEncodedMemory(output.data, output.size, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
            CHECK(source && LND_SourceGetLengthFrames(source) == frames);
            int16_t decoded[2010];
            if (source) {
                CHECK(LND_SourceRead(source, decoded, LND_FORMAT_S16, frames + 1) == frames);
                if (frames) CHECK(!memcmp(decoded, input, channels * 2));
                CHECK(LND_SourceFree(source) == LND_OK);
            }
            if (frames == 1003 && blocks[b] == 0) {
                snprintf(path, sizeof path, LND_TEST_OUTPUT_DIR "/lindar-%s%u.wav", variant, channels);
                FILE *f = fopen(path, "wb");
                CHECK(f != nullptr);
                if (f) {
                    CHECK(fwrite(output.data, 1, output.size, f) == output.size);
                    fclose(f);
                }
                snprintf(path, sizeof path, LND_TEST_OUTPUT_DIR "/lindar-%s%u.s16", variant, channels);
                f = fopen(path, "wb");
                CHECK(f != nullptr);
                if (f) {
                    CHECK(fwrite(decoded, channels * 2, frames, f) == frames);
                    fclose(f);
                }
            }
        }
    }
    LND_ENCODER_PARAMS invalid = {.encoder_name = "adpcm", .channels = channels, .sample_rate_hz = 16000, .options = options, .frame_size_frames = 3};
    if (!strcmp(variant, "ima") || channels == 1) CHECK(!LND_OutputCreateProc(&output_procs, &output, &invalid));
    invalid.frame_size_frames = UINT32_MAX;
    CHECK(!LND_OutputCreateProc(&output_procs, &output, &invalid));
    free(input);
}
#endif

int main(void) {
    for (int layout = LND_LAYOUT_INTERLEAVED; layout <= LND_LAYOUT_PLANAR; layout++) {
        test_init(layout);
        const char *variants[] = {"ima", "ms"};
        for (unsigned v = 0; v < 2; v++) {
            for (uint32_t ch = 1; ch <= 2; ch++) {
                test_decode(variants[v], ch);
#if LND_MODULE_ADPCM_ENCODER
                test_encode(variants[v], ch);
#endif
            }
        }
        LND_LibraryFree();
        CHECK(!live_allocations);
    }
    printf("ADPCM files: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
