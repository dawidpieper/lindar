#include "read.h"

#include <string.h>

static uint32_t lnd_riff_u32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t lnd_riff_u64(const uint8_t *p) { return lnd_riff_u32(p) | (uint64_t)lnd_riff_u32(p + 4) << 32; }

static int32_t lnd_riff_bound(lnd_riff *riff, uint64_t size) {
    if (!(riff->policy & LND_RIFF_PHYSICAL) && (size < 12 || (!(riff->policy & LND_RIFF_CLAMP_CONTAINER) && size > riff->physical))) return LND_ERR_FORMAT;
    riff->end = riff->policy & LND_RIFF_PHYSICAL ? riff->physical : LND_MIN(size, riff->physical);
    return LND_OK;
}

int32_t lnd_riff_open(const uint8_t header[12], uint64_t physical, uint32_t policy, lnd_riff *riff) {
    if (physical < 12 || memcmp(header + 8, "WAVE", 4)) return LND_ERR_FORMAT;
    bool rf64 = !memcmp(header, "RF64", 4) || !memcmp(header, "BW64", 4);
    if (!rf64 && memcmp(header, "RIFF", 4)) return LND_ERR_UNSUPPORTED;
    *riff = (lnd_riff){.end = physical, .physical = physical, .data_size = UINT64_MAX, .policy = policy, .rf64 = rf64};
    uint32_t size = lnd_riff_u32(header + 4);
    if (rf64) return size == UINT32_MAX ? LND_OK : LND_ERR_FORMAT;
    return lnd_riff_bound(riff, (uint64_t)size + 8);
}

int32_t lnd_riff_ds64(lnd_riff *riff, const uint8_t *data, size_t bytes, uint64_t chunk_size) {
    if (!riff->rf64 || riff->ds64 || chunk_size < 28 || bytes < 28) return LND_ERR_FORMAT;
    uint64_t size = lnd_riff_u64(data);
    if (size > UINT64_MAX - 8) return LND_ERR_FORMAT;
    if (lnd_riff_u32(data + 24)) return LND_ERR_UNSUPPORTED;
    int32_t result = lnd_riff_bound(riff, size + 8);
    if (result != LND_OK) return result;
    if (!(riff->policy & LND_RIFF_PHYSICAL) && (riff->end < 20 || chunk_size > riff->end - 20 || ((chunk_size & 1) && chunk_size == riff->end - 20)))
        return LND_ERR_FORMAT;
    riff->data_size = lnd_riff_u64(data + 8);
    riff->ds64 = true;
    return LND_OK;
}

int32_t lnd_riff_next(const lnd_riff *riff, const uint8_t header[8], uint64_t *position, lnd_riff_chunk *chunk) {
    uint64_t pos = *position;
    if (pos > riff->end || riff->end - pos < 8) return LND_ERR_FORMAT;
    bool data = !memcmp(header, "data", 4);
    if (riff->rf64 && !riff->ds64 && (pos != 12 || memcmp(header, "ds64", 4))) return LND_ERR_FORMAT;
    uint64_t size = lnd_riff_u32(header + 4), body = pos + 8;
    if (riff->rf64 && data && size == UINT32_MAX) size = riff->data_size;
    if (size > riff->end - body) {
        if (!data || !(riff->policy & LND_RIFF_CLAMP_DATA)) return LND_ERR_FORMAT;
        size = riff->end - body;
    }
    uint64_t end = body + size;
    if (size & 1) {
        if (end == riff->end) {
            if (!(riff->policy & LND_RIFF_ALLOW_MISSING_PAD)) return LND_ERR_FORMAT;
        } else end++;
    }
    *chunk = (lnd_riff_chunk){.body = body, .size = size, .next = end};
    *position = end;
    return LND_OK;
}
