#include "codec_test.h"

typedef struct decoder {
    LND_IO *io;
} decoder;
static unsigned decoder_closes;
static int mode;
static uint64_t declared_frames = 10;
static uint32_t declared_rate = 16000;
static int32_t probe(LND_IO *io) {
    uint8_t magic[4];
    return LND_IoRead(io, magic, 4) == 4 && !memcmp(magic, "test", 4) ? 200 : 0;
}
static int32_t open_decoder(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    decoder *s = malloc(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    *state = s;
    *info = (LND_CODEC_INFO){.format = mode == 2 ? 99 : LND_FORMAT_S16, .channels = 2, .sample_rate_hz = declared_rate, .length_frames = declared_frames, .seekable = true};
    return LND_IoSeekBytes(io, mode == 3 ? 0 : 4);
}
static uint64_t read_decoder(void *state, void *dst, uint64_t frames) {
    if (mode == 1) return frames + 1;
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) == LND_ERR_BUSY);
    decoder *s = state;
    return LND_IoRead(s->io, dst, (size_t)frames * 4) / 4;
}
static int32_t seek_decoder(void *state, uint64_t frame) { return LND_IoSeekBytes(((decoder *)state)->io, 4 + frame * 4); }
static void close_decoder(void *state) {
    decoder_closes++;
    free(state);
}
static const LND_CODEC codec = {.name = "test", .probe = probe, .open = open_decoder, .read = read_decoder, .seek = seek_decoder, .close = close_decoder};

