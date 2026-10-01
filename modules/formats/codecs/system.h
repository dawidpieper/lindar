#pragma once

#include "src/alloc.h"
#include "io/io.h"
#include "lindar_codecs.h"

#include <string.h>

static int32_t lnd_system_probe(LND_IO *io) {
    uint8_t h[16];
    int64_t n = LND_IoRead(io, h, sizeof h);
    if (n < 4) return 0;
    if (!memcmp(h, "RIFF", 4) || !memcmp(h, "RF64", 4) || !memcmp(h, "BW64", 4) || !memcmp(h, ".snd", 4) || !memcmp(h, "FORM", 4) || !memcmp(h, "OggS", 4) ||
        !memcmp(h, "fLaC", 4) || !memcmp(h, "caff", 4) || !memcmp(h, "ID3", 3) || !memcmp(h, "#!AMR", n < 5 ? n : 5) ||
        (n >= 8 && (!memcmp(h + 4, "ftyp", 4) || !memcmp(h + 4, "moov", 4) || !memcmp(h + 4, "mdat", 4))) || (h[0] == 0xff && (h[1] & 0xe0) == 0xe0) ||
        (h[0] == 0x1a && h[1] == 0x45 && h[2] == 0xdf && h[3] == 0xa3) || (h[0] == 0x30 && h[1] == 0x26 && h[2] == 0xb2 && h[3] == 0x75) ||
        (h[0] == 0x0b && h[1] == 0x77) || (h[0] == 0x47 && (h[3] & 0x30)) || (h[0] == 0x56 && (h[1] & 0xe0) == 0xe0))
        return 30;
    return 0;
}

static inline uint64_t lnd_system_frames(uint64_t time, uint32_t sample_rate_hz, uint64_t scale) {
    if (time / scale > UINT64_MAX / sample_rate_hz) return UINT64_MAX;
    uint64_t whole = time / scale * sample_rate_hz;
    uint64_t part = time % scale * sample_rate_hz / scale;
    return whole > UINT64_MAX - part ? UINT64_MAX : whole + part;
}

static inline uint64_t lnd_system_frames_ceil(uint64_t time, uint32_t sample_rate_hz, uint64_t scale) {
    uint64_t frames = lnd_system_frames(time, sample_rate_hz, scale);
    return frames < UINT64_MAX && time % scale * sample_rate_hz % scale ? frames + 1 : frames;
}

static inline uint64_t lnd_system_frames_nearest(uint64_t time, uint32_t sample_rate_hz, uint64_t scale) {
    uint64_t frames = lnd_system_frames(time, sample_rate_hz, scale);
    uint64_t remainder = time % scale * sample_rate_hz % scale;
    return frames < UINT64_MAX && remainder >= (scale + 1) / 2 ? frames + 1 : frames;
}
