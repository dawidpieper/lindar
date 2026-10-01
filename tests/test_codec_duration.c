#include "codec_test.h"
#if LND_MODULE_FILES && LND_MODULE_OUTPUT
#include "lindar_files.h"
#endif
#if LND_MODULE_GRAPH
#include "lindar_graph.h"
#endif
#include "lindar_pcm_float.h"

static uint64_t declared_length;
static bool estimated = true;

static int32_t open_decoder(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    *info = (LND_CODEC_INFO){.format = LND_FORMAT_S16, .channels = 1, .sample_rate_hz = 8000, .length_frames = declared_length, .seekable = true, .length_estimated = estimated};
    *state = io;
    return LND_OK;
}

static uint64_t read_decoder(void *state, void *dst, uint64_t frames) { return LND_IoRead(state, dst, (size_t)frames * 2) / 2; }
static int32_t seek_decoder(void *state, uint64_t frame) { return frame <= LND_IoGetSizeBytes(state) / 2 ? LND_IoSeekBytes(state, frame * 2) : LND_ERR_INVALID_ARG; }
static void close_decoder(void *state) {}
static const LND_CODEC codec = {.name = "duration-test", .open = open_decoder, .read = read_decoder, .seek = seek_decoder, .close = close_decoder};

static void check_source(const int16_t *data, size_t frames, uint32_t flags) {
    LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = codec.name, .block_frames = 83};
    LND_SOURCE *source = LND_SourceCreateEncodedMemory(data, frames * 2, flags, &options);
    CHECK(source != nullptr);
    if (!source) return;
    CHECK(LND_SourceGetLengthFrames(source) == (flags & LND_ENCODED_SOURCE_PRELOAD ? frames : declared_length));
    LND_SOURCE_INFO info;
    CHECK(LND_SourceGetInfo(source, &info) == LND_OK);
    CHECK(info.length_kind == ((flags & LND_ENCODED_SOURCE_PRELOAD) || !estimated ? LND_LENGTH_EXACT : LND_LENGTH_ESTIMATED));
    int16_t pcm[257];
    size_t total = 0;
    uint64_t got;
    while ((got = LND_SourceRead(source, pcm, LND_FORMAT_S16, 257)) && total <= frames) {
        CHECK(got <= 257 && got <= frames - total);
        if (got > 257 || got > frames - total) break;
        CHECK(!memcmp(pcm, data + total, (size_t)got * 2));
        total += (size_t)got;
    }
    CHECK(total == frames && got == 0);
    CHECK(LND_SourceGetStatus(source) == LND_SOURCE_EOF);
    CHECK(LND_SourceSeekFrames(source, frames - 3) == LND_OK);
    CHECK(LND_SourceRead(source, pcm, LND_FORMAT_S16, 257) == 3);
    CHECK(!memcmp(pcm, data + frames - 3, 6));
    CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
    LND_SOUND *sound = LND_SourceEnsureSound(source, &(LND_SOUND_CONFIG){.channels = 1, .sample_rate_hz = 8000});
    CHECK(sound != nullptr);
    if (sound) {
        CHECK(LND_SoundGetLengthFrames(sound) == (flags & LND_ENCODED_SOURCE_PRELOAD ? frames : declared_length));
#if LND_MODULE_PCM_FLOAT
        float output[257];
        total = 0;
        while ((got = LND_SoundReadF32(sound, output, 257)) && total <= frames) {
            CHECK(got <= 257 && got <= frames - total);
            if (got > 257 || got > frames - total) break;
            for (size_t i = 0; i < got; i++)
                CHECK(output[i] == (float)data[total + i] / 32768.0f);
            total += (size_t)got;
        }
        CHECK(total == frames && got == 0);
#endif
        if ((flags & LND_ENCODED_SOURCE_LIGHTWEIGHT) && declared_length == 1) {
            LND_RENDERER_CONFIG config = {.render = LND_SoundRenderPcm, .user = sound, .channels = 1, .sample_rate_hz = 8000, .block_frames = 83};
            LND_RENDERER *renderer = LND_RendererCreateProc(&config);
            CHECK(renderer != nullptr);
            if (renderer) {
                LND_PCM output = {.data = pcm, .frames = 257, .channels = 1, .format = LND_FORMAT_S16};
                CHECK(LND_SoundSeekFrames(sound, frames - 3) == LND_OK);
                CHECK(LND_SoundPlay(sound) == LND_OK);
                CHECK(LND_RendererFillPcm(renderer, &output, 0, 257) == LND_OK);
                CHECK(!memcmp(pcm, data + frames - 3, 6));
                for (size_t i = 3; i < 257; i++)
                    CHECK(pcm[i] == 0);
                CHECK(LND_SoundSeekFrames(sound, frames - 3) == LND_OK);
                CHECK(LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundPlay(sound) == LND_OK);
                CHECK(LND_RendererFillPcm(renderer, &output, 0, 257) == LND_OK);
                CHECK(!memcmp(pcm, data + frames - 3, 6) && !memcmp(pcm + 3, data, 254 * 2));
                CHECK(LND_RendererFree(renderer) == LND_OK);
            }
        }
#if LND_MODULE_GRAPH
        if (!(flags & LND_ENCODED_SOURCE_LIGHTWEIGHT) && declared_length == 1) {
            CHECK(LND_SoundSeekFrames(sound, 0) == LND_OK);
            CHECK(LND_SoundSetConfig(sound, &(LND_SOUND_CONFIG){.channels = 1, .sample_rate_hz = 16000, .flags = LND_SOUND_RESAMPLE_LINEAR}) == LND_OK);
            CHECK(sound != nullptr);
            if (sound) {
                float output[257];
                size_t count = 0;
                while ((got = LND_SoundReadF32(sound, output, 257)) && count <= frames * 2 + 32)
                    count += (size_t)got;
                CHECK(count >= frames * 2 - 2 && count <= frames * 2 + 32 && got == 0);
            }
        }
#endif
        if (flags & LND_ENCODED_SOURCE_LIGHTWEIGHT) CHECK(LND_SoundFree(sound) == LND_OK);
    }
