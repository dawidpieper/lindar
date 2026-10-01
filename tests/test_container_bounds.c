#include "formats/mp4/read.h"
#include "formats/riff/read.h"
#include "lnd_modules.h"
#if LND_MODULE_AAC_ENCODER
#include "formats/aac/mp4.h"
#endif
#include <stdio.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%d: %s\n", __LINE__, #x);                                                                                                                  \
        }                                                                                                                                                      \
    } while (0)
static void put32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
}
static void put64(uint8_t *p, uint64_t n) {
    put32(p, (uint32_t)(n >> 32));
    put32(p + 4, (uint32_t)n);
}
typedef struct locations {
    uint64_t offsets[4];
    uint32_t sizes[4];
} locations;
static void location(void *user, uint32_t i, uint64_t offset, uint32_t bytes) {
    locations *out = user;
    if (i < 4) {
        out->offsets[i] = offset;
        out->sizes[i] = bytes;
    }
}
static void mp4(void) {
    uint8_t data[32] = {0, 0, 0, 1, 'm', 'd', 'a', 't'};
    put64(data + 8, 24);
    lnd_mp4_header header;
    for (size_t n = 0; n < 16; n++) CHECK(!lnd_mp4_box_bounds(data, n, 24, &header));
    CHECK(lnd_mp4_box_bounds(data, 16, 24, &header) && header.size == 24 && header.bytes == 16);
    CHECK(!lnd_mp4_box_bounds(data, 16, 23, &header));
    put64(data + 8, UINT64_MAX);
    CHECK(!lnd_mp4_box_bounds(data, 16, UINT64_MAX - 1, &header));
    put32(data, 0);
    CHECK(lnd_mp4_box_bounds(data, 8, 32, &header) && header.size == 32 && header.bytes == 8);
    put32(data, 7);
    CHECK(!lnd_mp4_box_bounds(data, 8, 32, &header));
    uint8_t config[] = {3, 22, 0, 1, 0, 4, 17, 0x40, 0x15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 2, 0x12, 0x10};
    lnd_mp4_config parsed = {0};
    CHECK(lnd_mp4_descriptors(config, sizeof config, 0, &parsed) && parsed.object_type == 0x40 && parsed.size == 2);
    for (size_t n = 1; n < sizeof config; n++) CHECK(!lnd_mp4_descriptors(config, n, 0, &parsed));
    uint8_t overflow[] = {5, 0xff, 0xff, 0xff, 0xff, 0};
    CHECK(!lnd_mp4_descriptors(overflow, sizeof overflow, 0, &parsed));
    CHECK(!lnd_mp4_descriptors(config, sizeof config, 9, &parsed));
    uint8_t chunks[16], runs[24] = {0}, sizes[12];
    put64(chunks, UINT64_C(0x100000010));
    put64(chunks + 8, 900);
    put32(runs, 1);
    put32(runs + 4, 2);
    put32(runs + 8, 1);
    put32(runs + 12, 2);
    put32(runs + 16, 1);
    put32(runs + 20, 1);
    put32(sizes, 3);
    put32(sizes + 4, 5);
    put32(sizes + 8, 7);
    lnd_mp4_table ct, rt, st;
    CHECK(lnd_mp4_table_view(&ct, chunks, 16, 2, 8));
    CHECK(lnd_mp4_table_view(&rt, runs, 24, 2, 12));
    CHECK(lnd_mp4_table_view(&st, sizes, 12, 3, 4));
    CHECK(!lnd_mp4_table_view(&st, sizes, 11, 3, 4));
    CHECK(!lnd_mp4_table_view(&st, sizes, 12, UINT32_MAX, 4));
    locations out = {0};
    CHECK(lnd_mp4_locations(&ct, &rt, &st, 0, 3, 100, location, &out) == LND_OK);
    CHECK(out.offsets[0] == UINT64_C(0x100000010) && out.offsets[1] == UINT64_C(0x100000013) && out.offsets[2] == 900 && out.sizes[2] == 7);
    CHECK(lnd_mp4_locations(&ct, &rt, &st, 0, 3, 6, location, &out) == LND_ERR_FORMAT);
    put64(chunks, UINT64_MAX - 1);
    CHECK(lnd_mp4_locations(&ct, &rt, &st, 0, 3, 100, location, &out) == LND_ERR_FORMAT);
    put32(runs + 12, 1);
    CHECK(lnd_mp4_locations(&ct, &rt, &st, 0, 3, 100, location, &out) == LND_ERR_UNSUPPORTED);
}
static void riff(void) {
    uint8_t header[12] = {'R', 'I', 'F', 'F', 13, 0, 0, 0, 'W', 'A', 'V', 'E'};
    uint8_t body[8] = {'d', 'a', 't', 'a', 1, 0, 0, 0};
    lnd_riff r;
    lnd_riff_chunk chunk;
    uint64_t at = 12;
    CHECK(lnd_riff_open(header, 21, 0, &r) == LND_OK);
    CHECK(lnd_riff_next(&r, body, &at, &chunk) == LND_ERR_FORMAT && at == 12);
    CHECK(lnd_riff_open(header, 21, LND_RIFF_ALLOW_MISSING_PAD, &r) == LND_OK);
    CHECK(lnd_riff_next(&r, body, &at, &chunk) == LND_OK && at == 21 && chunk.size == 1);
    header[4] = 0;
    CHECK(lnd_riff_open(header, 21, 0, &r) == LND_ERR_FORMAT);
    CHECK(lnd_riff_open(header, 21, LND_RIFF_PHYSICAL | LND_RIFF_CLAMP_DATA | LND_RIFF_ALLOW_MISSING_PAD, &r) == LND_OK);
    body[4] = 100;
    at = 12;
    CHECK(lnd_riff_next(&r, body, &at, &chunk) == LND_OK && chunk.size == 1);
    memcpy(body, "JUNK", 4);
    at = 12;
    CHECK(lnd_riff_next(&r, body, &at, &chunk) == LND_ERR_FORMAT);
    memcpy(header, "RF64", 4);
    memset(header + 4, 255, 4);
    CHECK(lnd_riff_open(header, 60, 0, &r) == LND_OK);
    at = 12;
    CHECK(lnd_riff_next(&r, body, &at, &chunk) == LND_ERR_FORMAT);
    uint8_t ds[28] = {52};
    ds[8] = 4;
    CHECK(lnd_riff_ds64(&r, ds, 27, 28) == LND_ERR_FORMAT);
    CHECK(lnd_riff_ds64(&r, ds, 28, 28) == LND_OK);
    CHECK(lnd_riff_ds64(&r, ds, 28, 28) == LND_ERR_FORMAT);
    memcpy(body, "data", 4);
    memset(body + 4, 255, 4);
    at = 48;
    CHECK(lnd_riff_next(&r, body, &at, &chunk) == LND_OK && at == 60 && chunk.size == 4);
    CHECK(lnd_riff_open(header, 60, 0, &r) == LND_OK);
    ds[0] = 4;
    CHECK(lnd_riff_ds64(&r, ds, 28, 28) == LND_ERR_FORMAT);
    ds[0] = 52;
    ds[24] = 1;
    CHECK(lnd_riff_ds64(&r, ds, 28, 28) == LND_ERR_UNSUPPORTED);
}
int main(void) {
    mp4();
    riff();
#if LND_MODULE_AAC_ENCODER
    uint32_t limit = (uint32_t)LND_MIN((uint64_t)UINT32_MAX, SIZE_MAX / sizeof(uint32_t));
    lnd_mp4_mux mux = {.count = limit, .cap = limit};
    CHECK(lnd_mp4_sample(&mux, nullptr, 0) == LND_ERR_OUT_OF_MEMORY);
    CHECK(mux.count == limit && mux.cap == limit && !mux.sizes);
    mux.count = mux.cap = UINT32_MAX;
    CHECK(lnd_mp4_sample(&mux, nullptr, 0) == LND_ERR_OUT_OF_MEMORY);
#endif
    printf("%u checks, %u failures\n", checks, failures);
    return failures != 0;
}
