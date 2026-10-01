#include "../codec_test.h"
#include "formats/codecs/audiotoolbox/decoder.c"

static unsigned calls, fail_at, files, wrappers, invalid_format, read_error, compressed;
static bool fail(void) { return ++calls == fail_at; }
struct MockAudioFile {
    void *user;
    AudioFile_ReadProc read;
    AudioFile_GetSizeProc size;
};
struct MockExtAudioFile {
    AudioFileID file;
    uint64_t position;
    float history;
};
OSStatus AudioFileOpenWithCallbacks(void *user, AudioFile_ReadProc read, AudioFile_WriteProc write, AudioFile_GetSizeProc size, AudioFile_SetSizeProc resize,
                                    UInt32 hint, AudioFileID *file) {
    CHECK(write == nullptr && resize == nullptr && hint == 0);
    if (fail()) return -1;
    CHECK(size(user) == 8);
    char data[8];
    UInt32 actual = 99;
    CHECK(read(user, 0, 8, data, &actual) == noErr && actual == 8 && !memcmp(data, "testdata", 8));
    CHECK(read(user, 8, 8, data, &actual) == noErr && actual == 0);
    CHECK(read(user, -1, 8, data, &actual) != noErr && actual == 0);
    CHECK(read(user, 9, 8, data, &actual) != noErr && actual == 0);
    *file = malloc(sizeof **file);
    **file = (struct MockAudioFile){user, read, size};
    files++;
    return noErr;
}
OSStatus AudioFileClose(AudioFileID file) {
    CHECK(wrappers == 0 && files == 1);
    files--;
    free(file);
    return noErr;
}
OSStatus ExtAudioFileWrapAudioFileID(AudioFileID file, unsigned char writing, ExtAudioFileRef *decoder) {
    CHECK(!writing);
    if (fail()) return -1;
    *decoder = calloc(1, sizeof **decoder);
    (*decoder)->file = file;
    wrappers++;
    return noErr;
}
OSStatus ExtAudioFileDispose(ExtAudioFileRef decoder) {
    CHECK(files == 1 && wrappers > 0);
    wrappers--;
    free(decoder);
    return noErr;
}
OSStatus ExtAudioFileGetProperty(ExtAudioFileRef decoder, UInt32 property, UInt32 *size, void *data) {
    if (fail()) return -1;
    if (property == kExtAudioFileProperty_FileDataFormat) {
        CHECK(*size == sizeof(AudioStreamBasicDescription));
        *(AudioStreamBasicDescription *)data =
            (AudioStreamBasicDescription){.mSampleRate = invalid_format == 1 ? 0 : 8000, .mFormatID = compressed ? 0 : kAudioFormatLinearPCM, .mChannelsPerFrame = invalid_format == 2 ? 33 : 2};
    } else {
        CHECK(property == kExtAudioFileProperty_FileLengthFrames && *size == sizeof(SInt64));
        *(SInt64 *)data = 10;
    }
    return noErr;
}
OSStatus ExtAudioFileSetProperty(ExtAudioFileRef decoder, UInt32 property, UInt32 size, const void *data) {
    if (fail()) return -1;
    const AudioStreamBasicDescription *format = data;
    CHECK(property == kExtAudioFileProperty_ClientDataFormat && size == sizeof *format);
    CHECK(format->mSampleRate == 8000 && format->mChannelsPerFrame == 2 && format->mBytesPerFrame == 8);
    CHECK(format->mBitsPerChannel == 32 && format->mFramesPerPacket == 1 && format->mFormatID == kAudioFormatLinearPCM);
    return noErr;
}
OSStatus ExtAudioFileRead(ExtAudioFileRef decoder, UInt32 *frames, AudioBufferList *buffers) {
    if (read_error) return -1;
    CHECK(buffers->mNumberBuffers == 1 && buffers->mBuffers[0].mDataByteSize == *frames * 8);
    UInt32 count = (UInt32)LND_MIN(LND_MIN(*frames, 3), 10 - decoder->position);
    float *dst = buffers->mBuffers[0].mData;
    for (UInt32 i = 0; i < count * 2; i++)
        dst[i] = (float)(decoder->position * 2 + i) + decoder->history;
    decoder->position += count;
    *frames = count;
    return noErr;
}
OSStatus ExtAudioFileSeek(ExtAudioFileRef decoder, SInt64 frame) {
    if (frame < 0 || frame > 10) return -1;
    decoder->position = (uint64_t)frame;
    if (compressed) decoder->history += 0.125f;
    return noErr;
}
int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    LND_IO *io = lnd_io_open_memory("xxxtestdatazzz", 14);
    CHECK(lnd_io_window(io, 3, 8) == LND_OK);
    const LND_CODEC *codec = LND_AudioToolboxGetCodec();
    for (unsigned failure = 0; failure <= 4; failure++) {
        fail_at = failure;
        calls = 0;
        LND_CODEC_INFO info = {0};
        void *state = nullptr;
        int32_t r = codec->open(io, &info, &state);
        CHECK((r == LND_OK) == (failure == 0));
        if (state) {
            CHECK(info.sample_rate_hz == 8000 && info.channels == 2 && info.format == LND_FORMAT_F32 && info.length_frames == 10 && info.seekable);
            float pcm[32];
            CHECK(codec->read(state, pcm, 4) == 4 && pcm[7] == 7);
            CHECK(codec->seek(state, 3) == LND_OK);
            CHECK(codec->read(state, pcm, 12) == 7 && pcm[0] == 6 && pcm[13] == 19);
            CHECK(codec->read(state, pcm, 1) == 0);
            CHECK(codec->seek(state, UINT64_MAX) == LND_ERR_INVALID_ARG);
            CHECK(codec->seek(state, 11) == LND_ERR_INVALID_ARG);
            CHECK(codec->seek(state, 0) == LND_OK);
            read_error = 1;
            CHECK(codec->read(state, pcm, 1) == LND_CODEC_READ_ERROR);
            read_error = 0;
            CHECK(codec->read(state, pcm, 1) == LND_CODEC_READ_ERROR);
            CHECK(codec->seek(state, 0) == LND_OK);
            CHECK(codec->read(state, pcm, 1) == 1);
            codec->close(state);
        }
        CHECK(files == 0 && wrappers == 0);
    }
    fail_at = 0;
    for (compressed = 0; compressed <= 1; compressed++) {
        LND_CODEC_INFO info;
        void *state = nullptr;
        CHECK(codec->open(io, &info, &state) == LND_OK);
        if (!state) continue;
        float reference[8], pcm[8];
        CHECK(codec->read(state, reference, 4) == 4);
        for (unsigned rewind = 0; rewind < 3; rewind++) {
            CHECK(codec->seek(state, 0) == LND_OK);
            CHECK(codec->read(state, pcm, 4) == 4 && !memcmp(reference, pcm, sizeof pcm));
        }
        if (compressed) {
            for (unsigned failure = 1; failure <= 2; failure++) {
                fail_at = calls + failure;
                CHECK(codec->seek(state, 0) == LND_ERR_IO);
                CHECK(files == 1 && wrappers == 1);
                fail_at = 0;
                CHECK(codec->seek(state, 0) == LND_OK);
                CHECK(codec->read(state, pcm, 4) == 4 && !memcmp(reference, pcm, sizeof pcm));
            }
        }
        codec->close(state);
        CHECK(files == 0 && wrappers == 0);
    }
    compressed = 0;
    for (invalid_format = 1; invalid_format <= 2; invalid_format++) {
        LND_CODEC_INFO info;
        void *state = nullptr;
        CHECK(codec->open(io, &info, &state) == LND_ERR_FORMAT && state == nullptr);
        CHECK(files == 0 && wrappers == 0);
    }
    invalid_format = 0;
    fail_allocation = 0;
    LND_CODEC_INFO info;
    void *state = nullptr;
    CHECK(codec->open(io, &info, &state) == LND_ERR_OUT_OF_MEMORY);
    fail_allocation = -1;
    lnd_io_close(io);
    LND_LibraryFree();
    CHECK(live_allocations == 0);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
