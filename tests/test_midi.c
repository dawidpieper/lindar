#include "codec_test.h"
#include "lindar_midi.h"
#include <math.h>
static const uint8_t midi[] = {'M', 'T', 'h', 'd', 0, 0,    0,  6,   0,    0,    0,    1,  1, 0xe0, 'M',  'T',  'r', 'k',
                               0,   0,   0,   13,  0, 0x90, 60, 100, 0x83, 0x60, 0x80, 60, 0, 0,    0xff, 0x2f, 0};
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    test_init(LND_LAYOUT_PLANAR);
    size_t bytes;
    uint8_t *data = read_file(argv[1], &bytes);
    LND_SOUNDFONT *font = LND_SoundfontCreateMemory(data, bytes);
    CHECK(font && LND_SoundfontGetPresetCount(font) > 0);
    LND_SOUNDFONT_PRESET preset;
    CHECK(LND_SoundfontGetPreset(font, 0, &preset) == LND_OK && preset.name[0]);
    CHECK(LND_SoundfontGetPreset(font, UINT32_MAX, &preset) == LND_ERR_INVALID_ARG);
    LND_MIDI_OPTIONS options = {.soundfont = font, .block_frames = 127, .release_ms = 200};
    LND_SOURCE *source = LND_SourceCreateMidiMemory(midi, sizeof midi, &options);
    CHECK(source != nullptr);
    LND_SoundfontFree(font);
    free(data);
    if (!source) return 1;
    LND_SOURCE_INFO info;
    CHECK(LND_SourceGetInfo(source, &info) == LND_OK && info.length_frames == 33600 && info.length_kind == LND_LENGTH_EXACT);
    float audio[8192], again[2048];
    CHECK(LND_SourceRead(source, audio, LND_FORMAT_F32, 4096) == 4096);
    double energy = 0;
    for (unsigned i = 0; i < 8192; ++i) {
        CHECK(isfinite(audio[i]));
        energy += audio[i] * audio[i];
    }
    CHECK(energy > 0.01);
    CHECK(LND_SourceSeekFrames(source, 1024) == LND_OK);
    CHECK(LND_SourceRead(source, again, LND_FORMAT_F32, 1024) == 1024);
    CHECK(!memcmp(again, audio + 2048, sizeof again));
    CHECK(LND_SourceSeekFrames(source, info.length_frames) == LND_OK);
    CHECK(LND_SourceRead(source, again, LND_FORMAT_F32, 1) == 0);
    CHECK(LND_SourceSeekFrames(source, info.length_frames + 1) == LND_OK && LND_SourceGetPositionFrames(source) == info.length_frames);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    CHECK(sound && LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    CHECK(LND_SoundSeekFrames(sound, info.length_frames - 128) == LND_OK);
    LND_PCM loop = {.data = again, .frames = 1024, .channels = 2, .format = LND_FORMAT_F32};
    CHECK(LND_SoundRenderPcm(sound, &loop, 0, 1024) == 1024);
    CHECK(!memcmp(again + 256, audio, 896 * 2 * sizeof(float)));
    uint64_t position = LND_SoundGetPositionFrames(sound);
    CHECK(LND_SoundSetPause(sound, true) == LND_OK);
    CHECK(LND_SoundRenderPcm(sound, &loop, 0, 1024) == 0 && LND_SoundGetPositionFrames(sound) == position);
    CHECK(LND_SoundFree(sound) == LND_OK);
    CHECK(LND_SourceFree(source) == LND_OK);

    LND_LibraryFree();
    CHECK(live_allocations == 0);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
