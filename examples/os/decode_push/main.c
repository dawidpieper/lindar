#include "lindar_decode.h"
#include <stdio.h>

static const uint8_t wav[] = {
    'R','I','F','F',44,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
    1,0,1,0,0x40,0x1f,0,0,0x80,0x3e,0,0,2,0,16,0,
    'd','a','t','a',8,0,0,0,0,0,0,32,0,64,0,96
};

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_DECODER_OPTIONS options = {.codec_name = "wav", .input_bytes = 4096};
    LND_DECODER *decoder = LND_DecoderCreate(&options);
    size_t offset = 0;
    int64_t total = 0;
    bool ended = false;
    int result = 1;
    int16_t samples[16];
    LND_PCM pcm = {.data = samples, .frames = 16, .channels = 1, .format = LND_FORMAT_S16LE};
    for (unsigned step = 0; decoder && step < 100; step++) {
        if (offset < sizeof wav) {
            size_t bytes = sizeof wav - offset < 7 ? sizeof wav - offset : 7;
            int64_t accepted = LND_DecoderFeed(decoder, wav + offset, bytes);
            if (accepted < 0) break;
            // Keep unaccepted bytes and drain PCM before retrying.
            offset += (size_t)accepted;
        } else if (!ended) {
            if (LND_DecoderEnd(decoder) != LND_OK) break;
            ended = true;
        }
        int64_t got = LND_DecoderReadPcm(decoder, &pcm, 0, 16);
        if (got < 0) break;
        total += got;
        if (LND_DecoderGetStatus(decoder) == LND_SOURCE_EOF) { result = total == 4 ? 0 : 1; break; }
    }
    printf("%lld decoded frames from %zu bytes\n", (long long)total, offset);
    LND_DecoderFree(decoder);
    LND_LibraryFree();
    return result;
}
