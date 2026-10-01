#include "pcm.h"
#include "sample.h"
#include "error.h"
#if LND_MODULE_SIMD
#include "pcm/audio/simd.h"
#endif

#include <string.h>

size_t LND_PcmGetSampleBytes(int32_t format) {
    static const uint8_t sizes[] = {0, 1, 2, 3, 4, 4, 8};
    return format > LND_FORMAT_NONE && format <= LND_FORMAT_F64 ? sizes[format] : 0;
}

size_t lnd_pcm_stride(const LND_PCM *pcm) {
    if (pcm->stride_bytes) return pcm->stride_bytes;
    return LND_PcmGetSampleBytes(pcm->format) * (pcm->layout == LND_LAYOUT_INTERLEAVED ? pcm->channels : 1);
}

uint8_t *lnd_pcm_at(const LND_PCM *pcm, uint32_t channel, size_t frame) {
    size_t offset = frame * lnd_pcm_stride(pcm);
    return pcm->layout == LND_LAYOUT_PLANAR ? (uint8_t *)pcm->planes[channel] + offset
                                            : (uint8_t *)pcm->data + offset + channel * LND_PcmGetSampleBytes(pcm->format);
}

int32_t LND_PcmValidate(const LND_PCM *pcm) {
    if (!pcm || !LND_PcmGetSampleBytes(pcm->format) || !pcm->channels || pcm->channels > LND_MAX_CHANNELS ||
        (pcm->layout != LND_LAYOUT_INTERLEAVED && pcm->layout != LND_LAYOUT_PLANAR))
        return lnd_error(LND_ERR_INVALID_ARG);
    size_t width = LND_PcmGetSampleBytes(pcm->format) * (pcm->layout == LND_LAYOUT_INTERLEAVED ? pcm->channels : 1);
    size_t stride = lnd_pcm_stride(pcm);
    if (stride < width || (pcm->frames && pcm->frames - 1 > (SIZE_MAX - width) / stride)) return lnd_error(LND_ERR_INVALID_ARG);
    if (!pcm->frames) return LND_OK;
    size_t extent = (pcm->frames - 1) * stride + width;
    if (pcm->layout == LND_LAYOUT_PLANAR) {
        if (!pcm->planes) return lnd_error(LND_ERR_INVALID_ARG);
        for (uint32_t c = 0; c < pcm->channels; c++) {
            if (!pcm->planes[c] || (uintptr_t)pcm->planes[c] > UINTPTR_MAX - extent) return lnd_error(LND_ERR_INVALID_ARG);
        }
    } else if (!pcm->data || (uintptr_t)pcm->data > UINTPTR_MAX - extent) {
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    return LND_OK;
}

bool lnd_pcm_range(const LND_PCM *pcm, size_t offset, size_t frames) {
    return LND_PcmValidate(pcm) == LND_OK && offset <= pcm->frames && frames <= pcm->frames - offset;
}

bool lnd_pcm_writable(const LND_PCM *pcm, size_t offset, size_t frames) {
    if (!lnd_pcm_range(pcm, offset, frames)) return false;
    if (!frames || pcm->layout != LND_LAYOUT_PLANAR) return true;
    size_t bytes = LND_PcmGetSampleBytes(pcm->format), stride = lnd_pcm_stride(pcm);
    size_t extent = (frames - 1) * stride + bytes;
    for (uint32_t a = 0; a < pcm->channels; a++)
        for (uint32_t b = a + 1; b < pcm->channels; b++) {
            uintptr_t x = (uintptr_t)pcm->planes[a], y = (uintptr_t)pcm->planes[b];
            uintptr_t delta = x > y ? x - y : y - x;
            if (delta >= extent) continue;
            if (delta < bytes) return false;
            uintptr_t whole = delta / stride, part = delta % stride;
            if ((whole < frames && part < bytes) || (part && whole < frames - 1 && stride - part < bytes)) return false;
        }
    return true;
}

int64_t lnd_pcm_load_integer(const uint8_t *p, int32_t format) { return lnd_pcm_integer_load(p, format); }

void lnd_pcm_store_integer(uint8_t *p, int32_t format, int64_t value) { lnd_pcm_integer_store(p, format, value); }

static bool lnd_pcm_overlaps(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames) {
    uint32_t dc = dst->layout == LND_LAYOUT_PLANAR ? dst->channels : 1;
    uint32_t sc = src->layout == LND_LAYOUT_PLANAR ? src->channels : 1;
    size_t dw = LND_PcmGetSampleBytes(dst->format) * (dc == 1 && dst->layout == LND_LAYOUT_INTERLEAVED ? dst->channels : 1);
    size_t sw = LND_PcmGetSampleBytes(src->format) * (sc == 1 && src->layout == LND_LAYOUT_INTERLEAVED ? src->channels : 1);
    size_t ds = (frames - 1) * lnd_pcm_stride(dst) + dw;
    size_t ss = (frames - 1) * lnd_pcm_stride(src) + sw;
    for (uint32_t d = 0; d < dc; d++) {
        uintptr_t dp = (uintptr_t)lnd_pcm_at(dst, d, dst_offset);
        for (uint32_t s = 0; s < sc; s++) {
            uintptr_t sp = (uintptr_t)lnd_pcm_at(src, s, src_offset);
            if (dp < sp + ss && sp < dp + ds) return true;
        }
    }
    return false;
}

LND_INLINE int32_t lnd_pcm_convert_impl(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames, bool validate) {
    if (validate && (!lnd_pcm_writable(dst, dst_offset, frames) || !lnd_pcm_range(src, src_offset, frames) || dst->channels != src->channels))
        return lnd_error(LND_ERR_INVALID_ARG);
    if (!frames) return LND_OK;
    size_t ds = lnd_pcm_stride(dst), ss = lnd_pcm_stride(src);
    size_t bytes = LND_PcmGetSampleBytes(src->format);
    bool same = dst->format == src->format && dst->layout == src->layout && ds == ss;
    if (same && dst->layout == LND_LAYOUT_INTERLEAVED && ds == bytes * src->channels) {
        memmove(lnd_pcm_at(dst, 0, dst_offset), lnd_pcm_at(src, 0, src_offset), frames * ds);
        return LND_OK;
    }
    if (same) {
        bool identical = true;
        for (uint32_t c = 0; c < src->channels; c++) {
            if (lnd_pcm_at(dst, c, dst_offset) != lnd_pcm_at(src, c, src_offset)) identical = false;
        }
        if (identical) return LND_OK;
    }
    if (validate && lnd_pcm_overlaps(dst, dst_offset, src, src_offset, frames)) return lnd_error(LND_ERR_INVALID_ARG);
#if LND_MODULE_SIMD
    if (lnd_simd_pcm_convert(dst, dst_offset, src, src_offset, frames)) return LND_OK;
#endif
#if LND_MODULE_PCM_FLOAT
    if (lnd_pcm_convert_float(dst, dst_offset, src, src_offset, frames)) return LND_OK;
#endif
    uint32_t channels = src->channels;
    if (dst->layout == LND_LAYOUT_INTERLEAVED && src->layout == LND_LAYOUT_INTERLEAVED) {
        if (dst->format == src->format) {
            bytes *= channels;
            channels = 1;
        } else if (ss == bytes * channels && ds == LND_PcmGetSampleBytes(dst->format) * channels) {
            frames *= channels;
            ss = bytes;
            ds = LND_PcmGetSampleBytes(dst->format);
            channels = 1;
        }
    }
    for (uint32_t c = 0; c < channels; c++) {
        const uint8_t *s = lnd_pcm_at(src, c, src_offset);
        uint8_t *d = lnd_pcm_at(dst, c, dst_offset);
        if (dst->format == src->format) {
            if (ds == bytes && ss == bytes)
                memcpy(d, s, frames * bytes);
            else {
                switch (bytes) {
                case 1:
                    for (size_t f = 0; f < frames; f++) memcpy(d + f * ds, s + f * ss, 1);
                    break;
                case 2:
                    for (size_t f = 0; f < frames; f++) memcpy(d + f * ds, s + f * ss, 2);
                    break;
                case 3:
                    for (size_t f = 0; f < frames; f++) memcpy(d + f * ds, s + f * ss, 3);
                    break;
                case 4:
                    for (size_t f = 0; f < frames; f++) memcpy(d + f * ds, s + f * ss, 4);
                    break;
                case 8:
                    for (size_t f = 0; f < frames; f++) memcpy(d + f * ds, s + f * ss, 8);
                    break;
                default:
                    for (size_t f = 0; f < frames; f++) memcpy(d + f * ds, s + f * ss, bytes);
                    break;
                }
            }
        } else if (dst->format <= LND_FORMAT_S32 && src->format <= LND_FORMAT_S32) {
            for (size_t f = 0; f < frames; f++)
                lnd_pcm_store_integer(d + f * ds, dst->format, lnd_pcm_load_integer(s + f * ss, src->format));
        } else {
#if LND_MODULE_PCM_FLOAT
            for (size_t f = 0; f < frames; f++)
                lnd_pcm_store_sample(d + f * ds, dst->format, lnd_pcm_load_sample(s + f * ss, src->format));
#else
            return lnd_error(LND_ERR_UNSUPPORTED);
#endif
        }
    }
    return LND_OK;
}

int32_t LND_PcmConvert(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames) {
    return lnd_pcm_convert_impl(dst, dst_offset, src, src_offset, frames, true);
}

int32_t lnd_pcm_convert(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames) {
    return lnd_pcm_convert_impl(dst, dst_offset, src, src_offset, frames, false);
}

int32_t LND_PcmSilence(const LND_PCM *pcm, size_t offset, size_t frames) {
    if (!lnd_pcm_writable(pcm, offset, frames)) return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_pcm_silence(pcm, offset, frames);
}

int32_t lnd_pcm_silence(const LND_PCM *pcm, size_t offset, size_t frames) {
    if (!frames) return LND_OK;
    size_t bytes = LND_PcmGetSampleBytes(pcm->format), stride = lnd_pcm_stride(pcm);
    int value = pcm->format == LND_FORMAT_U8 ? 128 : 0;
    if (pcm->layout == LND_LAYOUT_INTERLEAVED && stride == bytes * pcm->channels) {
        memset(lnd_pcm_at(pcm, 0, offset), value, frames * stride);
    } else {
        for (uint32_t c = 0; c < pcm->channels; c++) {
            uint8_t *p = lnd_pcm_at(pcm, c, offset);
            if (stride == bytes)
                memset(p, value, frames * bytes);
            else
                for (size_t f = 0; f < frames; f++)
                    memset(p + f * stride, value, bytes);
        }
    }
    return LND_OK;
}

bool lnd_pcm_memory_overlaps(const LND_PCM *pcm, const void *memory, size_t bytes) {
    if (!pcm->frames) return false;
    uintptr_t begin = (uintptr_t)memory;
    if (begin > UINTPTR_MAX - bytes) return true;
    uint32_t channels = pcm->layout == LND_LAYOUT_PLANAR ? pcm->channels : 1;
    size_t width = LND_PcmGetSampleBytes(pcm->format) * (pcm->layout == LND_LAYOUT_PLANAR ? 1 : pcm->channels);
    size_t extent = (pcm->frames - 1) * lnd_pcm_stride(pcm) + width;
    for (uint32_t c = 0; c < channels; c++) {
        uintptr_t at = (uintptr_t)lnd_pcm_at(pcm, c, 0);
        if (at < begin + bytes && begin < at + extent) return true;
    }
    return false;
}
