#pragma once

#include "src/platform.h"
#include "lindar_audio.h"
#include "sinc.h"
#include "biquad.h"

typedef struct lnd_simd_ops {
    void (*accumulate)(float *dst, const float *src, float gain, size_t n);
    void (*scale)(float *dst, float gain, size_t n);
    void (*clip_hard)(float *dst, size_t n);
    void (*clip_soft)(float *dst, size_t n);
    void (*s16_to_f32)(const int16_t *src, float *dst, size_t n);
    void (*f32_to_s16)(const float *src, int16_t *dst, size_t n);
    void (*split_stereo)(const void *src, void *left, void *right, size_t frames, size_t bytes);
    void (*join_stereo)(const void *left, const void *right, void *dst, size_t frames, size_t bytes);
    void (*matrix_stereo)(const float *src, float *dst, size_t frames, const float *matrix);
    uint32_t (*biquad_f32)(float *pcm, uint32_t frames, uint32_t channels, const lnd_biquad_coeff_f32 *k, float *z1, float *z2);
    void (*biquad_f64)(float *pcm, uint32_t frames, uint32_t channels, const lnd_biquad_coeff *k, double z[2][2]);
    lnd_sinc_dot sinc;
    lnd_sinc_selector sinc_select;
    lnd_sinc_frame_selector sinc_frame_select;
    const char *name;
    const char *sinc_name;
} lnd_simd_ops;

extern lnd_simd_ops lnd_simd;
extern const lnd_simd_ops lnd_simd_c;

float lnd_sinc_dot_c(const float *src, const float *coefficients, uint32_t taps, uint32_t stride);
void lnd_simd_init(int32_t mode);
void lnd_simd_x86(int32_t mode);
void lnd_simd_neon(int32_t mode);
bool lnd_simd_pcm_integer(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames);
bool lnd_simd_pcm_convert(const LND_PCM *dst, size_t dst_offset, const LND_PCM *src, size_t src_offset, size_t frames);
bool lnd_simd_pcm_scale(const LND_PCM *pcm, size_t offset, size_t frames, float gain);

void lnd_simd_biquad_init(void);
