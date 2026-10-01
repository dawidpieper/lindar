#include "codec_test.h"
#include "lindar_file_io.h"
#if LND_MODULE_DECODE
#include "lindar_decode.h"
#endif

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    CHECK(!LND_IoCreateTemporary("lindar-missing-directory"));
    LND_IO *io = LND_IoCreateTemporary(nullptr);
    CHECK(io && LND_IoCanSeek(io) && LND_IoGetSizeBytes(io) == 0);
    if (io) {
        CHECK(LND_IoWrite(io, "abcdef", 6) == 6);
        CHECK(LND_IoGetSizeBytes(io) == 6 && LND_IoGetPositionBytes(io) == 6);
        char data[8] = {0};
        CHECK(LND_IoSeekBytes(io, 0) == LND_OK);
        CHECK(LND_IoRead(io, data, 3) == 3 && !memcmp(data, "abc", 3));
        CHECK(LND_IoWrite(io, "12", 2) == 2);
        CHECK(LND_IoRead(io, data, 1) == 1 && data[0] == 'f');
        CHECK(LND_IoSeekBytes(io, 0) == LND_OK);
        CHECK(LND_IoRead(io, data, 6) == 6 && !memcmp(data, "abc12f", 6));
        LND_IoFree(io);
    }
#if LND_MODULE_DECODE && LND_MODULE_WAV_DECODER
    io = LND_IoOpenFile("audiosamples/tone.wav");
    LND_DECODER *decoder = LND_DecoderCreate(nullptr);
    CHECK(io && decoder);
    if (io && decoder) {
        CHECK(LND_DecoderTakeIo(decoder, io) == LND_OK);
        CHECK(!strcmp(LND_DecoderGetCodec(decoder)->name, "wav"));
        int16_t samples[1024];
        LND_CODEC_INFO info;
        CHECK(LND_DecoderGetInfo(decoder, &info) == LND_OK);
        LND_PCM pcm = {.data = samples, .frames = 512, .channels = info.channels, .format = LND_FORMAT_S16};
        uint64_t total = 0;
        for (;;) {
            int64_t got = LND_DecoderReadPcm(decoder, &pcm, 0, 512);
            CHECK(got >= 0);
            if (got <= 0) break;
            total += (uint64_t)got;
        }
        CHECK(total == 88200 && LND_DecoderGetStatus(decoder) == LND_SOURCE_EOF);
        CHECK(LND_DecoderFeed(decoder, samples, 1) == LND_ERR_STATE);
    } else if (io)
        LND_IoFree(io);
    LND_DecoderFree(decoder);
#endif
    LND_LibraryFree();
    CHECK(!live_allocations);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
