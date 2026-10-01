#include "lindar_pcm_float.h"
#include "src/native.h"
#include <math.h>

double LND_SoundGetPositionSeconds(const LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundGetPositionSeconds(s);
#endif
    return lnd_native_sound_get_position_seconds((const lnd_native_sound *)s);
}

int32_t LND_SoundSeekSeconds(LND_SOUND *s, double sec) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundSeekSeconds(s, sec);
#endif
    return (int32_t)lnd_native_sound_seek_seconds((lnd_native_sound *)s, sec);
}

double LND_SoundGetLengthSeconds(const LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundGetLengthSeconds(s);
#endif
    return lnd_native_sound_get_length_seconds((const lnd_native_sound *)s);
}

int32_t LND_SoundSetGain(LND_SOUND *s, float gain) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundSetGain(s, gain);
#endif
    return (int32_t)lnd_native_sound_set_gain((lnd_native_sound *)s, gain);
}

float LND_SoundGetGain(const LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundGetGain(s);
#endif
    return (float)lnd_native_sound_get_gain((const lnd_native_sound *)s);
}

int64_t LND_SoundReadF32(LND_SOUND *s, float *dst, uint64_t frames) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundRead(s, dst, frames);
#endif
    return lnd_native_sound_read_f32((lnd_native_sound *)s, dst, frames);
}

double lnd_native_sound_get_position_seconds(const lnd_native_sound *s) {
    uint32_t sample_rate_hz = lnd_native_sound_get_sample_rate_hz(s);
    return sample_rate_hz ? (double)lnd_native_sound_get_position_frames(s) / sample_rate_hz : 0.0f;
}
double lnd_native_sound_get_length_seconds(const lnd_native_sound *s) {
    uint32_t sample_rate_hz = lnd_native_sound_get_sample_rate_hz(s);
    return sample_rate_hz ? (double)lnd_native_sound_get_length_frames(s) / sample_rate_hz : 0.0f;
}
int32_t lnd_native_sound_seek_seconds(lnd_native_sound *s, double sec) {
    double frame = (double)sec * lnd_native_sound_get_sample_rate_hz(s);
    if (!isfinite(frame) || frame < 0 || frame >= (double)UINT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_native_sound_seek_frames(s, (uint64_t)(frame + 0.5));
}
int32_t lnd_native_sound_set_gain(lnd_native_sound *s, float gain) {
    double fixed = (double)gain * 65536.0;
    if (!isfinite(fixed) || fixed < 0 || fixed > UINT32_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_native_sound_set_gain_q16(s, (uint32_t)(fixed + 0.5));
}
float lnd_native_sound_get_gain(const lnd_native_sound *s) { return (float)lnd_native_sound_get_gain_q16(s) / 65536.0f; }
int64_t lnd_native_sound_read_f32(lnd_native_sound *s, float *dst, uint64_t frames) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!s || frames > SIZE_MAX || frames > INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = lnd_native_sound_get_channels(s), .format = LND_FORMAT_F32};
    return lnd_native_sound_read(s, &pcm, 0, (size_t)frames);
}

int64_t lnd_native_sound_read(void *sound, const LND_PCM *pcm, size_t offset, size_t frames) {
    return lnd_native_sound_render(sound, pcm, offset, frames, false);
}
