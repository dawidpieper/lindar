#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "lindar_asio.h"
#include "lindar_graph.h"
#include "lindar.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct tone {
    double phase;
    double step;
    uint32_t channels;
} tone;

static int64_t tone_read(void *user, void *memory, uint64_t frames) {
    tone *state = user;
    float *output = memory;
    for (uint64_t i = 0; i < frames; i++) {
        float value = (float)(0.1 * sin(state->phase));
        state->phase += state->step;
        if (state->phase >= 6.283185307179586)
            state->phase -= 6.283185307179586;
        for (uint32_t channel = 0; channel < state->channels; channel++)
            output[i * state->channels + channel] = value;
    }
    return (int64_t)frames;
}

static bool number(const char *text, uint32_t maximum, uint32_t *value) {
    char *end;
    unsigned long parsed = strtoul(text, &end, 10);
    if (!*text || *end || parsed > maximum)
        return false;
    *value = (uint32_t)parsed;
    return true;
}

int main(int argc, char **argv) {
    uint32_t index = 0, seconds = 3;
    bool list = argc > 1 && !strcmp(argv[1], "--list");
    bool panel = argc > 1 && !strcmp(argv[1], "--panel");
    int first = panel ? 2 : 1;
    if ((!list && argc > first && !number(argv[first], UINT32_MAX, &index)) ||
        (!list && !panel && argc > 2 && (!number(argv[2], 3600, &seconds) || !seconds))) {
        fprintf(stderr, "Usage: asio_duplex --list | --panel [device] | [device] [seconds]\n");
        return 1;
    }
    LND_DeviceSetPreferredBackend(LND_DeviceBackendFind("asio"));
    LND_ConfigSet(LND_CFG_DEVICES_AUTO_OPEN, 0);
    int32_t result = LND_LibraryInit();
    if (result != LND_OK) {
        fprintf(stderr, "Init: %s\n", LND_ErrorGetString(result));
        return 1;
    }
    uint32_t count = LND_DeviceGetCount(LND_DEVICE_OUTPUT);
    for (uint32_t i = 0; i < count; i++) {
        LND_DEVICE *device = LND_DeviceGet(LND_DEVICE_OUTPUT, i);
        LND_ASIO_INFO info;
        printf("%u: %s [%s]\n", i, LND_DeviceGetName(device), LND_DeviceGetId(device));
        if (LND_DeviceGetAsioInfo(device, &info) == LND_OK)
            printf("   %u inputs, %u outputs, %u Hz, preferred %u frames\n", info.input_channels, info.output_channels, info.sample_rate_hz,
                   info.preferred_buffer_frames);
    }
    if (list) {
        if (!count)
            puts("No ASIO output driver is registered for this executable architecture.");
        LND_LibraryFree();
        return 0;
    }
    if (index >= count) {
        fprintf(stderr, "ASIO device index is unavailable.\n");
        LND_LibraryFree();
        return 1;
    }
    LND_DEVICE *output = LND_DeviceGet(LND_DEVICE_OUTPUT, index);
    if (panel) {
        result = LND_DeviceShowAsioControlPanel(output);
        if (result != LND_OK)
            fprintf(stderr, "Control panel: %s\n", LND_ErrorGetString(result));
        LND_LibraryFree();
        return result == LND_OK ? 0 : 1;
    }
    char id[40];
    snprintf(id, sizeof id, "%s", LND_DeviceGetId(output));
    LND_DEVICE *input = LND_DeviceFind(LND_DEVICE_INPUT, id);
    LND_DEVICE_INSTANCE *instance = LND_DeviceInstanceOpen(output);
    LND_SOURCE *source = nullptr, *capture = nullptr;
    if (!instance)
        result = LND_ErrorGetLast();
    else {
        uint32_t rate = LND_DeviceInstanceGetSampleRateHz(instance);
        uint32_t channels = LND_DeviceInstanceGetChannels(instance);
        tone state = {.step = 6.283185307179586 * 440.0 / rate, .channels = channels};
        LND_SOURCE_PROCS procs = {.read = tone_read};
        source = LND_SourceCreateProc(&procs, &state, LND_FORMAT_F32, channels, rate, 0);
        LND_SOUND *sound = source ? LND_SourceEnsureSound(source, &(LND_SOUND_CONFIG){.channels = channels, .sample_rate_hz = rate}) : nullptr;
        if (!sound)
            result = LND_ErrorGetLast();
        else {
            result = LND_SoundSetOutput(sound, LND_DeviceInstanceGetNode(instance));
            if (result == LND_OK)
                result = LND_SoundPlay(sound);
        }
        if (input)
            capture = LND_SourceCreateDevice(input, 1, rate, LND_CAPTURE_NONBLOCKING | LND_CAPTURE_RESAMPLE);
        if (result == LND_OK) {
            printf("Duplex: %u Hz, %u output channels, %u frames, latency %u frames; capture %s\n", rate, channels, LND_DeviceInstanceGetPeriodFrames(instance),
                   LND_DeviceInstanceGetLatencyFrames(instance), capture ? "enabled" : "unavailable");
            for (uint32_t tick = 0; tick < seconds * 20; tick++) {
                float samples[2048];
                int64_t frames = capture ? LND_SourceRead(capture, samples, LND_FORMAT_F32, 2048) : 0;
                double energy = 0;
                for (uint64_t i = 0; i < frames; i++)
                    energy += (double)samples[i] * samples[i];
                if (tick % 20 == 0) {
                    LND_ASIO_TIME time = {0};
                    LND_DeviceGetAsioTime(output, &time);
                    printf("frame %llu, input RMS %.6f\n", (unsigned long long)time.sample_position_frames, frames ? sqrt(energy / frames) : 0.0);
                }
                Sleep(50);
            }
        }
        if (source) {
            LND_SourceFree(source);
            source = nullptr;
        }
    }
    if (capture)
        LND_SourceFree(capture);
    if (instance)
        LND_DeviceInstanceClose(instance);
    LND_LibraryFree();
    if (result != LND_OK)
        fprintf(stderr, "%s\n", LND_ErrorGetString(result));
    return result == LND_OK ? 0 : 1;
}
