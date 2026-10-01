#include "lindar.h"
#include "lnd_modules.h"
#if LND_MODULE_ANALYSIS
#include "lindar_analysis.h"
#endif
#if LND_MODULE_DEMUX
#include "lindar_demux.h"
#endif
#if LND_MODULE_METADATA
#include "lindar_metadata.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static double now(void) {
#if defined(_WIN32)
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec + time.tv_nsec * 1e-9;
#endif
}

static void require(bool ok) {
    if (!ok) exit(1);
}

static double median(double *times) {
    for (unsigned i = 1; i < 7; i++)
        for (unsigned j = i; j && times[j] < times[j - 1]; j--) {
            double value = times[j];
            times[j] = times[j - 1];
            times[j - 1] = value;
        }
    return times[3];
}

static uint32_t checksum(const void *data, size_t bytes) {
    const uint8_t *p = data;
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < bytes; i++) hash = (hash ^ p[i]) * UINT32_C(16777619);
    return hash;
}

static void pcm(size_t frames, uint32_t channels) {
    static const int32_t pairs[][2] = {{LND_FORMAT_U8, LND_FORMAT_S16}, {LND_FORMAT_S24, LND_FORMAT_S32}, {LND_FORMAT_S32, LND_FORMAT_S16},
                                     {LND_FORMAT_S16, LND_FORMAT_S16}};
    uint8_t input[8192], output[8192];
    for (size_t i = 0; i < sizeof input; i++) input[i] = (uint8_t)(i * 79 + 31);
    for (unsigned pair = 0; pair < sizeof pairs / sizeof *pairs; pair++) {
        LND_PCM src = {.data = input + 1, .frames = frames, .channels = channels, .format = pairs[pair][0]};
        LND_PCM dst = {.data = output + 1, .frames = frames, .channels = channels, .format = pairs[pair][1]};
        if (pair == 3) src.stride_bytes = dst.stride_bytes = channels * 2 + 3;
        double times[7];
        unsigned iterations = (unsigned)(2000000 / frames / channels);
        for (unsigned trial = 0; trial < 7; trial++) {
            memset(output, 0, sizeof output);
            double start = now();
            for (unsigned i = 0; i < iterations; i++) require(LND_PcmConvert(&dst, 0, &src, 0, frames) == LND_OK);
            times[trial] = (now() - start) * 1e9 / iterations;
        }
        printf("pcm_%d_%d,%zu,%u,%.2f,%08x\n", src.format, dst.format, frames, channels, median(times), checksum(output, sizeof output));
    }
}

static void gain(size_t frames, uint32_t channels, int32_t format) {
    require(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
    require(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, (uint64_t)format) == LND_OK);
    require(LND_LibraryInit() == LND_OK);
    int32_t input[1536];
    uint8_t output[6144];
    for (size_t i = 0; i < sizeof input / sizeof *input; i++) input[i] = (int32_t)(i * UINT32_C(1234567));
    LND_PCM src = {.data = input, .frames = frames, .channels = channels, .format = format};
    LND_SOURCE_CONFIG config = {.pcm = &src, .channels = channels, .sample_rate_hz = 48000};
    LND_SOURCE *source = LND_SourceCreate(&config);
    require(source != nullptr);
    LND_SOUND *sound = LND_SourceEnsureSound(source, nullptr);
    require(sound && LND_SoundSetGainQ16(sound, 49152) == LND_OK && LND_SoundSetLoop(sound, true) == LND_OK && LND_SoundPlay(sound) == LND_OK);
    LND_PCM dst = {.data = output, .frames = frames, .channels = channels, .format = format};
    double times[7];
    unsigned iterations = (unsigned)(2000000 / frames / channels);
    for (unsigned trial = 0; trial < 7; trial++) {
        double start = now();
        for (unsigned i = 0; i < iterations; i++) require(LND_SoundRenderPcm(sound, &dst, 0, frames) == (int64_t)frames);
        times[trial] = (now() - start) * 1e9 / iterations;
    }
    printf("gain_%d,%zu,%u,%.2f,%08x\n", format, frames, channels, median(times), checksum(output, frames * channels * LND_PcmGetSampleBytes(format)));
    LND_LibraryFree();
}

