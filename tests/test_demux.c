#include "codec_test.h"
#include "lindar_demux.h"

static void ts_chunks(size_t chunk) {
    size_t original_bytes;
    uint8_t *original = read_file("audiosamples/tone.aac", &original_bytes);
    uint8_t *joined = malloc(original_bytes);
    size_t output = 0;
    LND_DEMUX *demux = LND_DemuxCreate(nullptr);
    CHECK(demux != nullptr);
    for (unsigned segment = 0; segment < 4; segment++) {
        char path[128];
        snprintf(path, sizeof path, "audiosamples/hls-ts/seg%u.ts", segment);
        size_t bytes;
        uint8_t *data = read_file(path, &bytes);
        for (size_t at = 0; at < bytes;) {
            size_t take = bytes - at < chunk ? bytes - at : chunk;
            CHECK(LND_DemuxBegin(demux, data + at, take) == LND_OK);
            LND_DEMUX_PACKET packet;
            int32_t r;
            while ((r = LND_DemuxRead(demux, &packet)) == LND_OK) {
                CHECK(!strcmp(packet.codec, "aac"));
                CHECK(packet.bytes <= original_bytes - output);
                if (packet.bytes > original_bytes - output) break;
                memcpy(joined + output, packet.data, packet.bytes);
                output += packet.bytes;
            }
            CHECK(r == LND_DEMUX_END);
            at += take;
        }
        free(data);
    }
    CHECK(LND_DemuxEnd(demux) == LND_OK);
    LND_DEMUX_PACKET packet;
    CHECK(LND_DemuxRead(demux, &packet) == LND_DEMUX_END);
    CHECK(output == original_bytes && !memcmp(joined, original, output));
    LND_DemuxFree(demux);
    free(joined);
    free(original);
}

static void mp4(void) {
    size_t init_bytes, bytes;
    uint8_t *init = read_file("audiosamples/hls-mp4/init.mp4", &init_bytes);
    uint8_t *segment = read_file("audiosamples/hls-mp4/seg0.m4s", &bytes);
    LND_DEMUX *demux = LND_DemuxCreate(nullptr);
    CHECK(LND_DemuxSetInit(demux, init, init_bytes) == LND_OK);
    CHECK(LND_DemuxBegin(demux, segment, bytes) == LND_OK);
    unsigned count = 0;
    int64_t previous = -1;
    LND_DEMUX_PACKET packet;
    while (LND_DemuxRead(demux, &packet) == LND_OK) {
        CHECK(packet.time_us > previous && packet.duration_ticks == 1024 && packet.timescale == 44100);
        CHECK(!strcmp(packet.codec, "aac") && packet.config_bytes >= 2 && packet.bytes > 0);
        previous = packet.time_us;
        count++;
    }
    CHECK(count == 26);
    LND_DemuxFree(demux);
    for (size_t i = 0; i < init_bytes; i++) {
        demux = LND_DemuxCreate(nullptr);
        init[i] ^= 0xff;
        int32_t r = LND_DemuxSetInit(demux, init, init_bytes);
        if (r == LND_OK) {
            r = LND_DemuxBegin(demux, segment, bytes);
            if (r == LND_OK) {
                unsigned n = 0;
                while (n++ < 100 && LND_DemuxRead(demux, &packet) == LND_OK) {
                }
                CHECK(n <= 100);
            }
        }
        init[i] ^= 0xff;
        LND_DemuxFree(demux);
    }
    free(init);
    free(segment);
}

static void mp4_fragments(void) {
    size_t init_bytes, bytes;
    uint8_t *init = read_file("audiosamples/hls-mp4/init.mp4", &init_bytes);
    uint8_t *segment = read_file("audiosamples/hls-mp4/seg0.m4s", &bytes);
    uint8_t *joined = malloc(bytes * 64);
    CHECK(joined != nullptr);
    if (!joined) {
        free(init);
        free(segment);
        return;
    }
    for (unsigned i = 0; i < 64; i++) memcpy(joined + i * bytes, segment, bytes);
    LND_DEMUX *d = LND_DemuxCreate(nullptr);
    CHECK(d && LND_DemuxSetInit(d, init, init_bytes) == LND_OK);
    unsigned baseline = allocations;
    for (unsigned fragments = 64; fragments; fragments /= 4) {
        CHECK(LND_DemuxBegin(d, joined, bytes * fragments) == LND_OK);
        LND_DEMUX_PACKET packet;
        unsigned count = 0;
        int32_t result;
        while ((result = LND_DemuxRead(d, &packet)) == LND_OK) {
            const uint8_t *data = packet.data;
            CHECK(packet.bytes && data >= joined && data + packet.bytes <= joined + bytes * fragments);
            if (count >= 26) CHECK(!memcmp(data, data - (count / 26) * bytes, packet.bytes));
            count++;
        }
        CHECK(result == LND_DEMUX_END && count == fragments * 26);
        LND_DemuxReset(d);
    }
    CHECK(allocations - baseline <= 8);
    LND_DemuxFree(d);
    baseline = live_allocations;
    for (int failure = 0; failure < 8; failure++) {
        d = LND_DemuxCreate(nullptr);
        CHECK(d && LND_DemuxSetInit(d, init, init_bytes) == LND_OK);
        fail_allocation = failure;
        int32_t result = LND_DemuxBegin(d, joined, bytes * 64);
        CHECK(result == LND_OK || result == LND_ERR_OUT_OF_MEMORY);
        fail_allocation = -1;
        CHECK(LND_DemuxBegin(d, joined, bytes * 64) == LND_OK);
        LND_DemuxFree(d);
        CHECK(live_allocations == baseline);
    }
    free(joined);
    free(segment);
    free(init);
}

