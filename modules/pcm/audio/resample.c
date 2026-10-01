#include "src/alloc.h"
#include "src/config.h"
#include "src/pcm.h"
#include "src/spinlock.h"
#include "source.h"
#include "simd.h"

#include <math.h>
#include <string.h>

#define LND_SINC_PHASES 512
#define LND_KAISER_BETA 6.0
#define LND_PI 3.14159265358979323846

typedef struct lnd_sinc_bank {
    struct lnd_sinc_bank *next;
    double cutoff;
    size_t references;
    uint32_t taps;
    alignas(LND_CACHE_LINE) float values[];
} lnd_sinc_bank;

static lnd_sinc_bank *lnd_sinc_banks;
static lnd_spinlock lnd_sinc_lock;

typedef struct lnd_resample_source {
    lnd_source base;
    lnd_source *inner;
    lnd_sinc_dot dot;
    lnd_sinc_frame frame;
    bool owns_inner;
    double step;
    uint32_t quality;
    uint32_t taps;
    uint32_t half;
    float *table;
    lnd_sinc_bank *bank;
    float *in;
    uint32_t in_cap;
    uint32_t in_count;
    double pos;
    bool ended;
    bool flushed;
    double frac;
    uint32_t count;
    uint32_t index;
    float *scratch;
    uint32_t scratch_frames;
    bool live;
    bool variable;
    double travel;
    double origin;
    uint64_t ticks;
    uint64_t discarded;
    double phases[8];
    uint32_t cached;
    float last[LND_MAX_CHANNELS];
    LND_PCM input;
    void *planes[LND_MAX_CHANNELS];
    uint32_t stride_samples, spacing;
} lnd_resample_source;

bool lnd_resample_source_drained(const lnd_source *source, uint64_t frames_after_end) {
    const lnd_resample_source *s = (const lnd_resample_source *)source;
    double pending = (double)s->inner->pos - (double)source->pos * s->step + s->taps + 1;
    return (double)frames_after_end >= pending;
}