static void test_bitrate(void) {
    mode = 3;
    static const struct { uint64_t bytes, frames; uint32_t rate; uint64_t bitrate; bool estimated; } cases[] = {
        {UINT64_C(44), UINT64_C(10), UINT32_C(16000), UINT64_C(563200), true},
        {UINT64_C(5), UINT64_C(16), UINT32_C(1), UINT64_C(3), true},
        {UINT64_C(1), UINT64_C(16), UINT32_C(1), UINT64_C(1), true},
        {UINT64_C(1), UINT64_C(17), UINT32_C(1), UINT64_C(0), true},
        {UINT64_C(18446744073709551614), UINT64_C(1), UINT32_C(48000), UINT64_C(0), false},
        {UINT64_C(18446744073709551614), UINT64_C(18446744073709551615), UINT32_C(4294967295), UINT64_C(34359738360), true},
        {UINT64_C(18446744073709551614), UINT64_C(8), UINT32_C(1), UINT64_C(18446744073709551614), true},
        {UINT64_C(2305843009213693952), UINT64_C(1), UINT32_C(1), UINT64_C(0), false},
        {UINT64_C(2305843009213693951), UINT64_C(1), UINT32_C(1), UINT64_C(18446744073709551608), true},
        {UINT64_C(2305843009213693951), UINT64_C(2), UINT32_C(1), UINT64_C(9223372036854775804), true},
        {UINT64_C(18446744073709551614), UINT64_C(97), UINT32_C(1), UINT64_C(1521380954532746525), true},
        {UINT64_C(44), UINT64_C(0), UINT32_C(16000), UINT64_C(0), false},
        {UINT64_C(1801662030962384124), UINT64_C(964920243575568432), UINT32_C(1089245971), UINT64_C(16270386046), true},
        {UINT64_C(15774360608283857007), UINT64_C(8294087079603280124), UINT32_C(2405548606), UINT64_C(36600523537), true},
        {UINT64_C(7014455027377311968), UINT64_C(11158786117207293540), UINT32_C(734035604), UINT64_C(3691340387), true},
        {UINT64_C(27014270282550781), UINT64_C(10044985855491041287), UINT32_C(3308704932), UINT64_C(71185565), true},
        {UINT64_C(14003420097358289922), UINT64_C(1102806300831252146), UINT32_C(2131345679), UINT64_C(216510398196), true},
        {UINT64_C(11600196104237369693), UINT64_C(7834211345565450799), UINT32_C(1067856043), UINT64_C(12649482087), true},
        {UINT64_C(13412011796579125032), UINT64_C(6463969010152996784), UINT32_C(2740088749), UINT64_C(45483018334), true},
        {UINT64_C(2828623243646558902), UINT64_C(12431757283135023711), UINT32_C(2381573922), UINT64_C(4335075210), true},
        {UINT64_C(7572908131191050721), UINT64_C(6044143371673324932), UINT32_C(1353660221), UINT64_C(13568367081), true},
        {UINT64_C(4999714359160895161), UINT64_C(8375686198247920970), UINT32_C(3878495143), UINT64_C(18521580106), true},
        {UINT64_C(16217941169312787140), UINT64_C(12190144597409569134), UINT32_C(1836508566), UINT64_C(19546536232), true},
        {UINT64_C(1448962352216550923), UINT64_C(16060361795309775366), UINT32_C(1507862629), UINT64_C(1088312311), true},
        {UINT64_C(18223347304294208463), UINT64_C(16168573221599518944), UINT32_C(3010228943), UINT64_C(27142257634), true},
        {UINT64_C(7303115505801541165), UINT64_C(13308136493410582552), UINT32_C(2454046937), UINT64_C(10773672630), true},
        {UINT64_C(7201210482203395347), UINT64_C(9264516625671253860), UINT32_C(2292369732), UINT64_C(14254677376), true},
        {UINT64_C(4729976488016380569), UINT64_C(15304196478211037709), UINT32_C(1610392005), UINT64_C(3981713816), true},
        {UINT64_C(452098531891367948), UINT64_C(15208844137465619227), UINT32_C(331518113), UINT64_C(78837735), true},
        {UINT64_C(47568100076035940), UINT64_C(9900757589340677532), UINT32_C(1694845360), UINT64_C(65142953), true},
        {UINT64_C(17252810913030915559), UINT64_C(12980722894866759954), UINT32_C(1466994504), UINT64_C(15598378607), true},
        {UINT64_C(14308418023088182482), UINT64_C(4252173916612825148), UINT32_C(1028833108), UINT64_C(27695902329), true},
        {UINT64_C(482059958800613756), UINT64_C(2015129854572973762), UINT32_C(571075815), UINT64_C(1092903401), true},
        {UINT64_C(8358396886318755379), UINT64_C(14336269767838414742), UINT32_C(2741852295), UINT64_C(12788537078), true},
        {UINT64_C(3775093843157536315), UINT64_C(11160779115304994133), UINT32_C(627437780), UINT64_C(1697828781), true},
        {UINT64_C(9416568552278627457), UINT64_C(6312135680409773510), UINT32_C(2996098918), UINT64_C(35757115853), true},
    };
    uint8_t file[] = {'t', 'e', 's', 't'};
    test_input input = {.data = file, .size = sizeof file};
    LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "test", .block_frames = 1};
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        declared_frames = cases[i].frames;
        declared_rate = cases[i].rate;
        LND_SOURCE *source = LND_SourceCreateEncodedInput(&input_procs, &input, cases[i].bytes, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
        CHECK(source != nullptr);
        LND_SOURCE_INFO info;
        if (source) {
            CHECK(LND_SourceGetInfo(source, &info) == LND_OK);
            CHECK(info.bitrate_bps == cases[i].bitrate && info.bitrate_estimated == cases[i].estimated);
            CHECK(LND_SourceFree(source) == LND_OK);
        }
    }
    declared_frames = 10;
    declared_rate = 16000;
    mode = 0;
}

#if LND_MODULE_AIFF_DECODER
static void test_aiff_rates(void) {
    static const struct { uint8_t rate[10]; uint32_t hz; } cases[] = {
        {{0x40, 0x0e, 0xac, 0x44}, 44100},
        {{0x3f, 0xfe, 0x80}, 1},
        {{0x40, 0x1e, 0xff, 0xff, 0xff, 0xff}, UINT32_MAX},
        {{0}, 0},
        {{0xc0, 0x0e, 0xac, 0x44}, 0},
        {{0x7f, 0xff, 0x80}, 0},
        {{0x40, 0x1e, 0xff, 0xff, 0xff, 0xff, 0x80}, 0},
    };
    uint8_t data[56] = {
        'F', 'O', 'R', 'M', 0, 0, 0, 48, 'A', 'I', 'F', 'F',
        'C', 'O', 'M', 'M', 0, 0, 0, 18, 0, 1, 0, 0, 0, 1, 0, 16,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        'S', 'S', 'N', 'D', 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0x12, 0x34,
    };
    LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "aiff", .block_frames = 1};
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        memcpy(data + 28, cases[i].rate, 10);
        LND_SOURCE *source = LND_SourceCreateEncodedMemory(data, sizeof data, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
        CHECK((source != nullptr) == (cases[i].hz != 0));
        if (source) {
            int16_t sample = 0;
            CHECK(LND_SourceGetSampleRateHz(source) == cases[i].hz);
            CHECK(LND_SourceRead(source, &sample, LND_FORMAT_S16, 1) == 1 && sample == 0x1234);
            CHECK(LND_SourceFree(source) == LND_OK);
        } else CHECK(LND_ErrorGetLast() == LND_ERR_FORMAT);
    }
}
#endif