static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void mp4_ranges(void) {
    size_t init_bytes;
    uint8_t *init = read_file("audiosamples/hls-mp4/init.mp4", &init_bytes);
    LND_DEMUX *d = LND_DemuxCreate(nullptr);
    CHECK(d && LND_DemuxSetInit(d, init, init_bytes) == LND_OK);
    uint8_t data[140] = {0};
    put32(data, 12);
    memcpy(data + 4, "mdatabcd", 8);
    put32(data + 12, 116);
    memcpy(data + 16, "moof", 4);
    put32(data + 20, 108);
    memcpy(data + 24, "traf", 4);
    put32(data + 28, 16);
    memcpy(data + 32, "tfhd", 4);
    put32(data + 36, 0x20000);
    put32(data + 40, 1);
    const uint32_t offsets[] = {136, 8, 138};
    for (unsigned i = 0; i < 3; i++) {
        uint8_t *run = data + 44 + i * 28;
        put32(run, 28);
        memcpy(run + 4, "trun", 4);
        put32(run + 8, 0x301);
        put32(run + 12, 1);
        put32(run + 16, offsets[i] - 12);
        put32(run + 20, 1024);
        put32(run + 24, 2);
    }
    put32(data + 128, 12);
    memcpy(data + 132, "mdatwxyz", 8);
    CHECK(LND_DemuxBegin(d, data, sizeof data) == LND_OK);
    LND_DEMUX_PACKET packet;
    for (unsigned i = 0; i < 3; i++) {
        CHECK(LND_DemuxRead(d, &packet) == LND_OK);
        CHECK(packet.data == data + offsets[i] && packet.bytes == 2);
    }
    CHECK(LND_DemuxRead(d, &packet) == LND_DEMUX_END);
    const uint32_t invalid[] = {139, 140, 128, 7};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        put32(data + 88, invalid[i] - 12);
        CHECK(LND_DemuxBegin(d, data, sizeof data) == LND_ERR_FORMAT);
    }
    put32(data + 88, offsets[1] - 12);
    CHECK(LND_DemuxBegin(d, data, sizeof data) == LND_OK);
    memcpy(data + 132, "free", 4);
    CHECK(LND_DemuxBegin(d, data, sizeof data) == LND_ERR_FORMAT);
    memcpy(data + 132, "mdat", 4);
    CHECK(LND_DemuxBegin(d, data, sizeof data) == LND_OK);
    LND_DemuxFree(d);
    free(init);
}

static void mp4_index(void) {
    const char *paths[] = {"audiosamples/tone.m4a", "audiosamples/tone.http-alac.m4a"};
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
        size_t bytes;
        uint8_t *data = read_file(paths[i], &bytes);
        LND_DEMUX *demux = LND_DemuxCreate(nullptr);
        CHECK(LND_DemuxSetInit(demux, data, bytes) == LND_OK);
        uint64_t count = LND_DemuxGetSampleCount(demux);
        CHECK(count > 10);
        int64_t previous = INT64_MIN;
        for (uint64_t j = 0; j < count; j++) {
            LND_DEMUX_SAMPLE sample;
            int32_t r = LND_DemuxGetSample(demux, j, &sample);
            if (r == LND_DEMUX_END) break;
            CHECK(r == LND_OK);
            CHECK(sample.offset_bytes < bytes && sample.packet.bytes <= bytes - sample.offset_bytes);
            CHECK(sample.packet.data == nullptr && sample.packet.config_bytes >= 2);
            CHECK(sample.packet.time_us > previous);
            previous = sample.packet.time_us;
        }
        LND_DemuxFree(demux);
        free(data);
    }
}

int main(void) {
    test_init(LND_LAYOUT_INTERLEAVED);
    unsigned before = live_allocations;
    ts_chunks(188);
    ts_chunks(188 * 7);
    ts_chunks(188 * 10000);
    mp4();
    mp4_fragments();
    mp4_ranges();
    mp4_index();
    CHECK(live_allocations == before);
    LND_LibraryFree();
    CHECK(!live_allocations);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
