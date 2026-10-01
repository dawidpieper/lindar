#pragma once

#include "lindar.h"
#include "lnd_modules.h"

typedef struct LND_NODE LND_NODE;
typedef struct LND_CODEC LND_CODEC;
#if LND_MODULE_METADATA
#include "lindar_metadata.h"
#endif

typedef struct lnd_source_ops {
    int32_t (*SourceGetInfo)(const LND_SOURCE *source, LND_SOURCE_INFO *info);
    int32_t (*SourceGetStatus)(const LND_SOURCE *s);
    int32_t (*SourceEnd)(LND_SOURCE *s);
    int32_t (*SourceFree)(LND_SOURCE *s);
    int32_t (*SourceGetFormat)(const LND_SOURCE *s);
    const LND_CODEC *(*SourceGetCodec)(const LND_SOURCE *s);
    uint32_t (*SourceGetSampleRateHz)(const LND_SOURCE *s);
    uint32_t (*SourceGetChannels)(const LND_SOURCE *s);
    uint64_t (*SourceGetLengthFrames)(const LND_SOURCE *s);
    uint64_t (*SourceGetPositionFrames)(const LND_SOURCE *s);
    int32_t (*SourceSeekFrames)(LND_SOURCE *s, uint64_t frame);
    int64_t (*SourceRead)(LND_SOURCE *s, void *dst, int32_t format, uint64_t frames);
    int64_t (*SourceReadPcm)(LND_SOURCE *s, const LND_PCM *pcm, size_t offset, size_t frames);
    LND_NODE *(*SourceGetNode)(const LND_SOURCE *s);
    LND_SOUND *(*SourceGetSound)(const LND_SOURCE *s);
    LND_NODE *(*SourceEnsureNode)(LND_SOURCE *s);
    LND_SOUND *(*SourceEnsureSound)(LND_SOURCE *s, const LND_SOUND_CONFIG *config);
} lnd_source_ops;

struct LND_SOURCE {
    const lnd_source_ops *ops;
#if LND_MODULE_CODECS
    uint64_t bitrate_bps;
    bool bitrate_estimated;
#endif
#if LND_MODULE_METADATA
    int32_t (*metadata_provider)(const LND_SOURCE *source, LND_METADATA *metadata);
    LND_METADATA *metadata;
    int32_t metadata_status;
#endif
};

typedef struct lnd_sound_ops {
    int32_t (*SoundRef)(LND_SOUND *sound);
    void (*SoundUnref)(LND_SOUND *sound);
    int64_t (*SoundRenderPcm)(LND_SOUND *sound, const LND_PCM *pcm, size_t offset, size_t frames);
    int32_t (*SoundSetConfig)(LND_SOUND *sound, const LND_SOUND_CONFIG *config);
    int32_t (*SoundSetGainQ16)(LND_SOUND *sound, uint32_t gain);
    uint32_t (*SoundGetGainQ16)(const LND_SOUND *sound);
    LND_NODE *(*SoundGetNode)(const LND_SOUND *s);
    LND_SOURCE *(*SoundGetSource)(const LND_SOUND *s);
    int32_t (*SoundPlay)(LND_SOUND *s);
    int32_t (*SoundSetPause)(LND_SOUND *s, bool pause);
    int32_t (*SoundStop)(LND_SOUND *s);
    int32_t (*SoundGetState)(const LND_SOUND *s);
    uint64_t (*SoundGetPositionFrames)(const LND_SOUND *s);
    uint32_t (*SoundGetSampleRateHz)(const LND_SOUND *s);
    uint32_t (*SoundGetChannels)(const LND_SOUND *s);
    double (*SoundGetPositionSeconds)(const LND_SOUND *s);
    int32_t (*SoundSeekFrames)(LND_SOUND *s, uint64_t frame);
    int32_t (*SoundSeekSeconds)(LND_SOUND *s, double sec);
    uint64_t (*SoundGetLengthFrames)(const LND_SOUND *s);
    double (*SoundGetLengthSeconds)(const LND_SOUND *s);
    int32_t (*SoundSetGain)(LND_SOUND *s, float gain);
    float (*SoundGetGain)(const LND_SOUND *s);
    int32_t (*SoundSetLoop)(LND_SOUND *s, bool loop);
    bool (*SoundGetLoop)(const LND_SOUND *s);
    int32_t (*SoundSetOutput)(LND_SOUND *s, LND_NODE *dst);
    LND_NODE *(*SoundGetOutput)(const LND_SOUND *s);
    int64_t (*SoundRead)(LND_SOUND *s, float *dst, uint64_t frames);
} lnd_sound_ops;

struct LND_SOUND {
    const lnd_sound_ops *ops;
};

LND_SOUND *lnd_graph_pcm_sound(LND_SOURCE *source, const LND_SOUND_CONFIG *config, bool configure);
