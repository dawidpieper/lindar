#pragma once

#include "format.h"

LND_INLINE int64_t lnd_pcm_integer_load(const uint8_t *p, int32_t format) {
    if (format == LND_FORMAT_U8) return ((int32_t)*p - 128) * INT32_C(16777216);
    uint32_t value = (uint32_t)p[0] | (uint32_t)p[1] << 8;
    if (format >= LND_FORMAT_S24) value |= (uint32_t)p[2] << 16;
    if (format == LND_FORMAT_S32) value |= (uint32_t)p[3] << 24;
    value <<= 32 - lnd_format_bytes(format) * 8;
    return (int64_t)(value ^ UINT32_C(0x80000000)) - INT64_C(2147483648);
}

LND_INLINE void lnd_pcm_integer_store(uint8_t *p, int32_t format, int64_t value) {
    uint32_t bits = (uint32_t)lnd_format_bytes(format) * 8, shift = 32 - bits;
    value = LND_CLAMP(value, INT32_MIN, INT32_MAX);
    uint32_t magnitude = value < 0 ? UINT32_C(0) - (uint32_t)value : (uint32_t)value;
    magnitude = (magnitude + ((UINT32_C(1) << shift) >> 1)) >> shift;
    uint32_t sample = value < 0 ? UINT32_C(0) - magnitude : LND_MIN(magnitude, (UINT32_C(1) << (bits - 1)) - 1);
    if (format == LND_FORMAT_U8) {
        *p = (uint8_t)(sample + 128);
        return;
    }
    p[0] = (uint8_t)sample;
    p[1] = (uint8_t)(sample >> 8);
    if (bits >= 24) p[2] = (uint8_t)(sample >> 16);
    if (bits == 32) p[3] = (uint8_t)(sample >> 24);
}