static double lnd_bessel_i0(double x) {
    double sum = 1.0, term = 1.0, h = x * 0.5;
    for (int k = 1; k < 64; k++) {
        term *= (h / k) * (h / k);
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}

static void lnd_sinc_table(float *table, uint32_t taps, double cutoff) {
    uint32_t half = taps / 2;
    double i0b = lnd_bessel_i0(LND_KAISER_BETA);
    for (uint32_t p = 0; p < LND_SINC_PHASES; p++) {
        double fp = (double)p / LND_SINC_PHASES;
        double sum = 0.0;
        float *row = table + (size_t)p * taps;
        for (uint32_t t = 0; t < taps; t++) {
            double x = ((double)t - (double)half + 1.0) - fp;
            double r = x / (double)half;
            double w = fabs(r) >= 1.0 ? 0.0 : lnd_bessel_i0(LND_KAISER_BETA * sqrt(1.0 - r * r)) / i0b;
            double y = cutoff * x;
            double s = y == 0.0 ? 1.0 : sin(LND_PI * y) / (LND_PI * y);
            double v = cutoff * s * w;
            row[t] = (float)v;
            sum += v;
        }
        float k = sum != 0.0 ? (float)(1.0 / sum) : 1.0f;
        for (uint32_t t = 0; t < taps; t++)
            row[t] *= k;
    }
}

static lnd_sinc_bank *lnd_sinc_find(uint32_t taps, double cutoff) {
    for (lnd_sinc_bank *p = lnd_sinc_banks; p; p = p->next)
        if (p->taps == taps && p->cutoff == cutoff) return p;
    return nullptr;
}

static lnd_sinc_bank *lnd_sinc_acquire(uint32_t taps, double cutoff) {
    lnd_spinlock_lock(&lnd_sinc_lock);
    lnd_sinc_bank *bank = lnd_sinc_find(taps, cutoff);
    if (bank) bank->references++;
    lnd_spinlock_unlock(&lnd_sinc_lock);
    if (bank) return bank;
    lnd_sinc_bank *fresh = lnd_alloc_aligned(sizeof *fresh + (size_t)LND_SINC_PHASES * taps * sizeof(float), LND_CACHE_LINE);
    if (!fresh) return nullptr;
    fresh->cutoff = cutoff;
    fresh->taps = taps;
    fresh->references = 1;
    lnd_sinc_table(fresh->values, taps, cutoff);
    lnd_spinlock_lock(&lnd_sinc_lock);
    bank = lnd_sinc_find(taps, cutoff);
    if (bank)
        bank->references++;
    else {
        fresh->next = lnd_sinc_banks;
        lnd_sinc_banks = fresh;
    }
    lnd_spinlock_unlock(&lnd_sinc_lock);
    if (bank) lnd_free_aligned(fresh);
    return bank ? bank : fresh;
}

static void lnd_sinc_release(lnd_sinc_bank *bank) {
    lnd_spinlock_lock(&lnd_sinc_lock);
    bool last = --bank->references == 0;
    if (last)
        for (lnd_sinc_bank **p = &lnd_sinc_banks; *p; p = &(*p)->next)
            if (*p == bank) {
                *p = bank->next;
                break;
            }
    lnd_spinlock_unlock(&lnd_sinc_lock);
    if (last) lnd_free_aligned(bank);
}

static void lnd_sinc_reset(lnd_resample_source *s) {
    uint32_t ch = s->base.channels;
    s->in_count = s->half - 1;
    if (s->stride_samples == 1) {
        for (uint32_t c = 0; c < ch; c++)
            memset(s->in + (size_t)c * s->spacing, 0, (size_t)s->in_count * sizeof(float));
    } else
        memset(s->in, 0, (size_t)s->in_count * ch * sizeof(float));
    s->pos = (double)(s->half - 1);
    s->discarded = 0;
    s->ended = false;
    s->flushed = false;
}

static bool lnd_sinc_refill(lnd_resample_source *s) {
    uint32_t ch = s->base.channels;
    double discard = floor(s->pos) - (s->half - 1);
    uint32_t keep_from = discard >= s->in_count ? s->in_count : (uint32_t)discard;
    if (keep_from > 0) {
        uint32_t planes = s->stride_samples == 1 ? ch : 1;
        for (uint32_t c = 0; c < planes; c++) {
            float *p = s->in + (size_t)c * s->spacing;
            memmove(p, p + (size_t)keep_from * s->stride_samples, (size_t)(s->in_count - keep_from) * s->stride_samples * sizeof(float));
        }
        s->in_count -= keep_from;
        s->pos -= (double)keep_from;
        s->discarded += keep_from;
    }
    if (s->ended) {
        if (s->flushed) return false;
        LND_PcmSilence(&s->input, s->in_count, s->half);
        s->in_count += s->half;
        s->flushed = true;
        return true;
    }
    uint32_t space = s->in_cap - s->half - s->in_count;
    int64_t read = lnd_source_read_pcm(s->inner, &s->input, s->in_count, space, false);
    if (read < 0) {
        lnd_store(&s->inner->status, (int32_t)read);
        return false;
    }
    uint32_t got = (uint32_t)read;
    s->in_count += got;
    if ((s->live || s->inner->live) && lnd_source_status(s->inner) != LND_SOURCE_EOF) return got > 0;
    if (got < space) s->ended = true;
    if (got == 0) {
        LND_PcmSilence(&s->input, s->in_count, s->half);
        s->in_count += s->half;
        s->flushed = true;
    }
    return true;
}

static void lnd_sinc_variable_table(float *table, uint32_t half) {
    uint32_t points = half * 256;
    double i0b = lnd_bessel_i0(LND_KAISER_BETA);
    for (uint32_t i = 0; i <= points; i++) {
        double x = (double)i / 256, r = (double)i / points;
        table[i] = (float)(i ? sin(LND_PI * x) / (LND_PI * x) : 1);
        table[points + 1 + i] = (float)(r < 1 ? lnd_bessel_i0(LND_KAISER_BETA * sqrt(1 - r * r)) / i0b : 0);
    }
}

static float lnd_sinc_lookup(const float *table, float at, uint32_t points) {
    uint32_t index = (uint32_t)at;
    if (index >= points) return table[points];
    return table[index] + (table[index + 1] - table[index]) * (at - index);
}

static const float *lnd_sinc_coefficients(lnd_resample_source *s, double phase) {
    uint32_t points = s->half * 256, slot = (uint32_t)(phase * 8);
    float *coefficients = s->table + (points + 1) * 2 + slot * s->taps;
    if ((s->cached & (1u << slot)) && s->phases[slot] == phase) return coefficients;
    s->phases[slot] = phase;
    s->cached |= 1u << slot;
    const float *window = s->table + points + 1;
    float cutoff = (float)(0.95 / LND_MAX(s->step, 1.0)), sum = 0;
    for (uint32_t t = 0; t < s->taps; t++) {
        float x = (float)fabs((double)t - s->half + 1 - phase) * 256;
        float value = cutoff * lnd_sinc_lookup(s->table, cutoff * x, points) * lnd_sinc_lookup(window, x, points);
        coefficients[t] = value;
        sum += value;
    }
    float scale = sum != 0 ? 1 / sum : 1;
    for (uint32_t t = 0; t < s->taps; t++)
        coefficients[t] *= scale;
    return coefficients;
}

static int64_t lnd_sinc_read_pcm(lnd_source *src, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_resample_source *s = (lnd_resample_source *)src;
    uint32_t ch = src->channels;
    uint32_t taps = s->taps;
    if (!lnd_pcm_writable(pcm, offset, frames) || pcm->channels != ch || frames > (size_t)INT64_MAX) return LND_ERR_INVALID_ARG;
    if (!frames) return 0;
    uint8_t *output[LND_MAX_CHANNELS];
    for (uint32_t c = 0; c < ch; c++)
        output[c] = lnd_pcm_at(pcm, c, offset);
    size_t output_stride = lnd_pcm_stride(pcm);
    bool packed = pcm->format == LND_FORMAT_F32 && pcm->layout == LND_LAYOUT_INTERLEAVED && output_stride == ch * sizeof(float) &&
                  (uintptr_t)output[0] % alignof(float) == 0;
    uint64_t produced = 0;
    while (produced < frames) {
        if (s->variable && lnd_source_status(s->inner) == LND_SOURCE_EOF && s->travel >= s->inner->pos) break;
        if (s->pos + s->half >= s->in_count) {
            if (!lnd_sinc_refill(s)) break;
            continue;
        }
        double fpos = floor(s->pos);
        uint32_t i0 = (uint32_t)fpos;
        if (s->variable && s->step == 1.0 && s->pos == fpos) {
            uint64_t count = LND_MIN(frames - produced, s->in_count - s->half - i0);
            if (lnd_source_status(s->inner) == LND_SOURCE_EOF) count = LND_MIN(count, s->inner->pos - (uint64_t)s->travel);
            int32_t result = LND_PcmConvert(pcm, offset + (size_t)produced, &s->input, i0, (size_t)count);
            if (result != LND_OK) {
                lnd_store(&src->status, result);
                break;
            }
            s->pos += (double)count;
            s->ticks += count;
            s->travel = s->origin + (double)s->ticks * s->step;
            produced += count;
            continue;
        }
        uint32_t phase = (uint32_t)((s->pos - fpos) * LND_SINC_PHASES);
        if (phase >= LND_SINC_PHASES) phase = LND_SINC_PHASES - 1;
        const float *h = s->variable ? lnd_sinc_coefficients(s, s->pos - fpos) : s->table + (size_t)phase * taps;
        const float *base = s->in + (size_t)(i0 - s->half + 1) * s->stride_samples;
        if (s->frame && packed)
            s->frame(base, h, (float *)(output[0] + produced * output_stride), taps, ch);
        else
            for (uint32_t c = 0; c < ch; c++) {
                float value = s->dot(base + (size_t)c * s->spacing, h, taps, s->stride_samples);
                uint8_t *out = output[c] + produced * output_stride;
                if (pcm->format == LND_FORMAT_F32)
                    memcpy(out, &value, sizeof value);
                else
                    lnd_pcm_store_sample(out, pcm->format, value);
            }
        produced++;
        s->travel = s->origin + (double)++s->ticks * s->step;
        s->pos = (s->half - 1 + s->travel) - (double)s->discarded;
    }
    lnd_store_relaxed(&src->pos, lnd_load_relaxed(&src->pos) + produced);
    int32_t status = lnd_source_status(s->inner);
    if ((s->variable && status == LND_SOURCE_EOF && s->travel >= s->inner->pos) || (produced < frames && (status == LND_SOURCE_EOF || status < 0)))
        lnd_store(&src->status, status);
    return produced;
}

static bool lnd_linear_refill(lnd_resample_source *s) {
    int64_t got = lnd_source_read_pcm(s->inner, &s->input, 0, s->scratch_frames, false);
    if (got < 0) lnd_store(&s->inner->status, (int32_t)got);
    s->count = got > 0 ? (uint32_t)got : 0;
    s->index = 0;
    return s->count != 0;
}

void lnd_resample_source_set_live(lnd_source *resampler, bool live) {
    ((lnd_resample_source *)resampler)->live = live;
    ((lnd_resample_source *)resampler)->inner->live = live;
    resampler->live = live;
}

static int64_t lnd_linear_read_pcm(lnd_source *src, const LND_PCM *pcm, size_t offset, size_t frames) {
    lnd_resample_source *s = (lnd_resample_source *)src;
    uint32_t ch = src->channels;
    if (!lnd_pcm_writable(pcm, offset, frames) || pcm->channels != ch || frames > (size_t)INT64_MAX) return LND_ERR_INVALID_ARG;
    if (!frames) return 0;
    uint8_t *output[LND_MAX_CHANNELS];
    for (uint32_t c = 0; c < ch; c++)
        output[c] = lnd_pcm_at(pcm, c, offset);
    size_t output_stride = lnd_pcm_stride(pcm);
    uint64_t produced = 0;
    while (produced < frames) {
        if (s->variable && lnd_source_status(s->inner) == LND_SOURCE_EOF && s->travel >= s->inner->pos) break;
        while (s->frac >= 1.0) {
            if (s->index >= s->count && !lnd_linear_refill(s)) goto done;
            s->frac -= 1.0;
            for (uint32_t c = 0; c < ch; c++)
                s->last[c] = s->scratch[(size_t)s->index * s->stride_samples + (size_t)c * s->spacing];
            s->index++;
        }
        const float *next = s->scratch + (size_t)s->index * s->stride_samples;
        uint32_t spacing = s->spacing;
        if (s->index >= s->count) {
            if (lnd_linear_refill(s))
                next = s->scratch;
            else if (s->variable && lnd_source_status(s->inner) == LND_SOURCE_EOF) {
                next = s->last;
                spacing = 1;
            } else
                goto done;
        }
        float f = (float)s->frac;
        for (uint32_t c = 0; c < ch; c++) {
            float value = s->last[c] + (next[(size_t)c * spacing] - s->last[c]) * f;
            uint8_t *out = output[c] + produced * output_stride;
            if (pcm->format == LND_FORMAT_F32)
                memcpy(out, &value, sizeof value);
            else
                lnd_pcm_store_sample(out, pcm->format, value);
        }
        produced++;
        s->frac += s->step;
        if (s->variable) s->travel = s->origin + (double)++s->ticks * s->step;
    }
done:
    lnd_store_relaxed(&src->pos, lnd_load_relaxed(&src->pos) + produced);
    int32_t status = lnd_source_status(s->inner);
    if ((s->variable && status == LND_SOURCE_EOF && s->travel >= s->inner->pos) || (produced < frames && (status == LND_SOURCE_EOF || status < 0)))
        lnd_store(&src->status, status);
    return produced;
}

static uint64_t lnd_sinc_read(lnd_source *src, float *dst, uint64_t frames) {
    if (frames > SIZE_MAX) {
        lnd_store(&src->status, LND_ERR_INVALID_ARG);
        return 0;
    }
    LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = src->channels, .format = LND_FORMAT_F32};
    int64_t got = lnd_sinc_read_pcm(src, &pcm, 0, (size_t)frames);
    if (got < 0) lnd_store(&src->status, (int32_t)got);
    return got > 0 ? (uint64_t)got : 0;
}

