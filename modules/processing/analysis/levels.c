#include "lindar_analysis.h"
#include "src/error.h"
#include "src/pcm.h"

#include <math.h>

double LND_AmplitudeToDb(double amplitude) { return 20.0 * log10(fabs(amplitude)); }

double LND_DbToAmplitude(double db) { return pow(10.0, db / 20.0); }

int32_t LND_PcmAnalyzeLevels(const LND_PCM *pcm, size_t offset, size_t frames, uint32_t channel, LND_LEVELS *levels) {
    if (!levels || !lnd_pcm_range(pcm, offset, frames) || (channel != LND_CHANNEL_ALL && channel >= pcm->channels)) return lnd_error(LND_ERR_INVALID_ARG);
    uint32_t first = channel == LND_CHANNEL_ALL ? 0 : channel;
    uint32_t count = channel == LND_CHANNEL_ALL ? pcm->channels : 1;
    if (frames > SIZE_MAX / count) return lnd_error(LND_ERR_INVALID_ARG);
    LND_LEVELS result = {.samples = frames * count};
    size_t stride = lnd_pcm_stride(pcm);
    double sum = 0.0;
    for (uint32_t c = first; c < first + count && frames; c++) {
        const uint8_t *p = lnd_pcm_at(pcm, c, offset);
        for (size_t f = 0; f < frames; f++) {
            double x = lnd_pcm_load_sample(p + f * stride, pcm->format);
            if (!isfinite(x)) return lnd_error(LND_ERR_FORMAT);
            double a = fabs(x);
            if (a > result.peak) result.peak = a;
            sum += x * x;
        }
    }
    if (isfinite(sum) && (sum != 0.0 || result.peak == 0.0)) {
        result.rms = result.samples ? sqrt(sum / (double)result.samples) : 0.0;
    } else {
        sum = 0.0;
        for (uint32_t c = first; c < first + count; c++) {
            const uint8_t *p = lnd_pcm_at(pcm, c, offset);
            for (size_t f = 0; f < frames; f++) {
                double x = lnd_pcm_load_sample(p + f * stride, pcm->format) / result.peak;
                sum += x * x;
            }
        }
        result.rms = result.peak * sqrt(sum / (double)result.samples);
    }
    result.peak_dbfs = LND_AmplitudeToDb(result.peak);
    result.rms_dbfs = LND_AmplitudeToDb(result.rms);
    *levels = result;
    return LND_OK;
}
