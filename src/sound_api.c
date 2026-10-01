#include "native.h"

int32_t LND_SoundSetConfig(LND_SOUND *sound, const LND_SOUND_CONFIG *config) {
    const LND_SOUND_CONFIG c = config ? *config : (LND_SOUND_CONFIG){0};
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!sound || c.channels > LND_MAX_CHANNELS) return lnd_error(LND_ERR_INVALID_ARG);
#if LND_MODULE_GRAPH
    sound = lnd_sound_view(sound);
    if (sound->ops) return sound->ops->SoundSetConfig(sound, config);
#endif
    const lnd_native_sound *native = (const lnd_native_sound *)sound;
    if (!native->source) return lnd_error(LND_ERR_STATE);
    if ((c.sample_rate_hz && c.sample_rate_hz != native->source->config.sample_rate_hz) || (c.channels && c.channels != native->source->config.channels) || c.channel_matrix || c.flags) {
#if LND_MODULE_GRAPH
        return lnd_graph_pcm_sound(&native->source->base, config, true) ? LND_OK : LND_ErrorGetLast();
#else
        return lnd_error(LND_ERR_UNSUPPORTED);
#endif
    }
    return LND_OK;
}

LND_SOURCE *LND_SoundGetSource(const LND_SOUND *s) {
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SoundGetSource(s);
#endif
    return (LND_SOURCE *)lnd_native_sound_get_source((const lnd_native_sound *)s);
}

int32_t LND_SoundPlay(LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundPlay(s);
#endif
    return lnd_native_sound_play((lnd_native_sound *)s);
}

int32_t LND_SoundSetPause(LND_SOUND *s, bool pause) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundSetPause(s, pause);
#endif
    return lnd_native_sound_set_pause((lnd_native_sound *)s, pause);
}

int32_t LND_SoundStop(LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundStop(s);
#endif
    return lnd_native_sound_stop((lnd_native_sound *)s);
}

int32_t LND_SoundGetState(const LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundGetState(s);
#endif
    return lnd_native_sound_get_state((const lnd_native_sound *)s);
}

uint64_t LND_SoundGetPositionFrames(const LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundGetPositionFrames(s);
#endif
    return lnd_native_sound_get_position_frames((const lnd_native_sound *)s);
}

uint32_t LND_SoundGetSampleRateHz(const LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundGetSampleRateHz(s);
#endif
    return lnd_native_sound_get_sample_rate_hz((const lnd_native_sound *)s);
}

uint32_t LND_SoundGetChannels(const LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundGetChannels(s);
#endif
    return lnd_native_sound_get_channels((const lnd_native_sound *)s);
}

int32_t LND_SoundSeekFrames(LND_SOUND *s, uint64_t frame) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundSeekFrames(s, frame);
#endif
    return lnd_native_sound_seek_frames((lnd_native_sound *)s, frame);
}

uint64_t LND_SoundGetLengthFrames(const LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundGetLengthFrames(s);
#endif
    return lnd_native_sound_get_length_frames((const lnd_native_sound *)s);
}

int32_t LND_SoundSetLoop(LND_SOUND *s, bool loop) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundSetLoop(s, loop);
#endif
    return lnd_native_sound_set_loop((lnd_native_sound *)s, loop);
}

bool LND_SoundGetLoop(const LND_SOUND *s) {
#if LND_MODULE_GRAPH
    s = lnd_sound_view(s);
    if (s && s->ops) return s->ops->SoundGetLoop(s);
#endif
    return lnd_native_sound_get_loop((const lnd_native_sound *)s);
}