static uint64_t lnd_linear_read(lnd_source *src, float *dst, uint64_t frames) {
    if (frames > SIZE_MAX) {
        lnd_store(&src->status, LND_ERR_INVALID_ARG);
        return 0;
    }
    LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = src->channels, .format = LND_FORMAT_F32};
    int64_t got = lnd_linear_read_pcm(src, &pcm, 0, (size_t)frames);
    if (got < 0) lnd_store(&src->status, (int32_t)got);
    return got > 0 ? (uint64_t)got : 0;
}

void lnd_resample_source_reset(lnd_source *src) {
    lnd_resample_source *s = (lnd_resample_source *)src;
    if (s->table)
        lnd_sinc_reset(s);
    else {
        s->frac = 1.0;
        s->count = s->index = 0;
        memset(s->last, 0, sizeof s->last);
    }
    src->deferred_error = s->inner->deferred_error = LND_OK;
    lnd_store(&src->status, LND_SOURCE_READY);
    lnd_store(&s->inner->status, LND_SOURCE_READY);
    src->pos = 0;
    s->inner->pos = 0;
    s->travel = s->origin = 0;
    s->ticks = 0;
}

static int32_t lnd_resample_seek(lnd_source *src, uint64_t frame) {
    lnd_resample_source *s = (lnd_resample_source *)src;
    int32_t r = lnd_source_seek(s->inner, frame);
    if (r != LND_OK) return r;
    uint64_t pos = s->inner->pos;
    lnd_resample_source_reset(src);
    s->inner->pos = pos;
    src->pos = (uint64_t)((double)pos / s->step);
    return LND_OK;
}