#if LND_MODULE_VORBIS_DECODER && LND_MODULE_VORBIS_ENCODER
static void test_vorbis_links(void) {
    static const struct { uint32_t channels, rate; } cases[] = {{1, 48000}, {2, 44100}, {2, 48000}};
    test_output *buffers = calloc(2, sizeof *buffers);
    CHECK(buffers != nullptr);
    if (!buffers) return;
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        memset(buffers, 0, 2 * sizeof *buffers);
        LND_ENCODER_PARAMS params = {.encoder_name = "vorbis", .channels = 2, .sample_rate_hz = 48000};
        LND_OUTPUT *first = LND_OutputCreateProc(&output_procs, &buffers[0], &params);
        params.channels = cases[i].channels;
        params.sample_rate_hz = cases[i].rate;
        LND_OUTPUT *second = LND_OutputCreateProc(&output_procs, &buffers[1], &params);
        CHECK(first && second);
        float pcm[512] = {0};
        if (first) {
            CHECK(LND_OutputWrite(first, pcm, LND_FORMAT_F32, 256) == LND_OK);
            CHECK(LND_OutputFree(first) == LND_OK);
        }
        if (second) {
            CHECK(LND_OutputWrite(second, pcm, LND_FORMAT_F32, 256) == LND_OK);
            CHECK(LND_OutputFree(second) == LND_OK);
        }
        CHECK(write_output(&buffers[0], buffers[1].data, buffers[1].size) == buffers[1].size);
        LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "vorbis", .block_frames = 31};
        LND_SOURCE *source = LND_SourceCreateEncodedMemory(buffers[0].data, buffers[0].size, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
        bool compatible = cases[i].channels == 2 && cases[i].rate == 48000;
        CHECK((source != nullptr) == compatible);
        if (source) {
            if (compatible) {
                CHECK(LND_SourceGetLengthFrames(source) == 512);
                CHECK(LND_SourceRead(source, pcm, LND_FORMAT_F32, 256) == 256);
                CHECK(LND_SourceRead(source, pcm, LND_FORMAT_F32, 256) == 256);
                CHECK(LND_SourceRead(source, pcm, LND_FORMAT_F32, 1) == 0);
            }
            CHECK(LND_SourceFree(source) == LND_OK);
        } else CHECK(LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
    }
    free(buffers);
}
#endif

#if LND_MODULE_FLAC_DECODER && LND_MODULE_FLAC_ENCODER
static void test_flac_format(void) {
    test_output *buffer = calloc(1, sizeof *buffer);
    CHECK(buffer != nullptr);
    if (!buffer) return;
    LND_ENCODER_PARAMS params = {.encoder_name = "flac", .channels = 1, .sample_rate_hz = 48000, .format = LND_FORMAT_S24};
    LND_OUTPUT *output = LND_OutputCreateProc(&output_procs, buffer, &params);
    CHECK(output != nullptr);
    float samples[256] = {0};
    if (output) {
        CHECK(LND_OutputWrite(output, samples, LND_FORMAT_F32, 256) == LND_OK);
        CHECK(LND_OutputFree(output) == LND_OK);
        LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "flac", .block_frames = 31};
        for (unsigned pass = 0; pass < 2; pass++) {
            if (pass) {
                buffer->data[20] &= 0xfe;
                buffer->data[21] = (buffer->data[21] & 0x0f) | 0xf0;
            }
            LND_SOURCE *source = LND_SourceCreateEncodedMemory(buffer->data, buffer->size, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
            CHECK(source != nullptr);
            if (source) {
                CHECK(LND_SourceRead(source, samples, LND_FORMAT_F32, 256) == (pass ? 0 : 256));
                CHECK(LND_SourceFree(source) == LND_OK);
            }
        }
    }
    free(buffer);
}
#endif

