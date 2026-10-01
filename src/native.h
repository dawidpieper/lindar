#pragma once

#include "handle.h"
#include "spinlock.h"
#include "context.h"
#include "error.h"

typedef struct lnd_native_source lnd_native_source;

typedef struct lnd_native_sound {
    LND_SOUND base;
    lnd_native_source *source;
    lnd_atomic_u32 state;
    lnd_atomic_u32 loop;
    lnd_atomic_u32 gain;
    uint32_t references;
#if LND_MODULE_GRAPH
    lnd_atomic(LND_SOUND *) view;
    LND_NODE *node;
#endif
    bool owned;
    bool ended;
#if LND_MODULE_NOTIFY
    struct LND_SUBSCRIPTION *notifications;
#endif
} lnd_native_sound;

struct lnd_native_source {
    LND_SOURCE base;
    lnd_native_source *next;
    lnd_native_sound *sound;
    LND_SOURCE_CONFIG config;
#if LND_MODULE_CODECS
    const LND_CODEC *codec;
    int32_t decoded_format;
    bool length_estimated;
#endif
    LND_PCM input;
    LND_PCM stage;
    lnd_atomic_u64 position;
#if LND_MODULE_GRAPH
    lnd_atomic_u64 revision;
#endif
    lnd_atomic_i32 status;
    lnd_atomic_u32 end_requested;
    lnd_spinlock lock;
    int32_t deferred_error;
    size_t bytes;
    bool owned;
};

LND_INLINE LND_SOUND *lnd_sound_view(const LND_SOUND *sound) {
#if LND_MODULE_GRAPH
    if (sound && !sound->ops) {
        LND_SOUND *view = lnd_load(&((const lnd_native_sound *)sound)->view);
        if (view) return view;
    }
#endif
    return (LND_SOUND *)sound;
}

LND_INLINE uint64_t lnd_native_source_limit(const lnd_native_source *s) {
#if LND_MODULE_CODECS
    if (s->length_estimated) return 0;
#endif
    return s->config.length_frames;
}

LND_INLINE bool lnd_native_source_bounded(const lnd_native_source *s) {
#if LND_MODULE_CODECS
    if (s->length_estimated) return false;
#endif
    return s->config.length_known;
}

lnd_native_source *lnd_native_source_create(const LND_SOURCE_CONFIG *config);
void lnd_playback_free_all(void);
#if LND_MODULE_METADATA
bool lnd_native_source_registered(const LND_SOURCE *source);
#endif
int64_t lnd_native_read(lnd_native_source *s, const LND_PCM *pcm, size_t offset, size_t frames);
int32_t lnd_native_seek(lnd_native_source *s, uint64_t frame);
lnd_native_sound *lnd_native_sound_init(void *memory, size_t bytes, lnd_native_source *source);
bool lnd_playback_overlaps(const void *memory, size_t bytes);

int32_t lnd_native_source_free(lnd_native_source *s);
int32_t lnd_native_source_get_format(const lnd_native_source *s);
uint32_t lnd_native_source_get_sample_rate_hz(const lnd_native_source *s);
uint32_t lnd_native_source_get_channels(const lnd_native_source *s);
uint64_t lnd_native_source_get_length_frames(const lnd_native_source *s);
uint64_t lnd_native_source_get_position_frames(const lnd_native_source *s);
int32_t lnd_native_source_seek_frames(lnd_native_source *s, uint64_t frame);
int64_t lnd_native_source_read(lnd_native_source *s, void *dst, int32_t format, uint64_t frames);
lnd_native_sound *lnd_native_source_ensure_sound(lnd_native_source *s, const LND_SOUND_CONFIG *config);
lnd_native_source *lnd_native_sound_get_source(const lnd_native_sound *s);
int32_t lnd_native_sound_play(lnd_native_sound *s);
int32_t lnd_native_sound_set_pause(lnd_native_sound *s, bool pause);
int32_t lnd_native_sound_stop(lnd_native_sound *s);
int32_t lnd_native_sound_get_state(const lnd_native_sound *s);
uint64_t lnd_native_sound_get_position_frames(const lnd_native_sound *s);
uint32_t lnd_native_sound_get_sample_rate_hz(const lnd_native_sound *s);
uint32_t lnd_native_sound_get_channels(const lnd_native_sound *s);
double lnd_native_sound_get_position_seconds(const lnd_native_sound *s);
int32_t lnd_native_sound_seek_frames(lnd_native_sound *s, uint64_t frame);
int32_t lnd_native_sound_seek_seconds(lnd_native_sound *s, double sec);
uint64_t lnd_native_sound_get_length_frames(const lnd_native_sound *s);
double lnd_native_sound_get_length_seconds(const lnd_native_sound *s);
int32_t lnd_native_sound_set_gain(lnd_native_sound *s, float gain);
float lnd_native_sound_get_gain(const lnd_native_sound *s);
int32_t lnd_native_sound_set_loop(lnd_native_sound *s, bool loop);
bool lnd_native_sound_get_loop(const lnd_native_sound *s);
int64_t lnd_native_sound_read_f32(lnd_native_sound *s, float *dst, uint64_t frames);

int32_t lnd_native_sound_ref(LND_SOUND *sound);
void lnd_native_sound_unref(LND_SOUND *sound);
int64_t lnd_native_sound_read(void *sound, const LND_PCM *pcm, size_t offset, size_t frames);
int64_t lnd_native_sound_render(void *sound, const LND_PCM *pcm, size_t offset, size_t frames, bool transport);

int64_t lnd_native_sound_render_pcm(void *sound, const LND_PCM *pcm, size_t offset, size_t frames);

int32_t lnd_native_sound_set_gain_q16(lnd_native_sound *s, uint32_t gain);
uint32_t lnd_native_sound_get_gain_q16(const lnd_native_sound *s);

int32_t lnd_sound_ref(LND_SOUND *sound);
void lnd_sound_unref(LND_SOUND *sound);
