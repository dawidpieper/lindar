#include "lindar_decode.h"
#include "src/platform.h"
#include "lindar_files.h"
#include <stdlib.h>
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#endif

static void check_pcm(const int16_t *pcm, uint64_t frames, uint32_t channels) {
    if (frames > 32) abort();
#if __has_feature(memory_sanitizer)
    __msan_check_mem_is_initialized(pcm, (size_t)frames * channels * sizeof *pcm);
#endif
}

static void read_source(LND_SOURCE *source, int16_t *pcm, uint32_t channels) {
#if __has_feature(memory_sanitizer)
    __msan_poison(pcm, 32 * channels * sizeof *pcm);
#endif
    int64_t got = LND_SourceRead(source, pcm, LND_FORMAT_S16, 32);
    if (got > 0) check_pcm(pcm, (uint64_t)got, channels);
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    return LND_LibraryInit() == LND_OK ? 0 : 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t bytes) {
    if (bytes > 65536) return 0;
    const char *codec = getenv("LND_FUZZ_CODEC");
    LND_ENCODED_SOURCE_OPTIONS file = {.codec_name = codec, .block_frames = 17};
    LND_SOURCE *source = LND_SourceCreateEncodedMemory(data, bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &file);
    int16_t *output = nullptr;
    if (source) {
        uint32_t channels = LND_SourceGetChannels(source);
        if (!channels || channels > LND_MAX_CHANNELS) abort();
        output = malloc(32 * channels * sizeof *output);
        if (!output) abort();
        for (unsigned i = 0; i < 64; i++) read_source(source, output, channels);
        LND_SourceSeekFrames(source, UINT64_MAX);
        read_source(source, output, channels);
        LND_SourceSeekFrames(source, 0);
        read_source(source, output, channels);
        LND_SourceFree(source);
        free(output);
    }
    bool fragmented = bytes && (data[0] & 1);
    LND_DECODER_OPTIONS options = {.codec_name = codec, .block_frames = 17, .input_bytes = fragmented ? 4096 : 65536, .allow_buffered = true};
    LND_DECODER *decoder = LND_DecoderCreate(&options);
    if (!decoder) return 0;
    size_t offset = 0;
    bool ended = false;
    for (unsigned i = 0; i < 512; i++) {
        if (offset < bytes) {
            size_t take = bytes - offset;
            if (fragmented && take > 257) take = 257;
            int64_t fed = LND_DecoderFeed(decoder, data + offset, take);
            if (fed < 0) break;
            offset += (size_t)fed;
        }
        if (offset == bytes && !ended) {
            LND_DecoderEnd(decoder);
            ended = true;
        }
        if (LND_DecoderStep(decoder) < 0) break;
        LND_CODEC_INFO info;
        if (LND_DecoderGetInfo(decoder, &info) != LND_OK) continue;
        if (!info.channels || info.channels > LND_MAX_CHANNELS) abort();
        output = malloc(32 * info.channels * sizeof *output);
        if (!output) abort();
        LND_PCM pcm = {.data = output, .format = LND_FORMAT_S16, .frames = 32, .channels = info.channels};
        int64_t got = LND_DecoderReadPcm(decoder, &pcm, 0, 32);
        if (got > 0) check_pcm(output, (uint64_t)got, info.channels);
        free(output);
        if (got < 0 || LND_DecoderGetStatus(decoder) == LND_SOURCE_EOF) break;
    }
    LND_DecoderFree(decoder);
    return 0;
}
