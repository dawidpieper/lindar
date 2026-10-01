#pragma once

#include "platform.h"
#include "lindar.h"

LND_INLINE uint32_t lnd_format_bytes(int32_t format) {
    static const uint8_t table[] = {0, 1, 2, 3, 4, 4, 8};
    return format > LND_FORMAT_NONE && format <= LND_FORMAT_F64 ? table[format] : 0;
}

LND_INLINE bool lnd_format_valid(int32_t format) { return format >= LND_FORMAT_U8 && format <= LND_FORMAT_F64; }

LND_INLINE bool lnd_format_is_float(int32_t format) { return format == LND_FORMAT_F32 || format == LND_FORMAT_F64; }

LND_INLINE uint32_t lnd_format_bits(int32_t format) { return lnd_format_bytes(format) * 8; }

const char *lnd_format_name(int32_t format);
