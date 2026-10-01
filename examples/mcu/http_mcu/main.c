#include "lindar_http.h"
#include "lindar.h"

#include <stdio.h>
#include <string.h>

enum { RATE = 8000, FRAMES = 8000, BLOCK = 128 };
static const uint8_t header[] = {82, 73, 70, 70, 164, 62, 0,   0,  87, 65, 86, 69, 102, 109, 116, 32, 16,  0,  0,   0,  1, 0,
                                 1,  0,  64, 31, 0,   0,  128, 62, 0,  0,  2,  0,  16,  0,   100, 97, 116, 97, 128, 62, 0, 0};
static const int16_t sine[] = {0, 3135, 5793, 7568, 8192, 7568, 5793, 3135, 0, -3135, -5793, -7568, -8192, -7568, -5793, -3135};
static struct {
    size_t position;
    uint32_t polls;
    bool open;
} network;
static alignas(max_align_t) uint8_t sound_memory[512], renderer_memory[4096];
static int16_t dma[2][BLOCK];

static int32_t begin(void *user, const LND_HTTP_REQUEST *request, void **transfer) {
    (void)user;
    if (network.open || strcmp(request->url, "http://mcu.demo/sine.wav") || request->range) return LND_ERR_INVALID_ARG;
    network.position = network.polls = 0;
    network.open = true;
    *transfer = &network;
    return LND_OK;
}

static int32_t poll(void *transfer, LND_HTTP_RESPONSE *response, void *buffer, size_t capacity, size_t *written) {
    (void)transfer;
    *response = (LND_HTTP_RESPONSE){.status = 200, .headers_complete = true, .length_known = true, .content_length_bytes = sizeof header + FRAMES * 2};
    *written = 0;
    if (++network.polls % 4 == 0) return LND_HTTP_PENDING;
    size_t take = sizeof header + FRAMES * 2 - network.position;
    if (take > capacity) take = capacity;
    if (take > 193) take = 193;
    uint8_t *out = buffer;
    for (size_t i = 0; i < take; i++) {
        size_t at = network.position++;
        if (at < sizeof header)
            out[i] = header[at];
        else {
            size_t byte = at - sizeof header;
            uint16_t value = (uint16_t)sine[(byte / 2) % 16];
            out[i] = (uint8_t)(value >> (byte % 2 * 8));
        }
    }
    *written = take;
    return network.position == sizeof header + FRAMES * 2 ? LND_HTTP_DONE : LND_HTTP_PENDING;
}

static void close_transfer(void *transfer) {
    (void)transfer;
    network.open = false;
}

static const LND_HTTP_TRANSPORT transport = {
    .size = sizeof(LND_HTTP_TRANSPORT), .capabilities = LND_HTTP_CAP_HTTP | LND_HTTP_CAP_MANUAL, .open = begin, .poll = poll, .close = close_transfer};

int main(void) {
    // Replace this simulated transport with the board HTTP client.
    if (LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) != LND_OK || LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_S16) != LND_OK ||
        LND_LibraryInit() != LND_OK)
        return 1;
    LND_HTTP_OPTIONS options;
    LND_HttpOptionsInit(&options);
    options.transport = &transport;
    options.execution = LND_HTTP_EXEC_MANUAL;
    options.buffer.compressed_bytes = 4096;
    options.buffer.segment_bytes = 4096;
    options.buffer.pcm_ms = 128;
    options.buffer.start_ms = options.buffer.resume_ms = 32;
    options.buffer.event_count = 8;
    LND_HTTP_OPEN *open = LND_HttpOpen("http://mcu.demo/sine.wav", &options);
    LND_SOURCE *source = nullptr;
    LND_SOUND *sound = nullptr;
    LND_RENDERER *renderer = nullptr;
    uint64_t energy = 0, frames = 0;
    int result = 1;
    for (uint32_t tick = 0; open && tick < 10000; tick++) {
        if (LND_HttpUpdate((uint64_t)tick * BLOCK * 1000 / RATE, 8) != LND_OK) break;
        if (!source) {
            int32_t r = LND_HttpOpenTakeSource(open, &source);
            if (r < 0) break;
            if (!source) continue;
            sound = LND_SoundInit(sound_memory, sizeof sound_memory, source);
            LND_RENDERER_CONFIG config = {.render = LND_SoundRenderPcm, .user = sound, .sample_rate_hz = RATE, .channels = 1, .block_frames = BLOCK};
            renderer = sound ? LND_RendererInit(renderer_memory, sizeof renderer_memory, &config) : nullptr;
            if (!renderer || LND_SoundPlay(sound) != LND_OK) break;
        }
        LND_PCM output = {.data = dma[tick % 2], .frames = BLOCK, .channels = 1, .format = LND_FORMAT_S16};
        if (LND_RendererFillPcm(renderer, &output, 0, BLOCK) != LND_OK) break;
        for (size_t i = 0; i < BLOCK; i++) energy += (uint64_t)((int32_t)dma[tick % 2][i] * dma[tick % 2][i]);
        frames = LND_SourceGetPositionFrames(source);
        if (LND_SourceGetStatus(source) == LND_SOURCE_EOF) {
            result = frames == FRAMES && energy ? 0 : 1;
            break;
        }
    }
    printf("MCU transport simulation: frames=%llu energy=%llu result=%d\n", (unsigned long long)frames, (unsigned long long)energy, result);
    LND_LibraryFree();
    return result;
}
