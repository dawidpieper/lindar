#include "lindar_output.h"
#include <stdio.h>
#include <string.h>

typedef struct storage { uint8_t bytes[1024]; size_t position, size; } storage;

static size_t write_bytes(void *user, const void *data, size_t bytes) {
    storage *s = user;
    if (bytes > sizeof s->bytes - s->position) return 0;
    memcpy(s->bytes + s->position, data, bytes);
    s->position += bytes;
    if (s->size < s->position) s->size = s->position;
    return bytes;
}

static int32_t seek_bytes(void *user, uint64_t position) {
    storage *s = user;
    if (position > s->size) return LND_ERR_IO;
    s->position = (size_t)position;
    return LND_OK;
}

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (LND_LibraryInit() != LND_OK) return 1;
    storage buffer = {0};
    LND_IO_OUTPUT_PROCS procs = {.write = write_bytes, .seek = seek_bytes};
    LND_ENCODER_PARAMS params = {.encoder_name = "wav", .channels = 1, .sample_rate_hz = 8000, .format = LND_FORMAT_S16LE};
    LND_OUTPUT *output = LND_OutputCreateProc(&procs, &buffer, &params);
    int16_t samples[128] = {0};
    LND_PCM pcm = {.data = samples, .frames = 128, .channels = 1, .format = LND_FORMAT_S16LE};
    int32_t result = output ? LND_OutputWritePcm(output, &pcm, 0, 128) : LND_ErrorGetLast();
    // Finish patches the WAV header before the application consumes the bytes.
    if (result == LND_OK) result = LND_OutputFinish(output);
    if (result == LND_OK) printf("WAV in application memory: %zu bytes\n", buffer.size);
    LND_OutputFree(output);
    LND_LibraryFree();
    return result == LND_OK ? 0 : 1;
}
