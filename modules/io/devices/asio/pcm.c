#include "pcm.h"

#include <math.h>
#include <string.h>

static uint64_t lnd_asio_load(const uint8_t *p, unsigned bytes, bool big) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++)
        value |= (uint64_t)p[i] << (8 * (big ? bytes - i - 1 : i));
    return value;
}

static void lnd_asio_store(uint8_t *p, uint64_t value, unsigned bytes, bool big) {
    for (unsigned i = 0; i < bytes; i++)
        p[i] = (uint8_t)(value >> (8 * (big ? bytes - i - 1 : i)));
}

#define LND_ASIO_INT(NAME, BYTES, BITS, BIG)                                                                                                                   \
    static void NAME##_read(const void *input, float *output, uint32_t frames, uint32_t stride) {                                                              \
        const uint8_t *p = input;                                                                                                                              \
        for (uint32_t i = 0; i < frames; i++, p += BYTES, output += stride) {                                                                                  \
            uint32_t bits = (uint32_t)lnd_asio_load(p, BYTES, BIG);                                                                                            \
            int32_t value = (int32_t)(bits << (32 - BITS)) >> (32 - BITS);                                                                                     \
            *output = (float)((double)value / (double)(UINT64_C(1) << (BITS - 1)));                                                                            \
        }                                                                                                                                                      \
    }                                                                                                                                                          \
    static void NAME##_write(const float *input, void *output, uint32_t frames, uint32_t stride) {                                                             \
        uint8_t *p = output;                                                                                                                                   \
        const double scale = (double)(UINT64_C(1) << (BITS - 1));                                                                                              \
        for (uint32_t i = 0; i < frames; i++, p += BYTES, input += stride) {                                                                                   \
            double x = *input;                                                                                                                                 \
            int64_t value = isnan(x) ? 0 : x <= -1.0 ? -(int64_t)scale : x >= 1.0 ? (int64_t)scale - 1 : (int64_t)llrint(x * scale);                           \
            if (value >= (int64_t)scale)                                                                                                                       \
                value = (int64_t)scale - 1;                                                                                                                    \
            lnd_asio_store(p, (uint32_t)value & ((UINT64_C(1) << BITS) - 1), BYTES, BIG);                                                                      \
        }                                                                                                                                                      \
    }

#define LND_ASIO_FLOAT(NAME, TYPE, UINT, BIG)                                                                                                                  \
    static void NAME##_read(const void *input, float *output, uint32_t frames, uint32_t stride) {                                                              \
        const uint8_t *p = input;                                                                                                                              \
        for (uint32_t i = 0; i < frames; i++, p += sizeof(TYPE), output += stride) {                                                                           \
            UINT bits = (UINT)lnd_asio_load(p, sizeof(TYPE), BIG);                                                                                             \
            TYPE value;                                                                                                                                        \
            memcpy(&value, &bits, sizeof value);                                                                                                               \
            *output = (float)value;                                                                                                                            \
        }                                                                                                                                                      \
    }                                                                                                                                                          \
    static void NAME##_write(const float *input, void *output, uint32_t frames, uint32_t stride) {                                                             \
        uint8_t *p = output;                                                                                                                                   \
        for (uint32_t i = 0; i < frames; i++, p += sizeof(TYPE), input += stride) {                                                                            \
            TYPE value = (TYPE) * input;                                                                                                                       \
            UINT bits;                                                                                                                                         \
            memcpy(&bits, &value, sizeof bits);                                                                                                                \
            lnd_asio_store(p, bits, sizeof(TYPE), BIG);                                                                                                        \
        }                                                                                                                                                      \
    }

#define LND_ASIO_ENDIAN(SUFFIX, BIG)                                                                                                                           \
    LND_ASIO_INT(lnd_asio_s16_##SUFFIX, 2, 16, BIG)                                                                                                            \
    LND_ASIO_INT(lnd_asio_s24_##SUFFIX, 3, 24, BIG)                                                                                                            \
    LND_ASIO_INT(lnd_asio_s32_##SUFFIX, 4, 32, BIG)                                                                                                            \
    LND_ASIO_INT(lnd_asio_s32_16_##SUFFIX, 4, 16, BIG)                                                                                                         \
    LND_ASIO_INT(lnd_asio_s32_18_##SUFFIX, 4, 18, BIG)                                                                                                         \
    LND_ASIO_INT(lnd_asio_s32_20_##SUFFIX, 4, 20, BIG)                                                                                                         \
    LND_ASIO_INT(lnd_asio_s32_24_##SUFFIX, 4, 24, BIG)                                                                                                         \
    LND_ASIO_FLOAT(lnd_asio_f32_##SUFFIX, float, uint32_t, BIG)                                                                                                \
    LND_ASIO_FLOAT(lnd_asio_f64_##SUFFIX, double, uint64_t, BIG)

LND_ASIO_ENDIAN(le, false)
LND_ASIO_ENDIAN(be, true)

#define LND_ASIO_PCM(NAME, BYTES, BITS, FLOAT, BIG) {BYTES, BITS, FLOAT, BIG, NAME##_read, NAME##_write}
#define LND_ASIO_FORMATS(SUFFIX, BIG)                                                                                                                          \
    LND_ASIO_PCM(lnd_asio_s16_##SUFFIX, 2, 16, false, BIG), LND_ASIO_PCM(lnd_asio_s24_##SUFFIX, 3, 24, false, BIG),                                            \
        LND_ASIO_PCM(lnd_asio_s32_##SUFFIX, 4, 32, false, BIG), LND_ASIO_PCM(lnd_asio_f32_##SUFFIX, 4, 32, true, BIG),                                         \
        LND_ASIO_PCM(lnd_asio_f64_##SUFFIX, 8, 64, true, BIG), {0}, {0}, {0}, LND_ASIO_PCM(lnd_asio_s32_16_##SUFFIX, 4, 16, false, BIG),                       \
        LND_ASIO_PCM(lnd_asio_s32_18_##SUFFIX, 4, 18, false, BIG), LND_ASIO_PCM(lnd_asio_s32_20_##SUFFIX, 4, 20, false, BIG),                                  \
        LND_ASIO_PCM(lnd_asio_s32_24_##SUFFIX, 4, 24, false, BIG), {0}, {0}, {0}, {0}

bool lnd_asio_pcm_get(int32_t type, lnd_asio_pcm *pcm) {
    static const lnd_asio_pcm formats[32] = {LND_ASIO_FORMATS(be, true), LND_ASIO_FORMATS(le, false)};
    if (!pcm || type < 0 || type >= 32 || !formats[type].bytes)
        return false;
    *pcm = formats[type];
    return true;
}

int32_t lnd_asio_buffer_size(uint32_t requested, int32_t minimum, int32_t maximum, int32_t preferred, int32_t granularity, uint32_t *frames) {
    if (!frames || minimum <= 0 || maximum < minimum || maximum > (1 << 20) || preferred < minimum || preferred > maximum || granularity < -1)
        return LND_ERR_FORMAT;
    uint32_t wanted = requested ? requested : (uint32_t)preferred;
    if (wanted < (uint32_t)minimum || wanted > (uint32_t)maximum)
        return LND_ERR_INVALID_ARG;
    if (granularity == -1 && (wanted & (wanted - 1)))
        return LND_ERR_INVALID_ARG;
    if (granularity > 0 && (wanted - (uint32_t)minimum) % (uint32_t)granularity)
        return LND_ERR_INVALID_ARG;
    if (granularity == 0 && wanted != (uint32_t)preferred)
        return LND_ERR_INVALID_ARG;
    *frames = wanted;
    return LND_OK;
}
