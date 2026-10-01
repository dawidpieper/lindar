#include "lindar.h"
#include "pcm/audio/source.h"
#include "src/thread.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)

typedef struct feed {
    lnd_source base;
    unsigned id;
} feed;

static lnd_atomic_u32 enabled, completed;
static unsigned order[128], calls;
static uint64_t first_time[8];

static uint64_t read_feed(lnd_source *base, float *pcm, uint64_t frames) {
    feed *s = (feed *)base;
    if (!lnd_load(&enabled)) return 0;
    uint64_t at = lnd_load(&base->pos);
    if (at >= 4099) {
        lnd_store(&base->status, LND_SOURCE_EOF);
        return 0;
    }
    uint64_t count = LND_MIN(frames, 4099 - at);
    if (calls < LND_COUNTOF(order)) order[calls] = s->id;
    calls++;
    uint64_t end = lnd_time_ns() + 100000;
    while (lnd_time_ns() < end) {}
    for (uint64_t f = 0; f < count; f++) pcm[f] = (float)(at + f + 1) / 8192;
    if (!first_time[s->id]) first_time[s->id] = lnd_time_ns();
    lnd_store(&base->pos, at + count);
    lnd_add(&completed, 1);
    return count;
}

static int32_t seek_feed(lnd_source *base, uint64_t at) {
    lnd_store(&base->pos, at);
    return LND_OK;
}

static const lnd_source_vt vt = {.read = read_feed, .seek = seek_feed};

static void verify(lnd_source *s, uint64_t at, unsigned count, bool sync) {
    float data[701];
    LND_PCM pcm = {.data = data, .frames = LND_COUNTOF(data), .channels = 1, .format = LND_FORMAT_F32};
    unsigned done = 0;
    uint64_t deadline = lnd_time_ns() + UINT64_C(5000000000);
    while (done < count && lnd_time_ns() < deadline) {
        size_t want = LND_MIN(count - done, LND_COUNTOF(data));
        int64_t got = lnd_source_read_pcm(s, &pcm, 0, want, sync);
        CHECK(got >= 0 && (uint64_t)got <= want);
        for (int64_t f = 0; f < got; f++) CHECK(data[f] == (float)(at + done + f + 1) / 8192);
        done += (unsigned)got;
        if (!got) lnd_sleep_ms(1);
    }
    CHECK(done == count && lnd_load(&s->pos) == at + count);
}

int main(void) {
    for (unsigned run = 0; run < 4; run++) {
        CHECK(LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED) == LND_OK);
        CHECK(LND_ConfigSet(LND_CFG_INTERNAL_FORMAT, LND_FORMAT_F32) == LND_OK);
        CHECK(LND_ConfigSet(LND_CFG_INTERNAL_LAYOUT, run % 2) == LND_OK);
        CHECK(LND_LibraryInit() == LND_OK);
        lnd_store(&enabled, 0);
        lnd_store(&completed, 0);
        calls = 0;
        memset(first_time, 0, sizeof first_time);
        feed feeds[8] = {0};
        lnd_source *streams[8];
        for (unsigned i = 0; i < 8; i++) {
            feeds[i] = (feed){.base = {.vt = &vt, .channels = 1, .sample_rate_hz = 48000, .live = true}, .id = i};
            streams[i] = lnd_stream_source_create(&feeds[i].base, false, 32, run < 2 ? 4 : 16);
            CHECK(streams[i] && (uintptr_t)streams[i] % LND_CACHE_LINE == 0);
        }
        lnd_store(&enabled, 1);
        uint64_t deadline = lnd_time_ns() + UINT64_C(5000000000);
        while (lnd_load(&completed) < 8 && lnd_time_ns() < deadline) lnd_sleep_ms(1);
        CHECK(lnd_load(&completed) >= 8);
        unsigned mask = 0;
        for (unsigned i = 0; i < 8; i++) mask |= 1u << order[i];
        CHECK(mask == 255);
        for (unsigned i = 0; i < 8; i++) {
            verify(streams[i], 0, 17, false);
            verify(streams[i], 17, 1601, i % 2 == 0);
            CHECK(lnd_source_seek(streams[i], 271) == LND_OK);
            verify(streams[i], 271, 3828, i % 2 == 0);
            float data;
            for (unsigned retry = 0; retry < 100 && lnd_source_status(streams[i]) != LND_SOURCE_EOF; retry++) {
                CHECK(lnd_source_read(streams[i], &data, 1) == 0);
                lnd_sleep_ms(1);
            }
            CHECK(lnd_source_status(streams[i]) == LND_SOURCE_EOF);
            lnd_source_free(streams[i]);
        }
        LND_LibraryFree();
    }
    puts("stream rounds, partial reads, oversized reads, seek, EOF and release passed");
    return 0;
}