static uint64_t lnd_resample_length(lnd_source *src) {
    lnd_resample_source *s = (lnd_resample_source *)src;
    return (uint64_t)((double)lnd_source_length(s->inner) / s->step);
}

static void lnd_resample_free(lnd_source *src) {
    lnd_resample_source *s = (lnd_resample_source *)src;
    if (s->owns_inner) lnd_source_free(s->inner);
    lnd_free_aligned(s->scratch);
    lnd_free_aligned(s->in);
    if (s->bank) lnd_sinc_release(s->bank);
    else lnd_free_aligned(s->table);
    lnd_free(s);
}

static const lnd_source_vt lnd_resample_linear_vt = {
    .read = lnd_linear_read,
    .read_pcm = lnd_linear_read_pcm,
    .seek = lnd_resample_seek,
    .length = lnd_resample_length,
    .free = lnd_resample_free,
};

static const lnd_source_vt lnd_resample_sinc_vt = {
    .read = lnd_sinc_read,
    .read_pcm = lnd_sinc_read_pcm,
    .seek = lnd_resample_seek,
    .length = lnd_resample_length,
    .free = lnd_resample_free,
};

static lnd_source *lnd_resample_create(lnd_source *inner, bool owns_inner, uint32_t out_rate, uint32_t scratch_frames, uint32_t quality, bool variable) {
    if (!inner || !inner->channels || inner->channels > LND_MAX_CHANNELS || !out_rate || !scratch_frames || scratch_frames > UINT32_MAX - 128 ||
        (size_t)scratch_frames + 128 > SIZE_MAX / inner->channels / sizeof(float))
        return nullptr;
    lnd_resample_source *s = lnd_alloc_zero(sizeof *s);
    if (!s) return nullptr;
    uint32_t ch = inner->channels;
    s->base.channels = ch;
    s->base.sample_rate_hz = out_rate;
    s->inner = inner;
    s->base.live = inner->live;
    s->base.length_known = inner->length_known;
    s->base.length_estimated = inner->length_estimated;
    s->owns_inner = owns_inner;
    s->variable = variable;
    s->step = (double)inner->sample_rate_hz / (double)out_rate;
    s->quality = quality;
    s->scratch_frames = scratch_frames;
    int32_t layout = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_LAYOUT);
    s->stride_samples = layout == LND_LAYOUT_PLANAR ? 1 : ch;
    s->in_cap = scratch_frames + (quality ? (quality == 1 ? 8 : quality == 2 ? 16 : 32) * 4 : 0);
    s->spacing = layout == LND_LAYOUT_PLANAR ? s->in_cap : 1;
    s->input = (LND_PCM){.planes = s->planes, .frames = s->in_cap, .channels = ch, .format = LND_FORMAT_F32, .layout = layout};
    if (quality == 0) {
        s->base.vt = &lnd_resample_linear_vt;
        s->frac = 1.0;
        s->scratch = lnd_alloc_aligned((size_t)scratch_frames * ch * sizeof(float), LND_CACHE_LINE);
        if (!s->scratch) {
            lnd_free(s);
            return nullptr;
        }
        s->input.data = s->scratch;
        for (uint32_t c = 0; c < ch; c++)
            s->planes[c] = s->scratch + (size_t)c * s->spacing;
        return &s->base;
    }
    s->base.vt = &lnd_resample_sinc_vt;
    s->taps = quality == 1 ? 8 : (quality == 2 ? 16 : 32);
    s->dot = lnd_simd.sinc_select(s->taps, s->stride_samples);
    s->frame = s->stride_samples > 1 ? lnd_simd.sinc_frame_select(s->taps, ch) : nullptr;
    s->half = s->taps / 2;
    s->in_cap = scratch_frames + s->taps * 4;
    if (variable) {
        size_t coefficients = (s->half * 256 + 1) * 2 + s->taps * 8;
        s->table = lnd_alloc_aligned(coefficients * sizeof(float), LND_CACHE_LINE);
    } else {
        double ratio = 1.0 / s->step;
        s->bank = lnd_sinc_acquire(s->taps, (ratio < 1.0 ? ratio : 1.0) * 0.95);
        s->table = s->bank ? s->bank->values : nullptr;
    }
    s->in = lnd_alloc_aligned((size_t)s->in_cap * ch * sizeof(float), LND_CACHE_LINE);
    if (!s->table || !s->in) {
        s->owns_inner = false;
        lnd_resample_free(&s->base);
        return nullptr;
    }
    s->input.data = s->in;
    for (uint32_t c = 0; c < ch; c++)
        s->planes[c] = s->in + (size_t)c * s->spacing;
    if (variable) lnd_sinc_variable_table(s->table, s->half);
    lnd_sinc_reset(s);
    return &s->base;
}

lnd_source *lnd_resample_source_create(lnd_source *inner, bool owns_inner, uint32_t out_rate, uint32_t scratch_frames, uint32_t quality) {
    return lnd_resample_create(inner, owns_inner, out_rate, scratch_frames, quality, false);
}

lnd_source *lnd_resample_source_create_variable(lnd_source *inner, uint32_t scratch_frames, uint32_t quality) {
    return lnd_resample_create(inner, false, inner->sample_rate_hz, scratch_frames, quality, true);
}

void lnd_resample_source_set_ratio(lnd_source *resampler, double ratio) {
    lnd_resample_source *s = (lnd_resample_source *)resampler;
    if (s->step == ratio) return;
    s->origin = s->travel;
    s->ticks = 0;
    s->cached = 0;
    s->step = ratio;
}
