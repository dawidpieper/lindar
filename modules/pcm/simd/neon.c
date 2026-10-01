#include "pcm/audio/simd.h"

void lnd_sinc_neon_init(void);

#if defined(LND_ARCH_ARM64) && (defined(__ARM_NEON) || defined(_M_ARM64))
#if defined(LND_COMPILER_MSVC)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif

static void accumulate(float *restrict dst, const float *restrict src, float gain, size_t n) {
    size_t i = 0;
    for (; n - i >= 16; i += 16) {
        vst1q_f32(dst + i, vaddq_f32(vld1q_f32(dst + i), vmulq_n_f32(vld1q_f32(src + i), gain)));
        vst1q_f32(dst + i + 4, vaddq_f32(vld1q_f32(dst + i + 4), vmulq_n_f32(vld1q_f32(src + i + 4), gain)));
        vst1q_f32(dst + i + 8, vaddq_f32(vld1q_f32(dst + i + 8), vmulq_n_f32(vld1q_f32(src + i + 8), gain)));
        vst1q_f32(dst + i + 12, vaddq_f32(vld1q_f32(dst + i + 12), vmulq_n_f32(vld1q_f32(src + i + 12), gain)));
    }
    lnd_simd_c.accumulate(dst + i, src + i, gain, n - i);
}

static void scale(float *dst, float gain, size_t n) {
    size_t i = 0;
    for (; n - i >= 16; i += 16) {
        vst1q_f32(dst + i, vmulq_n_f32(vld1q_f32(dst + i), gain));
        vst1q_f32(dst + i + 4, vmulq_n_f32(vld1q_f32(dst + i + 4), gain));
        vst1q_f32(dst + i + 8, vmulq_n_f32(vld1q_f32(dst + i + 8), gain));
        vst1q_f32(dst + i + 12, vmulq_n_f32(vld1q_f32(dst + i + 12), gain));
    }
    lnd_simd_c.scale(dst + i, gain, n - i);
}

static void clip(float *dst, size_t n) {
    float32x4_t lo = vdupq_n_f32(-1), hi = vdupq_n_f32(1);
    size_t i = 0;
    for (; n - i >= 4; i += 4) {
        float32x4_t x = vld1q_f32(dst + i);
        x = vbslq_f32(vcltq_f32(x, lo), lo, x);
        x = vbslq_f32(vcgtq_f32(x, hi), hi, x);
        vst1q_f32(dst + i, x);
    }
    lnd_simd_c.clip_hard(dst + i, n - i);
}

static void s16_f32(const int16_t *src, float *dst, size_t n) {
    size_t i = 0;
    for (; n - i >= 16; i += 16) {
        int16x8_t a = vld1q_s16((const int16_t *)((const uint8_t *)src + i * 2));
        int16x8_t b = vld1q_s16((const int16_t *)((const uint8_t *)src + i * 2 + 16));
        uint8_t *out = (uint8_t *)dst + i * 4;
        vst1q_f32((float *)out, vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(a))), 1.0f / 32768));
        vst1q_f32((float *)(out + 16), vmulq_n_f32(vcvtq_f32_s32(vmovl_high_s16(a)), 1.0f / 32768));
        vst1q_f32((float *)(out + 32), vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(b))), 1.0f / 32768));
        vst1q_f32((float *)(out + 48), vmulq_n_f32(vcvtq_f32_s32(vmovl_high_s16(b)), 1.0f / 32768));
    }
    lnd_simd_c.s16_to_f32((const int16_t *)((const uint8_t *)src + i * 2), (float *)((uint8_t *)dst + i * 4), n - i);
}

