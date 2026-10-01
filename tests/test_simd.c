#include "stream_test.h"
#include "pcm/audio/simd.h"
#include "src/pcm.h"
#include "lindar.h"
#include <math.h>
#include <fenv.h>
#if LND_MODULE_SIMD
#include "lindar_simd.h"
#endif
#if LND_MODULE_SINC_ASM
#include "lindar_sinc_asm.h"
#endif
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static uint32_t random_state = 17;
static float random_sample(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return (float)(int32_t)(random_state >> 8) / 8388608.0f - 1.0f;
}

static void test_convert(void) {
    static const size_t sizes[] = {0, 1, 2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 257};
    static const float edge[] = {
        0, -0.0f, 1, -1, INFINITY, -INFINITY, NAN, 0.5f / 32768, -0.5f / 32768, 1.5f / 32768, -1.5f / 32768, 32766.5f / 32768, -32767.5f / 32768};
    uint8_t input[257 * 4 + 32], output[257 * 4 + 32], reference[257 * 4 + 32];
    for (size_t k = 0; k < sizeof sizes / sizeof *sizes; k++) {
        size_t n = sizes[k];
        for (size_t alignment = 0; alignment < 16; alignment++) {
            memset(output, 0xa5, sizeof output);
            memset(reference, 0xa5, sizeof reference);
            for (size_t i = 0; i < n; i++) {
                float x = i < sizeof edge / sizeof *edge ? edge[i] : random_sample() * 1.2f;
                if (i == 19) x = nextafterf(0.5f / 32768, 0);
                if (i == 20) x = nextafterf(-0.5f / 32768, 0);
                memcpy(input + alignment + i * 4, &x, 4);
            }
            lnd_simd_c.f32_to_s16((const float *)(input + alignment), (int16_t *)(reference + alignment), n);
            lnd_simd.f32_to_s16((const float *)(input + alignment), (int16_t *)(output + alignment), n);
            CHECK(memcmp(output, reference, sizeof output) == 0);
            LND_PCM src = {.data = input + alignment, .frames = n, .channels = 1, .format = LND_FORMAT_F32};
            LND_PCM dst = {.data = output + alignment, .frames = n, .channels = 1, .format = LND_FORMAT_S16};
            memset(output, 0xa5, sizeof output);
            CHECK(LND_PcmConvert(&dst, 0, &src, 0, n) == LND_OK);
            CHECK(memcmp(output, reference, sizeof output) == 0);
            for (size_t i = 0; i < n; i++) {
                int16_t x = (int16_t)(i * 193 - 32768);
                memcpy(input + alignment + i * 2, &x, 2);
            }
            memset(output, 0xa5, sizeof output);
            memset(reference, 0xa5, sizeof reference);
            lnd_simd_c.s16_to_f32((const int16_t *)(input + alignment), (float *)(reference + alignment), n);
            lnd_simd.s16_to_f32((const int16_t *)(input + alignment), (float *)(output + alignment), n);
            CHECK(memcmp(output, reference, sizeof output) == 0);
            src.format = LND_FORMAT_S16;
            dst.format = LND_FORMAT_F32;
            memset(output, 0xa5, sizeof output);
            CHECK(LND_PcmConvert(&dst, 0, &src, 0, n) == LND_OK);
            CHECK(memcmp(output, reference, sizeof output) == 0);
        }
    }
    static const int16_t golden[] = {0, 0, 32767, -32768, 32767, -32768, 0, 1, -1, 2, -2, 32767, -32768};
    float exact[32];
    int16_t converted[32];
    for (size_t i = 0; i < 32; i++)
        exact[i] = edge[i % (sizeof edge / sizeof *edge)];
    for (int backend = 0; backend < 2; backend++) {
        (backend ? lnd_simd.f32_to_s16 : lnd_simd_c.f32_to_s16)(exact, converted, 32);
        for (size_t i = 0; i < 32; i++)
            CHECK(converted[i] == golden[i % (sizeof golden / sizeof *golden)]);
    }
    int16_t all[256], back[256];
    float expanded[256];
    for (int base = -32768; base < 32768; base += 256) {
        for (int i = 0; i < 256; i++)
            all[i] = (int16_t)(base + i);
        lnd_simd.s16_to_f32(all, expanded, 256);
        lnd_simd.f32_to_s16(expanded, back, 256);
        CHECK(memcmp(all, back, sizeof all) == 0);
    }
}

