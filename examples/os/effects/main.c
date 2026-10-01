#include "lindar_effects.h"
#include "lindar.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>

enum { RATE = 48000, INPUT_FRAMES = RATE, BLOCK_FRAMES = 128 };

static int64_t generate(void *user, void *dst, uint64_t frames) {
    uint64_t *position = user;
    if (*position == INPUT_FRAMES) return LND_READ_EOF;
    if (frames > INPUT_FRAMES - *position) frames = INPUT_FRAMES - *position;
    float *out = dst;
    for (uint64_t f = 0; f < frames; f++) {
        float x = 0.2f * sinf((float)(6.283185307179586 * 440 * (*position + f) / RATE));
        out[f * 2] = out[f * 2 + 1] = x;
    }
    *position += frames;
    return (int64_t)frames;
}

int main(void) {
    // Keep rendering after source EOF to collect the effect tail.
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_GRAPH_GAIN_RAMP_FRAMES, 0) != LND_OK || LND_LibraryInit() != LND_OK) return 1;
    uint64_t position = 0;
    LND_SOURCE_PROCS procs = {.read = generate, .length_frames = INPUT_FRAMES};
    LND_SOURCE *source = LND_SourceCreateProc(&procs, &position, LND_FORMAT_F32, 2, RATE, LND_GRAPH_SOURCE_DIRECT);
    LND_SOUND *sound = source ? LND_SourceEnsureSound(source, nullptr) : nullptr;
    LND_NODE *flanger = LND_NodeCreateFlanger(2, RATE, nullptr);
    LND_EFFECT_PARAM params[] = {{LND_EFFECT_PARAM_DELAY_MS, 180}, {LND_EFFECT_PARAM_WET, 0.35f}, {LND_EFFECT_PARAM_FEEDBACK, 0.4f}};
    LND_EFFECT_CONFIG config = {.params = params, .param_count = 3, .max_delay_ms = 250, .tail_ms = 3000};
    LND_NODE *echo = LND_NodeCreateEcho(2, RATE, &config);
    LND_RENDERER *renderer = nullptr;
    if (sound && flanger && echo && LND_NodeConnect(LND_SourceEnsureNode(source), flanger) == LND_OK && LND_NodeConnect(flanger, echo) == LND_OK)
        renderer = LND_RendererCreateNode(echo);
    int32_t result = renderer ? LND_SoundPlay(sound) : LND_ErrorGetLast();
    if (!renderer && result == LND_OK) result = LND_ERR_STATE;
    int16_t buffers[2][BLOCK_FRAMES * 2];
    uint32_t block = 0, checksum = 2166136261u;
    while (result == LND_OK && LND_NodeGetStatus(echo) != LND_SOURCE_EOF) {
        LND_PCM pcm = {.data = buffers[block++ & 1], .frames = BLOCK_FRAMES, .channels = 2, .format = LND_FORMAT_S16LE};
        result = LND_RendererFillPcm(renderer, &pcm, 0, pcm.frames);
        if (result != LND_OK) break;
        const int16_t *samples = pcm.data;
        for (size_t i = 0; i < BLOCK_FRAMES * 2; i++)
            checksum = (checksum ^ (uint16_t)samples[i]) * 16777619u;
    }
    LND_EFFECT_INFO info;
    if (result == LND_OK) result = LND_NodeGetEffectInfo(echo, &info);
    if (result == LND_OK)
        printf("effects: flanger -> echo, %" PRIu64 " frames including tails, %" PRIu64 " final tail frames, checksum %08x\n", info.output_frames,
               info.output_frames - info.input_frames, checksum);
    else
        fprintf(stderr, "effects: error %d\n", result);
    if (renderer) LND_RendererFree(renderer);
    if (echo) LND_NodeFree(echo);
    if (flanger) LND_NodeFree(flanger);
    if (source) LND_SourceFree(source);
    LND_LibraryFree();
    return result == LND_OK ? 0 : 1;
}