static void f32_s16(const float *src, int16_t *dst, size_t n) {
    size_t i = 0;
    for (; n - i >= 4; i += 4) {
        float32x4_t x = vld1q_f32((const float *)((const uint8_t *)src + i * 4));
        x = vbslq_f32(vceqq_f32(x, x), x, vdupq_n_f32(0));
        x = vminq_f32(vmaxq_f32(x, vdupq_n_f32(-1)), vdupq_n_f32(32767.0f / 32768));
        x = vmulq_n_f32(x, 32768);
        int32x4_t q = vcvtaq_s32_f32(x);
        vst1_s16((int16_t *)((uint8_t *)dst + i * 2), vqmovn_s32(q));
    }
    lnd_simd_c.f32_to_s16((const float *)((const uint8_t *)src + i * 4), (int16_t *)((uint8_t *)dst + i * 2), n - i);
}

static void split(const void *input, void *left, void *right, size_t frames, size_t bytes) {
    const uint8_t *src = input;
    uint8_t *l = left, *r = right;
    size_t f = 0;
    if (bytes == 4) {
        for (; frames - f >= 4; f += 4) {
            uint32x4x2_t x = vld2q_u32((const uint32_t *)(src + f * 8));
            vst1q_u32((uint32_t *)(l + f * 4), x.val[0]);
            vst1q_u32((uint32_t *)(r + f * 4), x.val[1]);
        }
    } else if (bytes == 2) {
        for (; frames - f >= 8; f += 8) {
            uint16x8x2_t x = vld2q_u16((const uint16_t *)(src + f * 4));
            vst1q_u16((uint16_t *)(l + f * 2), x.val[0]);
            vst1q_u16((uint16_t *)(r + f * 2), x.val[1]);
        }
    }
    lnd_simd_c.split_stereo(src + f * bytes * 2, l + f * bytes, r + f * bytes, frames - f, bytes);
}

static void join(const void *left, const void *right, void *output, size_t frames, size_t bytes) {
    const uint8_t *l = left, *r = right;
    uint8_t *dst = output;
    size_t f = 0;
    if (bytes == 4) {
        for (; frames - f >= 4; f += 4) {
            uint32x4x2_t x = {{vld1q_u32((const uint32_t *)(l + f * 4)), vld1q_u32((const uint32_t *)(r + f * 4))}};
            vst2q_u32((uint32_t *)(dst + f * 8), x);
        }
    } else if (bytes == 2) {
        for (; frames - f >= 8; f += 8) {
            uint16x8x2_t x = {{vld1q_u16((const uint16_t *)(l + f * 2)), vld1q_u16((const uint16_t *)(r + f * 2))}};
            vst2q_u16((uint16_t *)(dst + f * 4), x);
        }
    }
    lnd_simd_c.join_stereo(l + f * bytes, r + f * bytes, dst + f * bytes * 2, frames - f, bytes);
}

static void matrix(const float *restrict src, float *restrict dst, size_t frames, const float *m) {
    size_t f = 0;
    for (; frames - f >= 4; f += 4) {
        float32x4x2_t x = vld2q_f32(src + f * 2);
        float32x4x2_t y = {
            {vaddq_f32(vmulq_n_f32(x.val[0], m[0]), vmulq_n_f32(x.val[1], m[1])), vaddq_f32(vmulq_n_f32(x.val[0], m[2]), vmulq_n_f32(x.val[1], m[3]))}};
        vst2q_f32(dst + f * 2, y);
    }
    lnd_simd_c.matrix_stereo(src + f * 2, dst + f * 2, frames - f, m);
}
#endif

void lnd_simd_neon(int32_t mode) {
#if defined(LND_ARCH_ARM64) && (defined(__ARM_NEON) || defined(_M_ARM64))
    if (mode != LND_SIMD_AUTO && mode != LND_SIMD_NEON) return;
    lnd_simd.accumulate = accumulate;
    lnd_simd.scale = scale;
    lnd_simd.clip_hard = clip;
    const uint16_t endian = 1;
    if (*(const uint8_t *)&endian) {
        lnd_simd.s16_to_f32 = s16_f32;
        lnd_simd.f32_to_s16 = f32_s16;
    }
    lnd_simd.split_stereo = split;
    lnd_simd.join_stereo = join;
    lnd_simd.matrix_stereo = matrix;
    lnd_simd.name = "neon";
    lnd_sinc_neon_init();
#else
    LND_UNUSED(mode);
#endif
}