static void test_guarded_buffers(void) {
#if defined(_WIN32)
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    size_t page = system.dwPageSize;
    uint8_t *memory = VirtualAlloc(nullptr, page * 6, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(memory != nullptr);
    if (!memory) return;
    DWORD old;
    for (size_t i = 1; i < 6; i += 2)
        CHECK(VirtualProtect(memory + i * page, page, PAGE_NOACCESS, &old) != 0);
    for (size_t n = 1; n <= 65; n++) {
        float *src = (float *)(memory + page - n * 4);
        int16_t *dst = (int16_t *)(memory + page * 3 - n * 2);
        int16_t expected[65];
        for (size_t i = 0; i < n; i++)
            src[i] = random_sample() * 2;
        lnd_simd_c.f32_to_s16(src, expected, n);
        lnd_simd.f32_to_s16(src, dst, n);
        CHECK(memcmp(dst, expected, n * 2) == 0);
        int16_t *shorts = (int16_t *)(memory + page - n * 2);
        float *expanded = (float *)(memory + page * 3 - n * 4), reference[65];
        for (size_t i = 0; i < n; i++)
            shorts[i] = (int16_t)(i * 719 - 32768);
        lnd_simd_c.s16_to_f32(shorts, reference, n);
        lnd_simd.s16_to_f32(shorts, expanded, n);
        CHECK(memcmp(expanded, reference, n * 4) == 0);
        for (size_t bytes = 2; bytes <= 4; bytes += 2) {
            uint8_t *packed = memory + page - n * bytes * 2;
            uint8_t *left = memory + page * 3 - n * bytes, *right = memory + page * 5 - n * bytes;
            uint8_t saved[65 * 8];
            for (size_t i = 0; i < n * bytes * 2; i++)
                packed[i] = saved[i] = (uint8_t)(i * 79);
            lnd_simd.split_stereo(packed, left, right, n, bytes);
            lnd_simd.join_stereo(left, right, packed, n, bytes);
            CHECK(memcmp(packed, saved, n * bytes * 2) == 0);
        }
    }
    CHECK(VirtualFree(memory, 0, MEM_RELEASE) != 0);
#endif
}

static void test_integer(void) {
    uint8_t input[608], output[608], expected[608];
    for (size_t i = 0; i < sizeof input; i++)
        input[i] = (uint8_t)(i * 73 + i / 17);
    for (int sf = LND_FORMAT_U8; sf <= LND_FORMAT_S32; sf++) {
        for (int df = LND_FORMAT_U8; df <= LND_FORMAT_S32; df++) {
            if (sf == df) continue;
            for (int sl = LND_LAYOUT_INTERLEAVED; sl <= LND_LAYOUT_PLANAR; sl++) {
                for (int dl = LND_LAYOUT_INTERLEAVED; dl <= LND_LAYOUT_PLANAR; dl++) {
                    for (size_t padding = 0; padding <= 3; padding += 3) {
                        void *sp[] = {input + 1, input + 297}, *dp[] = {output + 1, output + 297}, *ep[] = {expected + 1, expected + 297};
                        LND_PCM src = {.data = input + 1,
                                       .planes = sp,
                                       .frames = 37,
                                       .channels = 2,
                                       .format = sf,
                                       .layout = sl,
                                       .stride_bytes = LND_PcmGetSampleBytes(sf) * (sl == LND_LAYOUT_PLANAR ? 1 : 2) + padding};
                        LND_PCM dst = {.data = output + 1,
                                       .planes = dp,
                                       .frames = 37,
                                       .channels = 2,
                                       .format = df,
                                       .layout = dl,
                                       .stride_bytes = LND_PcmGetSampleBytes(df) * (dl == LND_LAYOUT_PLANAR ? 1 : 2) + padding};
                        LND_PCM ref = dst;
                        ref.data = expected + 1;
                        ref.planes = ep;
                        memset(output, 0xa5, sizeof output);
                        memset(expected, 0xa5, sizeof expected);
                        for (uint32_t c = 0; c < 2; c++)
                            for (size_t f = 0; f < 37; f++)
                                lnd_pcm_store_integer(lnd_pcm_at(&ref, c, f), df, lnd_pcm_load_integer(lnd_pcm_at(&src, c, f), sf));
                        CHECK(LND_PcmConvert(&dst, 0, &src, 0, 37) == LND_OK);
                        CHECK(memcmp(output, expected, sizeof output) == 0);
                    }
                }
            }
        }
    }
    uint8_t samples[512], converted[1024], reference[1024];
    for (int base = -32768; base < 32768; base += 256) {
        for (int i = 0; i < 256; i++) {
            uint16_t v = (uint16_t)(base + i);
            samples[i * 2] = (uint8_t)v;
            samples[i * 2 + 1] = (uint8_t)(v >> 8);
        }
        for (int format = LND_FORMAT_U8; format <= LND_FORMAT_S32; format++) {
            if (format == LND_FORMAT_S16) continue;
            size_t bytes = LND_PcmGetSampleBytes(format);
            LND_PCM src = {.data = samples, .frames = 256, .channels = 1, .format = LND_FORMAT_S16};
            LND_PCM dst = {.data = converted, .frames = 256, .channels = 1, .format = format};
            for (size_t i = 0; i < 256; i++)
                lnd_pcm_store_integer(reference + i * bytes, format, (int64_t)(base + (int)i) * 65536);
            CHECK(LND_PcmConvert(&dst, 0, &src, 0, 256) == LND_OK);
            CHECK(memcmp(converted, reference, bytes * 256) == 0);
        }
    }
}

static void test_layout(void) {
    uint8_t input[260 * 8 + 16], output[260 * 8 + 16], left[260 * 4 + 16], right[260 * 4 + 16];
    for (size_t i = 0; i < sizeof input; i++)
        input[i] = (uint8_t)(i * 71);
    for (size_t bytes = 2; bytes <= 4; bytes += 2) {
        for (size_t n = 0; n <= 260; n++) {
            for (size_t align = 0; align < 4; align++) {
                memset(output, 0xa5, sizeof output);
                lnd_simd.split_stereo(input + align, left + align, right + align, n, bytes);
                for (size_t i = 0; i < n; i++) {
                    CHECK(memcmp(left + align + i * bytes, input + align + i * bytes * 2, bytes) == 0);
                    CHECK(memcmp(right + align + i * bytes, input + align + (i * 2 + 1) * bytes, bytes) == 0);
                }
                lnd_simd.join_stereo(left + align, right + align, output + align, n, bytes);
                CHECK(memcmp(input + align, output + align, n * bytes * 2) == 0);
                CHECK(output[align + n * bytes * 2] == 0xa5);
                void *planes[] = {left + align, right + align};
                LND_PCM src = {.data = input + align, .frames = n, .channels = 2, .format = bytes == 2 ? LND_FORMAT_S16 : LND_FORMAT_F32};
                LND_PCM dst = {.planes = planes, .frames = n, .channels = 2, .format = src.format, .layout = LND_LAYOUT_PLANAR};
                CHECK(LND_PcmConvert(&dst, 0, &src, 0, n) == LND_OK);
                LND_PCM packed = src;
                packed.data = output + align;
                CHECK(LND_PcmConvert(&packed, 0, &dst, 0, n) == LND_OK);
                CHECK(memcmp(input + align, output + align, n * bytes * 2) == 0);
            }
        }
    }
}

static void test_math(void) {
    float input[520], expected[520], output[520], base[520];
    const float m[] = {0.25f, -0.125f, 0.75f, 0.5f};
    for (unsigned i = 0; i < 520; i++)
        input[i] = random_sample(), base[i] = random_sample();
    for (size_t n = 0; n < 259; n++) {
        memcpy(output, base, sizeof base);
        memcpy(expected, base, sizeof base);
        lnd_simd.accumulate(output, input, 0.625f, n);
        lnd_simd_c.accumulate(expected, input, 0.625f, n);
        CHECK(memcmp(output, expected, sizeof output) == 0);
        lnd_simd.scale(output, 0.375f, n);
        lnd_simd_c.scale(expected, 0.375f, n);
        CHECK(memcmp(output, expected, sizeof output) == 0);
        memcpy(output, base, sizeof base);
        memcpy(expected, base, sizeof base);
        lnd_simd.matrix_stereo(input, output, n, m);
        lnd_simd_c.matrix_stereo(input, expected, n, m);
        CHECK(memcmp(output, expected, sizeof output) == 0);
    }
    for (unsigned i = 0; i < 520; i++)
        output[i] = expected[i] = i % 9 ? random_sample() * 2 : NAN;
    lnd_simd.clip_hard(output, 520);
    lnd_simd_c.clip_hard(expected, 520);
    for (unsigned i = 0; i < 520; i++)
        CHECK((isnan(output[i]) && isnan(expected[i])) || output[i] == expected[i]);
}

static void check_frame(const float *src, const float *h, uint32_t taps, uint32_t channels) {
    lnd_sinc_frame frame = lnd_simd.sinc_frame_select(taps, channels);
    if (!frame) return;
    float out[99];
    for (unsigned i = 0; i < 99; i++)
        out[i] = 123;
    frame(src, h, out + 1, taps, channels);
    CHECK(out[0] == 123 && out[channels + 1] == 123);
    for (uint32_t c = 0; c < channels; c++) {
        float expected = lnd_sinc_dot_c(src + c, h, taps, channels);
        CHECK(memcmp(&expected, out + c + 1, sizeof expected) == 0);
    }
}

static bool same_float(float a, float b) { return (isnan(a) && isnan(b)) || memcmp(&a, &b, sizeof a) == 0; }

static void test_sinc_special(void) {
    float data[32 * 32], h[32], out[32];
    static const float values[] = {0.0f, -0.0f, 0x1p-149f, -0x1p-126f, 1e20f, -1e20f, INFINITY, -INFINITY, NAN};
    int saved = fegetround();
    const int rounding[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (unsigned round = 0; round < sizeof rounding / sizeof *rounding; round++) {
        CHECK(fesetround(rounding[round]) == 0);
        for (unsigned pattern = 0; pattern < sizeof values / sizeof *values; pattern++) {
            for (unsigned i = 0; i < 32; i++)
                h[i] = pattern < 4 ? 0.5f : (float)((int)(i % 5) - 2) / 8;
            for (unsigned i = 0; i < 32 * 32; i++)
                data[i] = pattern < 4 || i % 7 == 0 ? values[pattern] : (float)((int)(i % 11) - 5);
            for (uint32_t taps = 8; taps <= 32; taps *= 2) {
                for (uint32_t channels = 1; channels <= 32; channels++) {
                    lnd_sinc_dot dot = lnd_simd.sinc_select(taps, channels);
                    float expected = lnd_sinc_dot_c(data, h, taps, channels);
                    CHECK(same_float(expected, dot(data, h, taps, channels)));
                    lnd_sinc_frame frame = lnd_simd.sinc_frame_select(taps, channels);
                    if (!frame) continue;
                    frame(data, h, out, taps, channels);
                    for (uint32_t c = 0; c < channels; c++)
                        CHECK(same_float(lnd_sinc_dot_c(data + c, h, taps, channels), out[c]));
                }
            }
        }
    }
    CHECK(fesetround(saved) == 0);
}

static void test_sinc_dynamic(void) {
    float data[32 * 97 + 4], h[32];
    for (size_t i = 0; i < sizeof data / sizeof *data; i++) data[i] = random_sample();
    for (size_t i = 0; i < sizeof h / sizeof *h; i++) h[i] = random_sample();
    for (uint32_t taps = 8; taps <= 32; taps *= 2) {
        for (uint32_t channels = 33; channels <= 97; channels++) {
            for (uint32_t offset = 0; offset < 4; offset++) {
                check_frame(data + offset, h, taps, channels);
                float expected = lnd_sinc_dot_c(data + offset, h, taps, channels);
                float actual = lnd_simd.sinc_select(taps, channels)(data + offset, h, taps, channels);
                CHECK(memcmp(&expected, &actual, sizeof actual) == 0);
            }
        }
    }
}

static void test_sinc(void) {
    test_sinc_special();
    test_sinc_dynamic();
    float h[35], data[35 * 32 + 16];
    for (unsigned i = 0; i < 35; i++)
        h[i] = random_sample();
    for (unsigned i = 0; i < 35 * 32 + 16; i++)
        data[i] = random_sample();
    for (uint32_t taps = 0; taps <= 35; taps++) {
        for (uint32_t stride = 0; stride <= 32; stride++) {
            for (uint32_t offset = 0; offset < 4; offset++) {
                float a = lnd_sinc_dot_c(data + offset, h, taps, stride);
                float b = lnd_simd.sinc(data + offset, h, taps, stride);
                CHECK(memcmp(&a, &b, sizeof a) == 0);
                double accurate = 0;
                for (uint32_t t = 0; t < taps; t++)
                    accurate += (double)data[offset + t * stride] * h[t];
                CHECK(fabs((double)b - accurate) < 0.000004);
                float selected = lnd_simd.sinc_select(taps, stride)(data + offset, h, taps, stride);
                CHECK(memcmp(&selected, &a, sizeof a) == 0);
                check_frame(data + offset, h, taps, stride);
            }
        }
    }
#if defined(_WIN32)
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    size_t page = system.dwPageSize;
    uint8_t *memory = VirtualAlloc(nullptr, page * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(memory != nullptr);
    if (memory) {
        DWORD old;
        CHECK(VirtualProtect(memory + page, page, PAGE_NOACCESS, &old) != 0);
        for (uint32_t taps = 1; taps <= 32; taps++) {
            for (uint32_t stride = 1; stride <= 16; stride++) {
                float *src = (float *)(memory + page - ((size_t)(taps - 1) * stride + 1) * sizeof(float));
                for (uint32_t t = 0; t < taps; t++)
                    src[t * stride] = (float)t / 32;
                float a = lnd_sinc_dot_c(src, h, taps, stride), b = lnd_simd.sinc(src, h, taps, stride);
                CHECK(memcmp(&a, &b, sizeof a) == 0);
            }
        }
        for (uint32_t taps = 8; taps <= 32; taps *= 2) {
            for (uint32_t channels = 1; channels <= 32; channels++) {
                float *src = (float *)(memory + page - (size_t)taps * channels * sizeof(float));
                for (uint32_t t = 0; t < taps * channels; t++)
                    src[t] = (float)(t % 17) / 32;
                check_frame(src, h, taps, channels);
            }
        }
        CHECK(VirtualFree(memory, 0, MEM_RELEASE) != 0);

        uint8_t *edges = VirtualAlloc(nullptr, page * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        CHECK(edges != nullptr);
        if (edges) {
            CHECK(VirtualProtect(edges + page, page, PAGE_NOACCESS, &old) != 0);
            CHECK(VirtualProtect(edges + page * 3, page, PAGE_NOACCESS, &old) != 0);
            for (uint32_t taps = 8; taps <= 32; taps *= 2) {
                float *coeff = (float *)(edges + page - taps * sizeof(float));
                memcpy(coeff, h, taps * sizeof(float));
                for (uint32_t channels = 1; channels <= 32; channels++) {
                    float *out = (float *)(edges + page * 3 - channels * sizeof(float));
                    float a = lnd_sinc_dot_c(data, coeff, taps, channels);
                    float b = lnd_simd.sinc_select(taps, channels)(data, coeff, taps, channels);
                    CHECK(memcmp(&a, &b, sizeof a) == 0);
                    lnd_sinc_frame frame = lnd_simd.sinc_frame_select(taps, channels);
                    if (!frame) continue;
                    frame(data, coeff, out, taps, channels);
                    for (uint32_t c = 0; c < channels; c++) {
                        float expected = lnd_sinc_dot_c(data + c, coeff, taps, channels);
                        CHECK(memcmp(&expected, out + c, sizeof expected) == 0);
                    }
                }
            }
            CHECK(VirtualFree(edges, 0, MEM_RELEASE) != 0);
        }
    }
#endif
}

int main(void) {
    for (int mode = LND_SIMD_AUTO; mode <= LND_SIMD_NEON; mode++) {
        CHECK(LND_ConfigSet(LND_CFG_AUDIO_SIMD, mode) == LND_OK);
#if LND_MODULE_SINC_ASM
        CHECK(LND_ConfigSet(LND_CFG_SINC_ASM_ENABLED, 0) == LND_OK);
#endif
        begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
        printf("SIMD %d: %s, sinc %s\n", mode, lnd_simd.name, lnd_simd.sinc_name);
#if LND_MODULE_SIMD
        CHECK(strcmp(LND_SimdGetName(), lnd_simd.name) == 0);
        CHECK(strcmp(LND_SimdGetSincName(), lnd_simd.sinc_name) == 0);
#endif
        if (mode == LND_SIMD_NONE) CHECK(strcmp(lnd_simd.name, "c") == 0 && strcmp(lnd_simd.sinc_name, "c") == 0);
        deny_alloc = true;
        const int rounding[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
        int saved_rounding = fegetround();
        for (size_t i = 0; i < sizeof rounding / sizeof *rounding; i++) {
            CHECK(fesetround(rounding[i]) == 0);
            test_convert();
        }
        CHECK(fesetround(saved_rounding) == 0);
        test_guarded_buffers();
        test_integer();
        test_layout();
        test_math();
        test_sinc();
        finish();
    }
#if LND_MODULE_SINC_ASM
    CHECK(LND_ConfigSet(LND_CFG_AUDIO_SIMD, LND_SIMD_NONE) == LND_OK);
    CHECK(LND_ConfigSet(LND_CFG_SINC_ASM_ENABLED, 1) == LND_OK);
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    CHECK(strcmp(LND_SincAsmGetName(), "c") == 0);
    deny_alloc = true;
    test_sinc();
    finish();
    for (int mode = LND_SIMD_AUTO; mode <= LND_SIMD_NEON; mode++) {
        CHECK(LND_ConfigSet(LND_CFG_AUDIO_SIMD, mode) == LND_OK);
        CHECK(LND_ConfigSet(LND_CFG_SINC_ASM_ENABLED, 1) == LND_OK);
        begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
        printf("sinc assembly %d: %s\n", mode, LND_SincAsmGetName());
        if (mode == LND_SIMD_AUTO) CHECK(LND_SincAsmIsAvailable() == (strncmp(LND_SincAsmGetName(), "asm-", 4) == 0));
        if (mode == LND_SIMD_NONE) CHECK(strcmp(LND_SincAsmGetName(), "c") == 0);
        deny_alloc = true;
        test_sinc();
        finish();
    }
#endif
    return report();
}
