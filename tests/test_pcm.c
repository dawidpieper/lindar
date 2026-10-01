#include "lindar.h"
#include "lnd_modules.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        checks++;                                                                                                                                              \
        if (!(x)) {                                                                                                                                            \
            failures++;                                                                                                                                        \
            printf("%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                                     \
        }                                                                                                                                                      \
    } while (0)
#define NEAR(a, b, e) CHECK(fabs((double)(a) - (double)(b)) <= (e))

static void test_formats(void) {
    int32_t values[] = {INT32_MIN, -1610612736, -1073741824, 0, 536870912, 1073741824, 1610612736, 0};
    LND_PCM input = {.data = values, .frames = 4, .channels = 2, .format = LND_FORMAT_S32};
    for (int32_t sf = LND_FORMAT_U8; sf <= (LND_MODULE_PCM_FLOAT ? LND_FORMAT_F64 : LND_FORMAT_S32); sf++) {
        for (int sl = 0; sl <= 1; sl++) {
            unsigned char store[2][80];
            memset(store, 0x55, sizeof store);
            void *planes[] = {store[0] + 1, store[1] + 1};
            LND_PCM src = {.data = store[0] + 1, .planes = planes, .frames = 4, .channels = 2, .format = sf, .layout = sl};
            CHECK(LND_PcmConvert(&src, 0, &input, 0, 4) == LND_OK);
            for (int32_t df = LND_FORMAT_U8; df <= (LND_MODULE_PCM_FLOAT ? LND_FORMAT_F64 : LND_FORMAT_S32); df++) {
                for (int dl = 0; dl <= 1; dl++) {
                    unsigned char dest[2][100];
                    memset(dest, 0xaa, sizeof dest);
                    void *outs[] = {dest[0] + 1, dest[1] + 1};
                    LND_PCM dst = {.data = dest[0] + 1, .planes = outs, .frames = 6, .channels = 2, .format = df, .layout = dl};
                    CHECK(LND_PcmConvert(&dst, 1, &src, 0, 4) == LND_OK);
                    int32_t actual[8] = {0};
                    LND_PCM back = {.data = actual, .frames = 4, .channels = 2, .format = LND_FORMAT_S32};
                    CHECK(LND_PcmConvert(&back, 0, &dst, 1, 4) == LND_OK);
                    for (unsigned k = 0; k < 8; k++)
                        CHECK(actual[k] == values[k]);
                    CHECK(dest[0][0] == 0xaa && dest[1][0] == 0xaa);
                    size_t width = LND_PcmGetSampleBytes(df) * (dl ? 1 : 2);
                    for (size_t k = 1; k <= width; k++)
                        CHECK(dest[0][k] == 0xaa);
                    for (size_t k = 1 + 5 * width; k <= 6 * width; k++)
                        CHECK(dest[0][k] == 0xaa);
                }
            }
        }
    }
}

static void test_bounds(void) {
    unsigned char data[20];
    memset(data, 0x33, sizeof data);
    LND_PCM pcm = {.data = data + 1, .frames = 4, .stride_bytes = 4, .channels = 1, .format = LND_FORMAT_S16};
    CHECK(LND_PcmSilence(&pcm, 1, 2) == LND_OK);
    for (size_t i = 0; i < sizeof data; i++)
        CHECK(data[i] == ((i == 5 || i == 6 || i == 9 || i == 10) ? 0 : 0x33));
    unsigned char before[20];
    memcpy(before, data, sizeof data);
    CHECK(LND_PcmSilence(&pcm, 3, 2) == LND_ERR_INVALID_ARG);
    CHECK(LND_PcmSilence(&pcm, SIZE_MAX, 1) == LND_ERR_INVALID_ARG);
    CHECK(memcmp(data, before, sizeof data) == 0);
    CHECK(LND_PcmSilence(&pcm, 4, 0) == LND_OK);
    pcm.stride_bytes = 1;
    CHECK(LND_PcmValidate(&pcm) == LND_ERR_INVALID_ARG);
    pcm.stride_bytes = SIZE_MAX;
    CHECK(LND_PcmValidate(&pcm) == LND_ERR_INVALID_ARG);
    pcm = (LND_PCM){.channels = 2, .format = LND_FORMAT_U8};
    CHECK(LND_PcmSilence(&pcm, 0, 0) == LND_OK);
    pcm.frames = 1;
    CHECK(LND_PcmValidate(&pcm) == LND_ERR_INVALID_ARG);
    CHECK(LND_PcmValidate(nullptr) == LND_ERR_INVALID_ARG);
    CHECK(LND_PcmGetSampleBytes(-1) == 0 && LND_PcmGetSampleBytes(7) == 0);
    void *planes[] = {data, nullptr};
    pcm = (LND_PCM){.planes = planes, .frames = 2, .channels = 2, .format = LND_FORMAT_S16, .layout = LND_LAYOUT_PLANAR};
    CHECK(LND_PcmValidate(&pcm) == LND_ERR_INVALID_ARG);
}

static void test_integer_precision(void) {
    unsigned char exact[] = {0x01, 0x00, 0x00, 0x40, 0xff, 0xff, 0xff, 0x7f, 0x01, 0x00, 0x00, 0x80};
    unsigned char plane[12];
    void *planes[] = {plane};
    LND_PCM src = {.data = exact, .frames = 3, .channels = 1, .format = LND_FORMAT_S32};
    LND_PCM dst = {.planes = planes, .frames = 3, .channels = 1, .format = LND_FORMAT_S32, .layout = LND_LAYOUT_PLANAR};
    CHECK(LND_PcmConvert(&dst, 0, &src, 0, 3) == LND_OK);
    CHECK(memcmp(exact, plane, sizeof exact) == 0);
#if LND_MODULE_PCM_FLOAT
    double x[3] = {0};
    LND_PCM fp = {.data = x, .frames = 3, .channels = 1, .format = LND_FORMAT_F64};
    CHECK(LND_PcmConvert(&fp, 0, &src, 0, 3) == LND_OK);
    CHECK(x[0] == 1073741825.0 / 2147483648.0);
    CHECK(x[1] == 2147483647.0 / 2147483648.0);
    CHECK(x[2] == -2147483647.0 / 2147483648.0);
    double clips[] = {-INFINITY, -1.0, -0.5, NAN, 0.5, 1.0, INFINITY};
    unsigned char result[7];
    src = (LND_PCM){.data = clips, .frames = 7, .channels = 1, .format = LND_FORMAT_F64};
    dst = (LND_PCM){.data = result, .frames = 7, .channels = 1, .format = LND_FORMAT_U8};
    unsigned char expected[] = {0, 0, 64, 128, 192, 255, 255};
    CHECK(LND_PcmConvert(&dst, 0, &src, 0, 7) == LND_OK);
    CHECK(memcmp(result, expected, sizeof result) == 0);
    CHECK(LND_PcmSilence(&dst, 0, 7) == LND_OK);
    for (unsigned k = 0; k < 7; k++)
        CHECK(result[k] == 128);
#endif
}

static void test_overlap(void) {
    uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    LND_PCM pcm = {.data = data, .frames = 8, .channels = 1, .format = LND_FORMAT_U8};
    CHECK(LND_PcmConvert(&pcm, 2, &pcm, 0, 6) == LND_OK);
    uint8_t expected[] = {1, 2, 1, 2, 3, 4, 5, 6};
    CHECK(memcmp(data, expected, sizeof data) == 0);
    LND_PCM dst = {.data = data, .frames = 4, .channels = 1, .format = LND_FORMAT_S16};
    CHECK(LND_PcmConvert(&dst, 0, &pcm, 0, 4) == LND_ERR_INVALID_ARG);
    CHECK(memcmp(data, expected, sizeof data) == 0);
}

static void test_planar_aliases(void) {
    unsigned char data[512];
    for (int format = LND_FORMAT_U8; format <= (LND_MODULE_PCM_FLOAT ? LND_FORMAT_F64 : LND_FORMAT_S32); format++) {
        size_t bytes = LND_PcmGetSampleBytes(format);
        for (size_t frames = 0; frames <= 8; frames++)
            for (size_t stride = bytes; stride <= bytes + 6; stride++)
                for (size_t delta = 0; delta <= 120; delta++) {
                    void *planes[] = {data, data + delta};
                    LND_PCM pcm = {.planes = planes, .frames = frames + 2, .stride_bytes = stride, .channels = 2, .format = format, .layout = LND_LAYOUT_PLANAR};
                    bool overlap = false;
                    for (size_t a = 0; a < frames; a++)
                        for (size_t b = 0; b < frames; b++)
                            if (a * stride < delta + b * stride + bytes && delta + b * stride < a * stride + bytes) overlap = true;
                    CHECK(LND_PcmSilence(&pcm, 1, frames) == (overlap ? LND_ERR_INVALID_ARG : LND_OK));
                }
    }
}

static int64_t quantize(int64_t value, unsigned bytes) {
    int64_t unit = INT64_C(1) << (32 - bytes * 8);
    int64_t limit = INT64_C(1) << (bytes * 8 - 1);
    value = value >= 0 ? (value + unit / 2) / unit : -((-value + unit / 2) / unit);
    return value < -limit ? -limit : value >= limit ? limit - 1 : value;
}

static void test_quantization(void) {
    int32_t samples[259] = {INT32_MIN, INT32_MAX, 0};
    uint32_t random = 173;
    for (unsigned i = 3; i < 259; i++) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        samples[i] = (int32_t)random;
    }
    for (unsigned bits = 8, i = 3; bits <= 24; bits += 8) {
        int32_t half = (int32_t)(UINT32_C(1) << (31 - bits));
        for (int delta = -1; delta <= 1; delta++) {
            samples[i++] = half + delta;
            samples[i++] = -half + delta;
        }
    }
    uint8_t input[259 * 4 + 1], output[259 * 7 + 2], expected[sizeof output];
    for (int32_t sf = LND_FORMAT_U8; sf <= LND_FORMAT_S32; sf++) {
        unsigned sb = (unsigned)LND_PcmGetSampleBytes(sf);
        for (unsigned i = 0; i < 259; i++) {
            uint32_t sample = (uint32_t)(quantize(samples[i], sb) + (sf == LND_FORMAT_U8 ? 128 : 0));
            for (unsigned b = 0; b < sb; b++) input[1 + i * sb + b] = (uint8_t)(sample >> (8 * b));
        }
        LND_PCM src = {.data = input + 1, .frames = 259, .channels = 1, .format = sf};
        for (int32_t df = LND_FORMAT_U8; df <= LND_FORMAT_S32; df++) {
            unsigned db = (unsigned)LND_PcmGetSampleBytes(df);
            for (unsigned padding = 0; padding <= 3; padding += 3) {
                memset(output, 0xa5, sizeof output);
                memset(expected, 0xa5, sizeof expected);
                LND_PCM dst = {.data = output + 1, .frames = 259, .stride_bytes = db + padding, .channels = 1, .format = df};
                for (unsigned i = 0; i < 259; i++) {
                    int64_t value = quantize(samples[i], sb) * (INT64_C(1) << (32 - 8 * sb));
                    uint32_t sample = (uint32_t)(quantize(value, db) + (df == LND_FORMAT_U8 ? 128 : 0));
                    for (unsigned b = 0; b < db; b++) expected[1 + i * dst.stride_bytes + b] = (uint8_t)(sample >> (8 * b));
                }
                CHECK(LND_PcmConvert(&dst, 0, &src, 0, 259) == LND_OK);
                CHECK(!memcmp(output, expected, sizeof output));
            }
        }
    }
}

static void test_copy_layouts(void) {
    unsigned char input[32][400], output[32][400], expected[32][400];
    void *in_planes[32], *out_planes[32];
    for (unsigned c = 0; c < 32; c++) {
        in_planes[c] = input[c] + 1;
        out_planes[c] = output[c] + 1;
        for (unsigned i = 0; i < 400; i++) input[c][i] = (unsigned char)(c * 71 + i * 13);
    }
    const unsigned channels[] = {1, 2, 3, 4, 6, 8, 16, 32}, counts[] = {0, 1, 7, 17};
    for (int format = LND_FORMAT_U8; format <= LND_FORMAT_F64; format++)
        for (unsigned ch = 0; ch < sizeof channels / sizeof *channels; ch++)
            for (unsigned sl = 0; sl < 2; sl++)
                for (unsigned dl = 0; dl < 2; dl++)
                    for (unsigned padding = 0; padding <= 3; padding += 3)
                        for (unsigned n = 0; n < sizeof counts / sizeof *counts; n++) {
                            unsigned bytes = (unsigned)LND_PcmGetSampleBytes(format), count = counts[n], nc = channels[ch];
                            size_t ss = bytes * (sl ? 1 : nc) + padding, ds = bytes * (dl ? 1 : nc) + padding;
                            LND_PCM src = {.data = input[0] + 1, .planes = in_planes, .frames = 20, .stride_bytes = ss,
                                           .channels = nc, .format = format, .layout = sl};
                            LND_PCM dst = {.data = output[0] + 1, .planes = out_planes, .frames = 20, .stride_bytes = ds,
                                           .channels = nc, .format = format, .layout = dl};
                            memset(output, 0x55, sizeof output);
                            memset(expected, 0x55, sizeof expected);
                            for (unsigned f = 0; f < count; f++)
                                for (unsigned c = 0; c < nc; c++)
                                    for (unsigned b = 0; b < bytes; b++) {
                                        size_t at = (sl ? c * 400 : c * bytes) + 1 + (f + 2) * ss + b;
                                        size_t to = (dl ? c * 400 : c * bytes) + 1 + (f + 1) * ds + b;
                                        ((unsigned char *)expected)[to] = ((unsigned char *)input)[at];
                                    }
                            CHECK(LND_PcmConvert(&dst, 1, &src, 2, count) == LND_OK);
                            CHECK(memcmp(output, expected, sizeof output) == 0);
                        }
}

int main(void) {
    test_quantization();
    test_formats();
    test_copy_layouts();
    test_bounds();
    test_integer_precision();
    test_overlap();
    test_planar_aliases();
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
