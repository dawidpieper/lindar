#include "src/pcm.h"
#include "convert.h"
#if LND_MODULE_SIMD
#include "pcm/audio/simd.h"
#endif

#include <math.h>
#include <string.h>

double lnd_pcm_load_sample(const uint8_t *p, int32_t format) {
    if (format == LND_FORMAT_F32) {
        float value;
        memcpy(&value, p, sizeof value);
        return value;
    }
    if (format == LND_FORMAT_F64) {
        double value;
        memcpy(&value, p, sizeof value);
        return value;
    }
    return (double)lnd_pcm_load_integer(p, format) * (1.0 / 2147483648.0);
}

void lnd_pcm_store_sample(uint8_t *p, int32_t format, double value) {
    if (format == LND_FORMAT_F32) {
        float sample = (float)value;
        memcpy(p, &sample, sizeof sample);
    } else if (format == LND_FORMAT_F64) {
        memcpy(p, &value, sizeof value);
    } else {
        if (isnan(value)) value = 0.0;
        uint32_t bits = (uint32_t)LND_PcmGetSampleBytes(format) * 8;
        double limit = (double)((uint64_t)1 << (bits - 1));
        value = LND_CLAMP(value, -1.0, 1.0);
        int64_t sample = (int64_t)round(value * limit);
        sample = LND_MIN(sample, (int64_t)limit - 1);
        lnd_pcm_store_integer(p, format, sample * ((int64_t)1 << (32 - bits)));
    }
}

bool lnd_pcm_convert_float(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames) {
    bool to_float = src->format == LND_FORMAT_S16 && dst->format == LND_FORMAT_F32;
    bool to_short = src->format == LND_FORMAT_F32 && dst->format == LND_FORMAT_S16;
    if (!to_float && !to_short) return false;
    size_t ss = lnd_pcm_stride(src), ds = lnd_pcm_stride(dst);
    for (uint32_t c = 0; c < src->channels; c++) {
        const uint8_t *s = lnd_pcm_at(src, c, src_offset);
        uint8_t *d = lnd_pcm_at(dst, c, dst_offset);
        if (to_float) {
            for (size_t f = 0; f < frames; f++) {
                float value = (int16_t)((uint16_t)s[f * ss] | (uint16_t)s[f * ss + 1] << 8) * (1.0f / 32768);
                memcpy(d + f * ds, &value, sizeof value);
            }
        } else {
            for (size_t f = 0; f < frames; f++) {
                float value;
                memcpy(&value, s + f * ss, sizeof value);
                uint16_t out = (uint16_t)lnd_pcm_s16(value);
                d[f * ds] = (uint8_t)out;
                d[f * ds + 1] = (uint8_t)(out >> 8);
            }
        }
    }
    return true;
}

void lnd_pcm_gain_float(const LND_PCM *pcm, size_t offset, size_t frames, uint32_t gain) {
    double volume = (double)gain / 65536.0;
#if LND_MODULE_SIMD
    if ((double)(float)volume == volume && lnd_simd_pcm_scale(pcm, offset, frames, (float)volume)) return;
#endif
    size_t stride = lnd_pcm_stride(pcm);
    for (uint32_t c = 0; c < pcm->channels; c++) {
        uint8_t *data = lnd_pcm_at(pcm, c, offset);
        if (pcm->format == LND_FORMAT_F32) {
            for (size_t f = 0; f < frames; f++) {
                float value;
                memcpy(&value, data + f * stride, sizeof value);
                value = (float)((double)value * volume);
                memcpy(data + f * stride, &value, sizeof value);
            }
        } else {
            for (size_t f = 0; f < frames; f++) {
                double value;
                memcpy(&value, data + f * stride, sizeof value);
                value *= volume;
                memcpy(data + f * stride, &value, sizeof value);
            }
        }
    }
}
