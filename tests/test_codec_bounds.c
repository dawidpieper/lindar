#include "codec_test.h"
#include "src/platform.h"

typedef struct sequential_input {
    const uint8_t *data;
    size_t bytes, position;
} sequential_input;

static int64_t sequential_read(void *user, void *dst, size_t bytes) {
    sequential_input *s = user;
    size_t take = s->bytes - s->position;
    if (take > bytes) take = bytes;
    if (!take) return LND_READ_EOF;
    memcpy(dst, s->data + s->position, take);
    s->position += take;
    return (int64_t)take;
}

static const LND_CODEC *find_codec(const char *name) {
    for (uint32_t i = 0; i < LND_CodecGetCount(); i++) {
        const LND_CODEC *codec = LND_CodecGet(i);
        if (!strcmp(codec->name, name)) return codec;
    }
    return nullptr;
}

static void read_chain(const LND_CODEC *codec, const uint8_t *data, size_t bytes, bool compatible, bool sequential) {
    sequential_input input = {.data = data, .bytes = bytes};
    LND_IO_STREAM_INPUT_PROCS procs = {.read = sequential_read};
    LND_IO *io = sequential ? LND_IoCreateStream(&procs, &input, bytes) : LND_IoOpenMemory(data, bytes);
    CHECK(io != nullptr);
    if (!io) return;
    void *state = nullptr;
    LND_CODEC_INFO info = {0};
    int32_t result = codec->open(io, &info, &state);
    if (result == LND_OK) {
        CHECK(info.channels == 1 || info.channels == 2);
        size_t capacity = 31 * info.channels * LND_PcmGetSampleBytes(info.format);
        uint8_t *pcm = malloc(capacity + 32);
        CHECK(pcm != nullptr);
        uint64_t got = 0, total = 0;
        if (pcm) {
            for (unsigned i = 0; i < 1024; i++) {
                memset(pcm + capacity, 0x5a, 32);
                got = codec->read(state, pcm, 31);
                for (unsigned j = 0; j < 32; j++) CHECK(pcm[capacity + j] == 0x5a);
                if (!got || got == LND_CODEC_READ_ERROR) break;
                CHECK(got <= 31);
                total += got;
            }
            CHECK(compatible ? got == 0 && total > 0 : got == LND_CODEC_READ_ERROR);
            if (!compatible) {
                memset(pcm, 0x5a, capacity + 32);
                for (unsigned i = 0; i < 3; i++) CHECK(codec->read(state, pcm, 31) == LND_CODEC_READ_ERROR);
                for (size_t i = 0; i < capacity + 32; i++) CHECK(pcm[i] == 0x5a);
            }
            free(pcm);
        }
        codec->close(state);
    } else CHECK(!compatible && (result == LND_ERR_FORMAT || result == LND_ERR_UNSUPPORTED));
    LND_IoFree(io);
}

static void next_serial(uint8_t *data, size_t bytes) {
    for (size_t at = 0; at < bytes;) {
        CHECK(bytes - at >= 27);
        if (bytes - at < 27) return;
        size_t header = 27u + data[at + 26], page = header;
        CHECK(header <= bytes - at);
        if (header > bytes - at) return;
        for (size_t i = 27; i < header; i++) page += data[at + i];
        CHECK(page <= bytes - at);
        if (page > bytes - at) return;
        data[at + 17] ^= 0x80;
        memset(data + at + 22, 0, 4);
        uint32_t crc = 0;
        for (size_t i = 0; i < page; i++) {
            crc ^= (uint32_t)data[at + i] << 24;
            for (unsigned bit = 0; bit < 8; bit++) crc = crc << 1 ^ (crc & 0x80000000u ? 0x04c11db7u : 0);
        }
        for (unsigned i = 0; i < 4; i++) data[at + 22 + i] = (uint8_t)(crc >> (8 * i));
        at += page;
    }
}

static void test_chains(const char *name, const char *extension) {
    const LND_CODEC *codec = find_codec(name);
    CHECK(codec != nullptr);
    if (!codec) return;
    char path[80];
    size_t sizes[2];
    uint8_t *parts[2];
    for (unsigned i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "audiosamples/bounds/%s.%s", i ? "stereo" : "mono", extension);
        parts[i] = read_file(path, &sizes[i]);
    }
    if (parts[0] && parts[1]) {
        uint8_t *chain = malloc(2 * (sizes[0] + sizes[1]));
        CHECK(chain != nullptr);
        if (chain) {
            for (unsigned a = 0; a < 2; a++)
                for (unsigned b = 0; b < 2; b++) {
                    memcpy(chain, parts[a], sizes[a]);
                    memcpy(chain + sizes[a], parts[b], sizes[b]);
                    if (!strcmp(extension, "spx")) next_serial(chain + sizes[a], sizes[b]);
                    read_chain(codec, chain, sizes[a] + sizes[b], a == b, false);
                    if (!strcmp(name, "speex")) read_chain(codec, chain, sizes[a] + sizes[b], a == b, true);
                }
            free(chain);
        }
    }
    free(parts[0]);
    free(parts[1]);
}

static void test_prefixes(const char *name, const char *path) {
    const LND_CODEC *codec = find_codec(name);
    if (!codec) return;
    size_t bytes = 0;
    uint8_t *data = read_file(path, &bytes);
    if (!data) return;
    size_t limit = bytes < 128 ? bytes : 128;
    for (size_t length = 0; length <= limit; length++) {
        uint8_t *prefix = malloc(length ? length : 1);
        CHECK(prefix != nullptr);
        if (!prefix) break;
        memcpy(prefix, data, length);
        LND_IO *io = LND_IoOpenMemory(prefix, length);
        CHECK(io != nullptr);
        void *state = nullptr;
        LND_CODEC_INFO info = {0};
        if (io && codec->open(io, &info, &state) == LND_OK) {
            bool valid = info.channels && info.channels <= LND_MAX_CHANNELS && LND_PcmGetSampleBytes(info.format);
            CHECK(valid);
            if (valid) {
                size_t capacity = 31 * info.channels * LND_PcmGetSampleBytes(info.format);
                void *pcm = malloc(capacity);
                CHECK(pcm != nullptr);
                if (pcm) {
                    for (unsigned i = 0; i < 16; i++) {
                        uint64_t got = codec->read(state, pcm, 31);
                        CHECK(got <= 31 || got == LND_CODEC_READ_ERROR);
                        if (!got || got == LND_CODEC_READ_ERROR) break;
                    }
                    free(pcm);
                }
            }
            codec->close(state);
        }
        LND_IoFree(io);
        free(prefix);
    }
    free(data);
}

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    static const struct { const char *codec, *path; } samples[] = {
        {"wav", "audiosamples/tone.wav"}, {"aiff", "audiosamples/tone.aiff"},
        {"mp3", "audiosamples/tone.mp3"}, {"vorbis", "audiosamples/tone.ogg"},
        {"opus", "audiosamples/tone.opus"}, {"flac", "audiosamples/tone.flac"},
        {"aac", "audiosamples/bounds/mono.aac"}, {"speex", "audiosamples/bounds/mono.spx"},
        {"ffmpeg", "audiosamples/bounds/mono.aac"},
    };
    for (unsigned i = 0; i < sizeof samples / sizeof *samples; i++) test_prefixes(samples[i].codec, samples[i].path);

#if LND_MODULE_SPEEX_DECODER
    test_chains("speex", "spx");
#endif
#if LND_MODULE_FFMPEG_DECODER
    test_chains("ffmpeg", "aac");
#endif
    LND_LibraryFree();
    CHECK(live_allocations == 0);
    printf("codec bounds: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