#if LND_MODULE_ANALYSIS
static void analysis(void) {
    float input[1024];
    for (unsigned i = 0; i < 1024; i++) input[i] = (float)((int)(i % 17) - 8) / 16;
    LND_PCM pcm = {.data = input, .frames = 1024, .channels = 1, .format = LND_FORMAT_F32};
    LND_FFT *fft = LND_FftCreate(1024, LND_WINDOW_HANN);
    require(fft != nullptr);
    LND_COMPLEX bins[513];
    LND_LEVELS levels;
    for (unsigned mode = 0; mode < 2; mode++) {
        double times[7];
        for (unsigned trial = 0; trial < 7; trial++) {
            double start = now();
            for (unsigned i = 0; i < 4000; i++)
                require((mode ? LND_FftExecute(fft, &pcm, 0, 0, bins, 513) : LND_PcmAnalyzeLevels(&pcm, 0, 1024, 0, &levels)) == LND_OK);
            times[trial] = (now() - start) * 1e9 / 4000;
        }
        printf("%s,1024,1,%.2f,%08x\n", mode ? "fft" : "levels", median(times),
               mode ? checksum(bins, sizeof bins) : checksum(&levels.peak, sizeof levels.peak));
    }
    LND_FftFree(fft);
}
#endif

#if LND_MODULE_DEMUX
static uint8_t *read_file(const char *directory, const char *name, size_t *bytes) {
    char path[1024];
    int length = snprintf(path, sizeof path, "%s/%s", directory, name);
    require(length > 0 && (size_t)length < sizeof path);
    FILE *file = fopen(path, "rb");
    require(file != nullptr && fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    require(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *data = malloc((size_t)size);
    require(data && fread(data, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    *bytes = (size_t)size;
    return data;
}

static void demux(const char *directory) {
    size_t init_bytes, bytes;
    uint8_t *init = read_file(directory, "init.mp4", &init_bytes);
    uint8_t *segment = read_file(directory, "seg0.m4s", &bytes);
    require(bytes <= SIZE_MAX / 256);
    uint8_t *joined = malloc(bytes * 256);
    require(joined != nullptr);
    for (unsigned i = 0; i < 256; i++) memcpy(joined + i * bytes, segment, bytes);
    for (unsigned fragments = 1; fragments <= 256; fragments *= 16) {
        LND_DEMUX *d = LND_DemuxCreate(nullptr);
        require(d && LND_DemuxSetInit(d, init, init_bytes) == LND_OK);
        double times[7];
        for (unsigned trial = 0; trial < 7; trial++) {
            double start = now();
            for (unsigned i = 0; i < 256 / fragments; i++) require(LND_DemuxBegin(d, joined, bytes * fragments) == LND_OK);
            times[trial] = (now() - start) * 1e9 / (256 / fragments);
        }
        LND_DEMUX_PACKET packet;
        uint32_t hash = 0, count = 0;
        int32_t result;
        while ((result = LND_DemuxRead(d, &packet)) == LND_OK) {
            hash = hash * 31 + checksum(packet.data, packet.bytes);
            count++;
        }
        require(result == LND_DEMUX_END && count == fragments * 26);
        printf("demux,%u,1,%.2f,%08x\n", fragments, median(times), hash);
        LND_DemuxFree(d);
    }
    free(joined);
    free(segment);
    free(init);
}
#endif

#if LND_MODULE_METADATA
static void metadata(void) {
    for (unsigned count = 64; count <= 4096; count *= 8) {
        double times[7];
        uint32_t hash = 0;
        for (unsigned trial = 0; trial < 7; trial++) {
            LND_METADATA *m = LND_MetadataCreate(nullptr);
            require(m != nullptr);
            for (unsigned i = 0; i < count; i++) {
                LND_METADATA_FIELD field = {.key = i & 1 ? "KEEP" : "DROP", .value = "text"};
                require(LND_MetadataAddField(m, &field) == LND_OK);
            }
            double start = now();
            require(LND_MetadataRemove(m, "DROP") == LND_OK);
            times[trial] = (now() - start) * 1e9;
            require(LND_MetadataGetFieldCount(m) == count / 2);
            hash = checksum(LND_MetadataGetField(m, 0)->key, 4);
            LND_MetadataFree(m);
        }
        printf("metadata_remove,%u,1,%.2f,%08x\n", count, median(times), hash);
    }
}
#endif

int main(int argc, char **argv) {
    puts("operation,frames,channels,median_ns,checksum");
#if LND_MODULE_DEMUX
    if (argc == 2) {
        demux(argv[1]);
        return 0;
    }
#else
    (void)argc;
    (void)argv;
#endif
    const size_t blocks[] = {16, 256};
    const uint32_t channels[] = {1, 2, 6};
    for (unsigned b = 0; b < 2; b++)
        for (unsigned c = 0; c < 3; c++) {
            pcm(blocks[b], channels[c]);
            gain(blocks[b], channels[c], LND_FORMAT_S16);
            gain(blocks[b], channels[c], LND_FORMAT_S32);
        }
#if LND_MODULE_ANALYSIS
    analysis();
#endif
#if LND_MODULE_METADATA
    metadata();
#endif
    return 0;
}
