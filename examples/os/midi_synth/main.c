#include "lindar_midi.h"
#include "lindar_file_io.h"
#include <stdio.h>

static const uint8_t midi[] = {
    'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,'M','T','r','k',0,0,0,13,
    0,0x90,60,100,0x83,0x60,0x80,60,0,0,0xff,0x2f,0
};

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: midi_synth soundfont.sf2\n"); return 2; }
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_IO *io = LND_IoOpenFile(argv[1]);
    LND_SOUNDFONT *font = io ? LND_SoundfontCreateIo(io) : nullptr;
    LND_IoFree(io);
    LND_MIDI_OPTIONS options = {.soundfont = font, .channels = 2, .sample_rate_hz = 48000, .release_ms = 200};
    LND_SOURCE *source = font ? LND_SourceCreateMidiMemory(midi, sizeof midi, &options) : nullptr;
    LND_SoundfontFree(font);
    float samples[512];
    LND_PCM pcm = {.data = samples, .frames = 256, .channels = 2, .format = LND_FORMAT_F32};
    int64_t got = -1, frames = 0;
    double energy = 0;
    if (source) while ((got = LND_SourceReadPcm(source, &pcm, 0, 256)) > 0) {
        frames += got;
        for (int64_t i = 0; i < got * 2; i++) energy += samples[i] * samples[i];
    }
    printf("MIDI: %lld frames, energy=%.3f\n", (long long)frames, energy);
    LND_SourceFree(source);
    LND_LibraryFree();
    return got == 0 && frames > 0 && energy > 0 ? 0 : 1;
}
