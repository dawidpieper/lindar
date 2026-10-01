#include "gain.h"
#include "native.h"
#include "src/pcm.h"
#include "src/sample.h"

#include <math.h>
#include <string.h>

#define LND_GAIN_INTEGER(format)                                                                                                                               \
    for (uint32_t f = 0; f < frames; f++) {                                                                                                                    \
        float gain = gains[f];                                                                                                                                 \
        if (gain == 1.0f) continue;                                                                                                                            \
        if (gain > 65535.0f) {                                                                                                                                 \
            for (uint32_t c = 0; c < pcm->channels; c++) {                                                                                                     \
                uint8_t *p = planes[c] + (size_t)f * stride;                                                                                                   \
                lnd_pcm_store_sample(p, format, lnd_pcm_integer_load(p, format) * (1.0 / 2147483648.0) * gain);                                                \
            }                                                                                                                                                  \
            continue;                                                                                                                                          \
        }                                                                                                                                                      \
        int64_t q = (int64_t)llround((double)gain * 65536.0);                                                                                                  \
        for (uint32_t c = 0; c < pcm->channels; c++) {                                                                                                         \
            uint8_t *p = planes[c] + (size_t)f * stride;                                                                                                       \
            int64_t value = lnd_pcm_integer_load(p, format) * q;                                                                                               \
            value = value >= 0 ? (value + 32768) / 65536 : -((-value + 32768) / 65536);                                                                        \
            lnd_pcm_integer_store(p, format, value);                                                                                                           \
        }                                                                                                                                                      \
    }                                                                                                                                                          \
    break

#define LND_GAIN_FLOAT(type)                                                                                                                                   \
    for (uint32_t c = 0; c < pcm->channels; c++) {                                                                                                             \
        uint8_t *p = planes[c];                                                                                                                                \
        for (uint32_t f = 0; f < frames; f++, p += stride) {                                                                                                   \
            type value;                                                                                                                                        \
            memcpy(&value, p, sizeof value);                                                                                                                   \
            value *= gains[f];                                                                                                                                 \
            memcpy(p, &value, sizeof value);                                                                                                                   \
        }                                                                                                                                                      \
    }                                                                                                                                                          \
    break

void lnd_graph_pcm_gains(const LND_PCM *pcm, size_t offset, uint32_t frames, const float *gains) {
    uint8_t *planes[LND_MAX_CHANNELS];
    size_t stride = lnd_pcm_stride(pcm);
    for (uint32_t c = 0; c < pcm->channels; c++) planes[c] = lnd_pcm_at(pcm, c, offset);
    switch (pcm->format) {
    case LND_FORMAT_U8:
        LND_GAIN_INTEGER(LND_FORMAT_U8);
    case LND_FORMAT_S16:
        LND_GAIN_INTEGER(LND_FORMAT_S16);
    case LND_FORMAT_S24:
        LND_GAIN_INTEGER(LND_FORMAT_S24);
    case LND_FORMAT_S32:
        LND_GAIN_INTEGER(LND_FORMAT_S32);
    case LND_FORMAT_F32:
        LND_GAIN_FLOAT(float);
    case LND_FORMAT_F64:
        LND_GAIN_FLOAT(double);
    }
}

void lnd_graph_pcm_ramp_active(const LND_PCM *pcm, size_t offset, uint32_t frames, float *gain, float step, float target, uint32_t *remaining) {
    uint32_t count = LND_MIN(frames, *remaining);
    float value = *gain;
    bool direct = pcm->format == LND_FORMAT_F32 && pcm->layout == LND_LAYOUT_INTERLEAVED && lnd_pcm_stride(pcm) == pcm->channels * sizeof(float) &&
                  (uintptr_t)lnd_pcm_at(pcm, 0, offset) % alignof(float) == 0;
    if (direct) {
        float *out = (float *)lnd_pcm_at(pcm, 0, offset);
        for (uint32_t f = 0; f < count; f++) {
            value += step;
            if (--*remaining == 0) value = target;
            for (uint32_t c = 0; c < pcm->channels; c++) *out++ *= value;
        }
    } else {
        float gains[64];
        for (uint32_t done = 0; done < count;) {
            uint32_t block = LND_MIN(count - done, 64);
            for (uint32_t f = 0; f < block; f++) {
                value += step;
                if (--*remaining == 0) value = target;
                gains[f] = value;
            }
            lnd_graph_pcm_gains(pcm, offset + done, block, gains);
            done += block;
        }
    }
    *gain = value;
    if (count < frames) lnd_graph_pcm_gain(pcm, offset + count, frames - count, value);
}
