#include "lindar_simd.h"
#include "pcm/audio/simd.h"
#include "src/pcm.h"

const char *LND_SimdGetName(void) { return lnd_simd.name; }
const char *LND_SimdGetSincName(void) { return lnd_simd.sinc_name; }

bool lnd_simd_pcm_convert(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames) {
    if (src->format <= LND_FORMAT_S32 && dst->format <= LND_FORMAT_S32 && lnd_simd_pcm_integer(dst, dst_offset, src, src_offset, frames)) return true;
    size_t sb = LND_PcmGetSampleBytes(src->format), db = LND_PcmGetSampleBytes(dst->format);
    size_t ss = lnd_pcm_stride(src), ds = lnd_pcm_stride(dst);
    uint32_t channels = src->channels;
    if (src->format == dst->format && channels == 2 && (sb == 2 || sb == 4) && src->layout != dst->layout) {
        if (src->layout == LND_LAYOUT_INTERLEAVED && ss == sb * 2 && ds == sb) {
            lnd_simd.split_stereo(lnd_pcm_at(src, 0, src_offset), lnd_pcm_at(dst, 0, dst_offset), lnd_pcm_at(dst, 1, dst_offset), frames, sb);
            return true;
        }
        if (dst->layout == LND_LAYOUT_INTERLEAVED && ds == db * 2 && ss == sb) {
            lnd_simd.join_stereo(lnd_pcm_at(src, 0, src_offset), lnd_pcm_at(src, 1, src_offset), lnd_pcm_at(dst, 0, dst_offset), frames, sb);
            return true;
        }
    }
    if (src->layout != dst->layout) return false;
    size_t count = frames, planes = channels;
    if (src->layout == LND_LAYOUT_INTERLEAVED) {
        if (ss != sb * channels || ds != db * channels) return false;
        count *= channels;
        planes = 1;
    } else if (ss != sb || ds != db)
        return false;
    bool to_float = src->format == LND_FORMAT_S16 && dst->format == LND_FORMAT_F32;
    bool to_short = src->format == LND_FORMAT_F32 && dst->format == LND_FORMAT_S16;
    if (!to_float && !to_short) return false;
    for (uint32_t c = 0; c < planes; c++) {
        const void *in = lnd_pcm_at(src, c, src_offset);
        void *out = lnd_pcm_at(dst, c, dst_offset);
        if (to_float)
            lnd_simd.s16_to_f32(in, out, count);
        else
            lnd_simd.f32_to_s16(in, out, count);
    }
    return true;
}

bool lnd_simd_pcm_scale(const LND_PCM *pcm, size_t offset, size_t frames, float gain) {
    if (pcm->format != LND_FORMAT_F32) return false;
    size_t stride = lnd_pcm_stride(pcm);
    if (pcm->layout == LND_LAYOUT_INTERLEAVED) {
        if (stride != pcm->channels * sizeof(float)) return false;
        float *data = (float *)lnd_pcm_at(pcm, 0, offset);
        if ((uintptr_t)data % alignof(float)) return false;
        lnd_simd.scale(data, gain, frames * pcm->channels);
    } else {
        if (stride != sizeof(float)) return false;
        for (uint32_t c = 0; c < pcm->channels; c++)
            if ((uintptr_t)lnd_pcm_at(pcm, c, offset) % alignof(float)) return false;
        for (uint32_t c = 0; c < pcm->channels; c++)
            lnd_simd.scale((float *)lnd_pcm_at(pcm, c, offset), gain, frames);
    }
    return true;
}