static void test_files(void) {
    const char *files[][2] = {
#if LND_MODULE_WAV_DECODER
        {"wav", "audiosamples/tone.wav"},
#endif
#if LND_MODULE_AIFF_DECODER
        {"aiff", "audiosamples/tone.aiff"},
#endif
#if LND_MODULE_MP3_DECODER
        {"mp3", "audiosamples/tone.mp3"},
#endif
#if LND_MODULE_VORBIS_DECODER
        {"vorbis", "audiosamples/tone.ogg"},
#endif
#if LND_MODULE_OPUS_DECODER
        {"opus", "audiosamples/tone.opus"},
#endif
#if LND_MODULE_FLAC_DECODER
        {"flac", "audiosamples/tone.flac"},
#endif
#if LND_MODULE_AAC_DECODER
        {"aac", "audiosamples/tone.aac"},
        {"aac", "audiosamples/tone.m4a"},
#endif
        {nullptr, nullptr}};
    for (unsigned i = 0; files[i][0]; i++) {
        size_t size;
        uint8_t *data = read_file(files[i][1], &size);
        if (!data) continue;
        LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = files[i][0], .block_frames = 83};
        LND_SOURCE *source = LND_SourceCreateEncodedMemory(data, size, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
        CHECK(source != nullptr);
        if (source) {
            CHECK(!strcmp(LND_SourceGetCodec(source)->name, files[i][0]));
            uint64_t total = 0, energy = 0, got = 0;
            uint32_t channels = LND_SourceGetChannels(source);
            CHECK(channels > 0 && channels <= 2);
            int16_t samples[256];
            if (channels <= 2) {
                while (total < 480000 && (got = LND_SourceRead(source, samples, LND_FORMAT_S16, 128))) {
                    for (size_t f = 0; f < got * channels; f++) energy += (uint64_t)((int32_t)samples[f] * samples[f]);
                    total += got;
                }
            }
            CHECK(total > 0 && total < 480000 && energy > 0 && got == 0);
            CHECK(LND_SourceFree(source) == LND_OK);
        }
        unsigned baseline = live_allocations;
        for (int failure = 0; failure < 16; failure++) {
            fail_allocation = failure;
            source = LND_SourceCreateEncodedMemory(data, size, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
            if (source) {
                int16_t samples[32 * 32];
                LND_SourceRead(source, samples, LND_FORMAT_S16, 32);
                CHECK(LND_SourceFree(source) == LND_OK);
            }
            CHECK(live_allocations == baseline);
        }
        fail_allocation = -1;
        free(data);
    }
}

int main(void) {
    for (int layout = LND_LAYOUT_INTERLEAVED; layout <= LND_LAYOUT_PLANAR; layout++) {
        test_init(layout);
        test_files();
#if LND_MODULE_FLAC_DECODER && LND_MODULE_FLAC_ENCODER
        test_flac_format();
#endif
#if LND_MODULE_AIFF_DECODER
        test_aiff_rates();
#endif
#if LND_MODULE_VORBIS_DECODER && LND_MODULE_VORBIS_ENCODER
        test_vorbis_links();
#endif
        CHECK(LND_CodecRegister(&codec) == LND_OK);
        test_bitrate();
        uint8_t file[48] = {'t', 'e', 's', 't'};
        for (unsigned i = 0; i < 20; i++) {
            file[4 + 2 * i] = (uint8_t)(1000 + i);
            file[5 + 2 * i] = (uint8_t)((1000 + i) >> 8);
        }
        LND_ENCODED_SOURCE_OPTIONS options = {.codec_name = "test", .block_frames = 3, .length_bytes = 44};
        test_input input = {.data = file, .size = sizeof file, .limit = 3};
        LND_SOURCE *source = LND_SourceCreateEncodedInput(&input_procs, &input, sizeof file, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
        CHECK(source != nullptr);
        if (!source) continue;
        CHECK(LND_SourceGetCodec(source) == &codec && LND_SourceGetFormat(source) == LND_FORMAT_S16);
        CHECK(LND_SourceGetSampleRateHz(source) == 16000 && LND_SourceGetChannels(source) == 2 && LND_SourceGetLengthFrames(source) == 10);
        int16_t data[28];
        memset(data, 0x5a, sizeof data);
        LND_PCM pcm = {.data = data, .channels = 2, .frames = 14, .format = LND_FORMAT_S16};
        unsigned before = allocations;
        CHECK(LND_SourceReadPcm(source, &pcm, 2, 12) == 10);
        CHECK(allocations == before && LND_SourceGetPositionFrames(source) == 10);
        for (unsigned i = 0; i < 20; i++) CHECK(data[4 + i] == 1000 + i);
        CHECK(data[0] == 0x5a5a && data[27] == 0x5a5a);
        CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
        int16_t strided[40];
        memset(strided, 0x5a, sizeof strided);
        LND_PCM padded = {.data = strided, .channels = 2, .frames = 10, .format = LND_FORMAT_S16, .stride_bytes = 8};
        CHECK(LND_SourceReadPcm(source, &padded, 1, 7) == 7);
        for (unsigned f = 0; f < 7; f++) CHECK(strided[(f + 1) * 4] == 1000 + f * 2 && strided[(f + 1) * 4 + 1] == 1001 + f * 2);
        for (unsigned f = 0; f < 10; f++) CHECK(strided[f * 4 + 2] == 0x5a5a && strided[f * 4 + 3] == 0x5a5a);
        CHECK(LND_SourceSeekFrames(source, 0) == LND_OK);
        alignas(max_align_t) uint8_t unaligned[30];
        memset(unaligned, 0x5a, sizeof unaligned);
        LND_PCM packed = {.data = unaligned + 1, .channels = 2, .frames = 7, .format = LND_FORMAT_S16};
        CHECK(LND_SourceReadPcm(source, &packed, 0, 7) == 7);
        CHECK(!memcmp(unaligned + 1, file + 4, 28) && unaligned[0] == 0x5a && unaligned[29] == 0x5a);
        CHECK(LND_SourceSeekFrames(source, 9) == LND_OK);
        CHECK(LND_SourceRead(source, data, LND_FORMAT_S16, 3) == 1 && data[0] == 1018);
        CHECK(LND_SourceSeekFrames(source, 999) == LND_OK && LND_SourceGetPositionFrames(source) == 10);
        LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
        CHECK(sound != nullptr);
        LND_RENDERER_CONFIG config = {.render = LND_SoundRenderPcm, .user = sound, .channels = 2, .sample_rate_hz = 16000, .block_frames = 3};
        LND_RENDERER *renderer = LND_RendererCreateProc(&config);
        CHECK(renderer != nullptr && LND_SourceFree(source) == LND_ERR_BUSY);
        CHECK(LND_SoundSeekFrames(sound, 0) == LND_OK && LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundPlay(sound) == LND_OK);
        before = allocations;
        CHECK(LND_RendererFillPcm(renderer, &pcm, 0, 14) == LND_OK);
        for (unsigned i = 0; i < 28; i++) CHECK(data[i] == 1000 + i % 20);
        CHECK(allocations == before);
        CHECK(LND_RendererFree(renderer) == LND_OK && LND_SourceFree(source) == LND_OK && input.closes == 1);
        unsigned baseline = live_allocations;
        for (int failure = 0; failure < 8; failure++) {
            fail_allocation = failure;
            source = LND_SourceCreateEncodedMemory(file, sizeof file, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
            if (source) CHECK(LND_SourceFree(source) == LND_OK);
            CHECK(live_allocations == baseline);
        }
        fail_allocation = -1;
        mode = 1;
        source = LND_SourceCreateEncodedMemory(file, sizeof file, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options);
        CHECK(source && LND_SourceReadPcm(source, &pcm, 0, 2) == LND_ERR_IO);
        CHECK(LND_SourceFree(source) == LND_OK);
        mode = 2;
        CHECK(!LND_SourceCreateEncodedMemory(file, sizeof file, LND_ENCODED_SOURCE_LIGHTWEIGHT, &options));
        CHECK(LND_ErrorGetLast() == LND_ERR_FORMAT && live_allocations == baseline);
        mode = 0;
        CHECK(!LND_SourceCreateEncodedMemory(file, sizeof file, LND_ENCODED_SOURCE_LIGHTWEIGHT | LND_ENCODED_SOURCE_PRELOAD, &options));
        LND_LibraryFree();
        CHECK(!live_allocations);
    }
    printf("file source: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
