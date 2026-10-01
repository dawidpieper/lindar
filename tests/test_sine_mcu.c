#include "../examples/mcu/sine_mcu/sine_mcu.h"

#include <math.h>
#include <stdio.h>

static unsigned checks, failures;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)

static void run(int32_t layout) {
    CHECK(sine_mcu_configure() == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, layout) == LND_OK);
    size_t bytes = sine_mcu_memory_size();
    max_align_t memory[(bytes + sizeof(max_align_t) - 1) / sizeof(max_align_t)];
    sine_mcu sine = {0};
    CHECK(sine_mcu_init(&sine, memory, sizeof memory, 48000, 440) == LND_OK);
    CHECK(LND_ConfigGet(LND_CFG_RUN_MODE) == LND_MODE_SINGLE_THREADED);
    CHECK(LND_ConfigGet(LND_CFG_INTERNAL_FORMAT) == LND_FORMAT_S16LE);
    CHECK(LND_ConfigGet(LND_CFG_INTERNAL_LAYOUT) == (uint64_t)layout);
    int16_t data[129];
    size_t position = 0;
    for (unsigned block = 0; block < 100; block++) {
        size_t frames = 1 + block % 127;
        data[0] = data[frames + 1] = 12345;
        CHECK(sine_mcu_fill(&sine, data + 1, frames) == LND_OK);
        CHECK(data[0] == 12345 && data[frames + 1] == 12345);
        for (size_t f = 0; f < frames; f++) {
            double expected = 32767 * sin(6.2831853071795864769 * 440 * (double)(position + f) / 48000);
            CHECK(fabs(data[f + 1] - expected) < 2.0);
        }
        position += frames;
    }
    CHECK(LND_SoundGetPositionFrames(sine.sound) == position);
    CHECK(LND_SoundSetPause(sine.sound, true) == LND_OK);
    CHECK(sine_mcu_fill(&sine, data, 128) == LND_OK);
    for (size_t f = 0; f < 128; f++)
        CHECK(data[f] == 0);
    CHECK(LND_SoundGetPositionFrames(sine.sound) == position);
    CHECK(LND_SoundStop(sine.sound) == LND_OK);
    CHECK(LND_SoundGetPositionFrames(sine.sound) == 0);
    CHECK(LND_SoundPlay(sine.sound) == LND_OK);
    CHECK(sine_mcu_fill(&sine, data, 128) == LND_OK);
    for (size_t f = 0; f < 128; f++) {
        double expected = 32767 * sin(6.2831853071795864769 * 440 * (double)f / 48000);
        CHECK(fabs(data[f] - expected) < 2.0);
    }
    CHECK(sine_mcu_fill(&sine, nullptr, 1) == LND_ERR_INVALID_ARG);
    CHECK(sine_mcu_fill(&sine, nullptr, 0) == LND_OK);
    CHECK(sine_mcu_free(&sine) == LND_OK);
    LND_LibraryFree();
}

int main(void) {
    run(LND_LAYOUT_INTERLEAVED);
    run(LND_LAYOUT_PLANAR);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
