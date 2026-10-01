#include "codec_test.h"
#include <math.h>
int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    size_t bytes;
    uint8_t *data = read_file("audiosamples/tone.spx", &bytes);
    LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "speex"};
    LND_SOURCE *source = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
    CHECK(source != nullptr);
    int16_t audio[50000], seeked[1024];
    uint64_t frames = LND_SourceRead(source, audio, LND_FORMAT_S16, 50000);
    printf("speex frames=%llu length=%llu\n", (unsigned long long)frames, (unsigned long long)LND_SourceGetLengthFrames(source));
    CHECK(frames == 47777 && LND_SourceGetLengthFrames(source) == frames);
#if LND_MODULE_WAV_DECODER
    size_t reference_bytes;
    uint8_t *reference_data = read_file("audiosamples/speex-reference.wav", &reference_bytes);
    LND_SOURCE *reference = LND_SourceCreateEncodedMemory(reference_data, reference_bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, nullptr);
    CHECK(reference != nullptr);
    CHECK(frames == LND_SourceGetLengthFrames(reference) && frames == LND_SourceGetLengthFrames(source));
    int16_t expected[50000];
    CHECK(LND_SourceRead(reference, expected, LND_FORMAT_S16, 50000) == frames);
    int difference = 0;
    for (uint64_t i = 0; i < frames; ++i) {
        int delta = abs((int)audio[i] - expected[i]);
        if (delta > difference) difference = delta;
    }
    CHECK(difference <= 2);
    CHECK(LND_SourceFree(reference) == LND_OK);
    free(reference_data);
#endif

    CHECK(LND_SourceSeekFrames(source, 10000) == LND_OK);
    CHECK(LND_SourceRead(source, seeked, LND_FORMAT_S16, 1024) == 1024);
    CHECK(!memcmp(seeked, audio + 10000, sizeof seeked));
    CHECK(LND_SourceFree(source) == LND_OK);
    free(data);
    LND_LibraryFree();
    CHECK(live_allocations == 0);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