#if LND_MODULE_OUTPUT && LND_MODULE_WAV_ENCODER
    CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
    test_output encoded = {0};
    LND_ENCODER_PARAMS params = {.encoder_name = "wav", .channels = 1, .sample_rate_hz = 8000, .format = LND_FORMAT_S16};
    LND_OUTPUT *out = LND_OutputCreateProc(&output_procs, &encoded, &params);
    CHECK(out != nullptr);
    if (out) {
        CHECK(LND_OutputWriteSource(out, source, 0) == (int64_t)frames);
        CHECK(LND_OutputGetFrames(out) == frames);
        CHECK(LND_OutputFree(out) == LND_OK);
    }
#if LND_MODULE_FILES
    if ((flags & LND_ENCODED_SOURCE_LIGHTWEIGHT) && declared_length == 1) {
        CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
        CHECK(LND_SourceRenderFile(source, "lindar-audit-estimated.wav", LND_FORMAT_S16, 0) == (int64_t)frames);
        CHECK(remove("lindar-audit-estimated.wav") == 0);
    }
#endif
#endif
    CHECK(LND_SourceFree(source) == LND_OK);
}

int main(void) {
    const size_t frames = 70001;
    int16_t *data = malloc(frames * 2);
    if (!data) return 1;
    for (size_t i = 0; i < frames; i++)
        data[i] = (int16_t)(1 + i % 30000);
    const uint64_t lengths[] = {1, frames - 2, frames, frames + 123, UINT64_MAX, 0};
    const uint32_t modes[] = {
        LND_ENCODED_SOURCE_LIGHTWEIGHT,
#if LND_MODULE_GRAPH
        LND_ENCODED_SOURCE_DIRECT,
        0,
        LND_ENCODED_SOURCE_PRELOAD,
#endif
    };
    for (int32_t layout = LND_LAYOUT_INTERLEAVED; layout <= LND_LAYOUT_PLANAR; layout++) {
        test_init(layout);
        CHECK(LND_CodecRegister(&codec) == LND_OK);
        for (size_t i = 0; i < sizeof lengths / sizeof *lengths; i++) {
            declared_length = lengths[i];
            for (size_t j = 0; j < sizeof modes / sizeof *modes; j++) {
                printf("duration %llu, flags %u, layout %d\n", (unsigned long long)declared_length, modes[j], layout);
                check_source(data, frames, modes[j]);
            }
        }
        estimated = false;
        declared_length = 3;
        check_source(data, 3, LND_ENCODED_SOURCE_LIGHTWEIGHT);
        estimated = true;
        CHECK(LND_CodecUnregister(&codec) == LND_OK);
        LND_LibraryFree();
        CHECK(live_allocations == 0);
    }
    free(data);
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
