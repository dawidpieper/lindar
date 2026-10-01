#pragma once

#include "src/atomic.h"
#include "src/platform.h"
#include "node.h"
#include "lindar.h"
#include "src/handle.h"

struct lnd_graph_source {
    LND_SOURCE base;
    lnd_source *source;
    lnd_atomic(lnd_node *) node;
    bool view;
    uint32_t flags;
    int32_t format;
    const LND_CODEC *codec;
    struct lnd_graph_source *prev;
    struct lnd_graph_source *next;
};

typedef struct lnd_sound_tap {
    lnd_source base;
    struct lnd_graph_sound *sound;
} lnd_sound_tap;

struct lnd_graph_sound {
    LND_SOUND base;
    lnd_node *node;
    lnd_spinlock lock;
    uint32_t references;
    uint64_t cursor;
    bool consumer;
    bool transport;
    bool paused;
    lnd_graph_atomic_float gain;
    lnd_atomic_u32 loop;
    lnd_atomic_u32 sample_rate_hz;
    lnd_atomic_u32 channels;
    uint32_t flags;
    float *matrix;
    uint32_t matrix_channels;
    bool custom_matrix;
    lnd_sound_tap tap;
    lnd_source *resampler;
    float *scratch;
    uint32_t scratch_frames;
    uint32_t prepared_source_rate_hz;
    uint32_t prepared_source_channels;
    uint64_t source_base_frames;
    uint64_t delivered_frames;
    bool conversion_ready;
    bool reset_pending;
    bool ahead;
};

typedef struct lnd_graph_source lnd_graph_source;
typedef struct lnd_graph_sound lnd_graph_sound;

void lnd_sources_free_all(void);

lnd_source *lnd_source_stream_wrap(lnd_source *source, uint32_t flags);
bool lnd_source_obj_valid(const lnd_graph_source *s);

lnd_graph_source *lnd_source_obj_create(lnd_source *source, uint32_t flags, int32_t format);

extern const lnd_source_ops lnd_graph_source_ops;
extern const lnd_sound_ops lnd_graph_sound_ops;

lnd_graph_source *lnd_graph_source_create_buffer(LND_BUFFER *b);
lnd_graph_source *lnd_graph_source_create_proc(const LND_SOURCE_PROCS *procs, void *user, int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint32_t flags);
int32_t lnd_graph_source_free(lnd_graph_source *s);
int32_t lnd_graph_source_get_format(const lnd_graph_source *s);
const LND_CODEC *lnd_graph_source_get_codec(const lnd_graph_source *s);
uint32_t lnd_graph_source_get_sample_rate_hz(const lnd_graph_source *s);
uint32_t lnd_graph_source_get_channels(const lnd_graph_source *s);
uint64_t lnd_graph_source_get_length_frames(const lnd_graph_source *s);
uint64_t lnd_graph_source_get_position_frames(const lnd_graph_source *s);
int32_t lnd_graph_source_seek_frames(lnd_graph_source *s, uint64_t frame);
int64_t lnd_graph_source_read(lnd_graph_source *s, void *dst, int32_t format, uint64_t frames);
LND_NODE *lnd_graph_source_ensure_node(lnd_graph_source *s);
lnd_graph_sound *lnd_graph_source_ensure_sound(lnd_graph_source *s, const LND_SOUND_CONFIG *config);
lnd_graph_source *lnd_graph_node_ensure_source(LND_NODE *n);
lnd_graph_sound *lnd_graph_node_ensure_sound(LND_NODE *n, const LND_SOUND_CONFIG *config);
LND_NODE *lnd_graph_sound_get_node(const lnd_graph_sound *s);
lnd_graph_source *lnd_graph_sound_get_source(const lnd_graph_sound *s);
int32_t lnd_graph_sound_play(lnd_graph_sound *s);
int32_t lnd_graph_sound_set_pause(lnd_graph_sound *s, bool pause);
int32_t lnd_graph_sound_stop(lnd_graph_sound *s);
int32_t lnd_graph_sound_get_state(const lnd_graph_sound *s);
uint64_t lnd_graph_sound_get_position_frames(const lnd_graph_sound *s);
uint32_t lnd_graph_sound_get_sample_rate_hz(const lnd_graph_sound *s);
uint32_t lnd_graph_sound_get_channels(const lnd_graph_sound *s);
double lnd_graph_sound_get_position_seconds(const lnd_graph_sound *s);
int32_t lnd_graph_sound_seek_frames(lnd_graph_sound *s, uint64_t frame);
int32_t lnd_graph_sound_seek_seconds(lnd_graph_sound *s, double sec);
uint64_t lnd_graph_sound_get_length_frames(const lnd_graph_sound *s);
double lnd_graph_sound_get_length_seconds(const lnd_graph_sound *s);
int32_t lnd_graph_sound_set_gain(lnd_graph_sound *s, float gain);
float lnd_graph_sound_get_gain(const lnd_graph_sound *s);
int32_t lnd_graph_sound_set_loop(lnd_graph_sound *s, bool loop);
bool lnd_graph_sound_get_loop(const lnd_graph_sound *s);
int32_t lnd_graph_sound_set_output(lnd_graph_sound *s, LND_NODE *dst);
LND_NODE *lnd_graph_sound_get_output(const lnd_graph_sound *s);
int64_t lnd_graph_sound_read_f32(lnd_graph_sound *s, float *dst, uint64_t frames);

int64_t lnd_graph_source_read_pcm(lnd_graph_source *s, const LND_PCM *pcm, size_t offset, size_t frames);
lnd_node *lnd_source_obj_node(lnd_graph_source *s);

LND_INLINE uint32_t lnd_sound_rate(const lnd_graph_sound *s) { return s->sample_rate_hz ? s->sample_rate_hz : s->node->sample_rate_hz; }

LND_INLINE uint32_t lnd_sound_channels(const lnd_graph_sound *s) { return s->channels ? s->channels : s->node->channels; }

LND_INLINE uint64_t lnd_scale_frames(uint64_t frames, uint32_t from, uint32_t to) {
    if (from == to || !from || !to) return frames;
    uint64_t whole = frames / from;
    uint64_t fraction = ((frames % from) * to + from / 2) / from;
    if (whole > (UINT64_MAX - fraction) / to) return UINT64_MAX;
    return whole * to + fraction;
}

LND_INLINE bool lnd_sound_valid(const lnd_graph_sound *s) { return s && lnd_context_has_node(s->node) && s->node->sound == s; }

LND_INLINE bool lnd_sound_draining(const lnd_graph_sound *s) {
    if (!s->transport || !s->ahead || !s->resampler) return false;
    int32_t status = lnd_source_status(s->resampler);
    return status >= 0 && status != LND_SOURCE_EOF;
}

bool lnd_sound_busy(const lnd_node *node);
int32_t lnd_sound_sync_read(lnd_graph_sound *sound);
