#include "native.h"

int32_t LND_SourceGetInfo(const LND_SOURCE *source, LND_SOURCE_INFO *info) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!source || !info) return lnd_error(LND_ERR_INVALID_ARG);
#if LND_MODULE_GRAPH
    if (source->ops) {
        int32_t result = source->ops->SourceGetInfo(source, info);
#if LND_MODULE_CODECS
        if (!result) {
            info->bitrate_bps = source->bitrate_bps;
            info->bitrate_estimated = source->bitrate_estimated;
        }
#endif
        return result;
    }
#endif
    lnd_native_source *s = (lnd_native_source *)source;
    if (!lnd_spinlock_try(&s->lock)) return lnd_error(LND_ERR_BUSY);
    *info = (LND_SOURCE_INFO){.length_frames = s->config.length_frames,
                              .position_frames = lnd_load(&s->position),
                              .sample_rate_hz = s->config.sample_rate_hz,
                              .channels = s->config.channels,
                              .format = lnd_native_source_get_format(s),
                              .status = lnd_load(&s->status),
                              .length_kind = s->config.length_known ? LND_LENGTH_EXACT : LND_LENGTH_UNKNOWN};
#if LND_MODULE_CODECS
    if (s->length_estimated) info->length_kind = LND_LENGTH_ESTIMATED;
    info->bitrate_bps = source->bitrate_bps;
    info->bitrate_estimated = source->bitrate_estimated;
#endif
    lnd_spinlock_unlock(&s->lock);
    return LND_OK;
}

int32_t LND_SourceFree(LND_SOURCE *s) {
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SourceFree(s);
#endif
    return lnd_native_source_free((lnd_native_source *)s);
}

int32_t LND_SourceGetFormat(const LND_SOURCE *s) {
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SourceGetFormat(s);
#endif
    return lnd_native_source_get_format((const lnd_native_source *)s);
}

uint32_t LND_SourceGetSampleRateHz(const LND_SOURCE *s) {
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SourceGetSampleRateHz(s);
#endif
    return lnd_native_source_get_sample_rate_hz((const lnd_native_source *)s);
}

uint32_t LND_SourceGetChannels(const LND_SOURCE *s) {
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SourceGetChannels(s);
#endif
    return lnd_native_source_get_channels((const lnd_native_source *)s);
}

uint64_t LND_SourceGetLengthFrames(const LND_SOURCE *s) {
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SourceGetLengthFrames(s);
#endif
    return lnd_native_source_get_length_frames((const lnd_native_source *)s);
}

uint64_t LND_SourceGetPositionFrames(const LND_SOURCE *s) {
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SourceGetPositionFrames(s);
#endif
    return lnd_native_source_get_position_frames((const lnd_native_source *)s);
}

int32_t LND_SourceSeekFrames(LND_SOURCE *s, uint64_t frame) {
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SourceSeekFrames(s, frame);
#endif
    return lnd_native_source_seek_frames((lnd_native_source *)s, frame);
}

int64_t LND_SourceRead(LND_SOURCE *s, void *dst, int32_t format, uint64_t frames) {
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SourceRead(s, dst, format, frames);
#endif
    return lnd_native_source_read((lnd_native_source *)s, dst, format, frames);
}

LND_SOUND *LND_SourceGetSound(const LND_SOURCE *source) {
    if (!source) return nullptr;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    LND_SOUND *sound;
#if LND_MODULE_GRAPH
    if (source->ops)
        sound = source->ops->SourceGetSound(source);
    else
#endif
        sound = (LND_SOUND *)((const lnd_native_source *)source)->sound;
    lnd_context_unlock();
    return sound;
}

LND_SOUND *LND_SourceEnsureSound(LND_SOURCE *s, const LND_SOUND_CONFIG *config) {
#if LND_MODULE_GRAPH
    if (s && s->ops) return s->ops->SourceEnsureSound(s, config);
    return lnd_graph_pcm_sound(s, config, false);
#else
    return (LND_SOUND *)lnd_native_source_ensure_sound((lnd_native_source *)s, config);
#endif
}
