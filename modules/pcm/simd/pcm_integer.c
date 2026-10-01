#include "pcm/audio/simd.h"
#include "src/pcm.h"

static int32_t s16(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8); }

static int32_t s24(const uint8_t *p) {
    uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
    return v & 0x800000 ? (int32_t)v - 16777216 : (int32_t)v;
}

static int32_t s32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }

static int32_t narrow(int64_t value, int32_t unit) { return (int32_t)(value >= 0 ? (value + unit / 2) / unit : -((-value + unit / 2) / unit)); }

static void put_s16(uint8_t *p, int32_t value) {
    uint16_t v = (uint16_t)LND_CLAMP(value, -32768, 32767);
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

bool lnd_simd_pcm_integer(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames) {
    size_t ss = lnd_pcm_stride(src), ds = lnd_pcm_stride(dst);
    uint32_t channels = src->channels;
#define LND_PAIR(SRC, DST, EXPR)                                                                                                                             \
    case (SRC) * 8 + (DST):                                                                                                                                  \
        if (((SRC) == LND_FORMAT_U8 || (DST) == LND_FORMAT_U8) && channels > 1 &&                                                                            \
            src->layout == LND_LAYOUT_INTERLEAVED && dst->layout == LND_LAYOUT_INTERLEAVED &&                                                                \
            ss == ((SRC) == LND_FORMAT_U8 ? 1u : 2u) * channels && ds == ((DST) == LND_FORMAT_U8 ? 1u : 2u) * channels) {                                    \
            frames *= channels;                                                                                                                              \
            ss = (SRC) == LND_FORMAT_U8 ? 1 : 2;                                                                                                             \
            ds = (DST) == LND_FORMAT_U8 ? 1 : 2;                                                                                                             \
            channels = 1;                                                                                                                                    \
        }                                                                                                                                                    \
        for (uint32_t c = 0; c < channels; c++) {                                                                                                            \
            const uint8_t *in = lnd_pcm_at(src, c, src_offset);                                                                                              \
            uint8_t *out = lnd_pcm_at(dst, c, dst_offset);                                                                                                   \
            for (size_t f = 0; f < frames; f++) {                                                                                                            \
                const uint8_t *s = in + f * ss;                                                                                                              \
                uint8_t *d = out + f * ds;                                                                                                                   \
                EXPR;                                                                                                                                        \
            }                                                                                                                                                \
        }                                                                                                                                                    \
        return true
    switch (src->format * 8 + dst->format) {
        LND_PAIR(LND_FORMAT_U8, LND_FORMAT_S16, d[0] = 0; d[1] = s[0] ^ 128);
        LND_PAIR(LND_FORMAT_S16, LND_FORMAT_U8, d[0] = (uint8_t)LND_MIN(narrow(s16(s), 256) + 128, 255));
        LND_PAIR(LND_FORMAT_S16, LND_FORMAT_S24, d[0] = 0; d[1] = s[0]; d[2] = s[1]);
        LND_PAIR(LND_FORMAT_S24, LND_FORMAT_S16, put_s16(d, narrow(s24(s), 256)));
        LND_PAIR(LND_FORMAT_S16, LND_FORMAT_S32, d[0] = d[1] = 0; d[2] = s[0]; d[3] = s[1]);
        LND_PAIR(LND_FORMAT_S32, LND_FORMAT_S16, put_s16(d, narrow(s32(s), 65536)));
    }
#undef LND_PAIR
    return false;
}
