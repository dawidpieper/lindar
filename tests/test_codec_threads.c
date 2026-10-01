#include "lindar_codecs.h"
#include "src/atomic.h"
#include "src/format.h"
#include "src/thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct reader {
    const LND_CODEC *codec;
    const void *data;
    size_t bytes;
    lnd_atomic_u32 *ready, *start;
    uint64_t hash, frames;
    bool ok;
} reader;

static void decode(void *user) {
    reader *r = user;
    lnd_thread_com_init();
    lnd_add(r->ready, 1);
    while (!lnd_load(r->start)) lnd_sleep_ms(0);
    r->ok = true;
    for (unsigned pass = 0; pass < 3 && r->ok; pass++) {
        LND_IO *io = LND_IoOpenMemory(r->data, r->bytes);
        LND_CODEC_INFO info = {0};
        void *state = nullptr;
        if (!io || r->codec->open(io, &info, &state) != LND_OK) {
            if (io) LND_IoFree(io);
            r->ok = false;
            break;
        }
        uint32_t stride = lnd_format_bytes(info.format) * info.channels;
        unsigned char *pcm = stride && info.channels <= LND_MAX_CHANNELS ? malloc(257 * stride) : nullptr;
        uint64_t hash = 14695981039346656037ull, frames = 0;
        if (!pcm) r->ok = false;
        for (unsigned block = 0; block < 16 && r->ok; block++) {
            uint64_t got = r->codec->read(state, pcm, 257);
            if (got > 257) {
                r->ok = false;
                break;
            }
            for (size_t i = 0; i < got * stride; i++) hash = (hash ^ pcm[i]) * 1099511628211ull;
            frames += got;
            if (!got) break;
        }
        free(pcm);
        r->codec->close(state);
        LND_IoFree(io);
        if (!frames || (pass && (hash != r->hash || frames != r->frames))) r->ok = false;
        r->hash = hash;
        r->frames = frames;
    }
    lnd_thread_com_free();
}

static bool check(const LND_CODEC *codec, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    long bytes = ftell(file);
    rewind(file);
    void *data = bytes > 0 ? malloc((size_t)bytes) : nullptr;
    bool loaded = data && fread(data, 1, (size_t)bytes, file) == (size_t)bytes;
    fclose(file);
    if (!loaded) {
        free(data);
        return false;
    }
    lnd_atomic_u32 ready = 0, start = 0;
    reader readers[4];
    lnd_thread threads[4];
    for (unsigned i = 0; i < 4; i++) {
        readers[i] = (reader){.codec = codec, .data = data, .bytes = (size_t)bytes, .ready = &ready, .start = &start};
        if (lnd_thread_create(&threads[i], decode, &readers[i]) != LND_OK) exit(2);
    }
    while (lnd_load(&ready) != 4) lnd_sleep_ms(0);
    lnd_store(&start, 1);
    bool ok = true;
    for (unsigned i = 0; i < 4; i++) lnd_thread_join(&threads[i]);
    for (unsigned i = 0; i < 4; i++)
        if (!readers[i].ok || readers[i].hash != readers[0].hash || readers[i].frames != readers[0].frames) ok = false;
    free(data);
    printf("%s: %s\n", codec->name, ok ? "OK" : "FAIL");
    return ok;
}

int main(void) {
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_MANUAL) != LND_OK || LND_LibraryInit() != LND_OK) return 1;
    static const struct { const char *codec, *path; } cases[] = {
        {"mediafoundation", "audiosamples/tone.wav"}, {"ffmpeg", "audiosamples/tone.wav"},
        {"wav", "audiosamples/tone.wav"}, {"aiff", "audiosamples/tone.aiff"}, {"mp3", "audiosamples/tone.mp3"},
        {"opus", "audiosamples/tone.opus"}, {"vorbis", "audiosamples/tone.ogg"}, {"flac", "audiosamples/tone.flac"},
        {"aac", "audiosamples/tone.aac"}, {"speex", "audiosamples/tone.spx"}
    };
    bool ok = true;
    for (unsigned c = 0; c < sizeof cases / sizeof *cases; c++) for (uint32_t i = 0; i < LND_CodecGetCount(); i++) {
        const LND_CODEC *codec = LND_CodecGet(i);
        if (!strcmp(codec->name, cases[c].codec) && !check(codec, cases[c].path)) ok = false;
    }
    LND_LibraryFree();
    return ok ? 0 : 1;
}
