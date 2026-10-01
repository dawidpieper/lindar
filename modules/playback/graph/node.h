#pragma once

#include "src/atomic.h"
#include "src/platform.h"
#include "ring.h"
#include "src/spinlock.h"
#include "lindar_graph.h"
#include "pcm/audio/source.h"
#if LND_MODULE_MONITOR
#include "lindar_monitor.h"
#endif

#if LND_THREADS
typedef _Atomic(float) lnd_graph_atomic_float;
#else
typedef float lnd_graph_atomic_float;
#endif

struct LND_OUTPUT;

typedef struct LND_NODE lnd_node;
typedef struct lnd_edge lnd_edge;
typedef struct LND_DEVICE_INSTANCE lnd_instance;
typedef struct lnd_graph_source lnd_graph_source;
typedef struct lnd_graph_sound lnd_graph_sound;

enum {
    LND_OP_START,
    LND_OP_STOP,
    LND_OP_PAUSE,
    LND_OP_RESUME,
    LND_OP_SEEK,
    LND_OP_LOOP,
    LND_OP_GAIN,
    LND_OP_CLIP,
    LND_OP_PARAM,
    LND_OP_RESET,
    LND_OP_FLUSH,
};

typedef struct lnd_node_vt {
    void (*render)(lnd_node *n, float *dst, uint32_t frames);
    void (*command)(lnd_node *n, const lnd_cmd *c, bool immediate);
    void (*finish)(lnd_node *n);
    void (*destroy)(lnd_node *n);
    void (*render_pcm)(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames);
    uint32_t pcm_layouts;
    void (*render_planar)(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames);
} lnd_node_vt;

struct lnd_edge {
    lnd_node *src;
    lnd_node *dst;
    uint64_t cursor;
    uint64_t revision;
    lnd_source *adapter;
    lnd_source *resampler;
    float *mono;
    float *matrix;
    uint32_t channel;
    bool paused;
    lnd_edge *next_in;
    lnd_edge *next_out;
};

struct LND_NODE {
#if LND_MODULE_MONITOR
    LND_NODE_STATS monitor;
#endif
    const lnd_node_vt *vt;
#if LND_MODULE_SLIDE
    struct lnd_slide *slides;
#endif
#if LND_MODULE_NOTIFY
    struct LND_SUBSCRIPTION *notifications;
    uint64_t notification_position;
    int32_t notification_status;
#endif
    struct lnd_native_node *native;
    int32_t type;
    lnd_atomic_u32 channels;
    lnd_atomic_u32 sample_rate_hz;
    uint32_t block;
    uint32_t ramp;
    uint32_t rendered;
    lnd_atomic_i32 status;
    lnd_atomic_i32 clip;
    int32_t channel_mix;
    lnd_spinlock lock;
    lnd_ring ring;
    lnd_edge *inputs;
    lnd_edge *clock;
    lnd_atomic_u32 inputs_count;
    lnd_edge *outputs;
    lnd_atomic_u32 outputs_count;
    float *out;
    uint32_t out_cap;
    lnd_atomic_u64 revision;
    lnd_atomic_u64 produced;
    float *scratch;
    float *scratch_map;
    uint32_t input_channels;
    lnd_atomic_ptr matrices[LND_MAX_CHANNELS + 1];
    lnd_graph_atomic_float gain_param;
    float gain;
    float gain_target;
    float gain_step;
    uint32_t gain_ramp;
    lnd_atomic_u32 active;
    lnd_atomic_u64 last_pull;
    uint32_t extra_consumers;
    int32_t mix_mode;
    lnd_instance *instance;
    lnd_graph_sound *sound;
    lnd_graph_source *view;
    bool pending;
    lnd_node *pending_prev;
    lnd_node *pending_next;
    lnd_node *transaction_next;
    uint64_t visit[2];
    lnd_node *visit_parent[2];
    lnd_edge *visit_edge[2];
    uint32_t visit_branch[2];
    struct LND_NODE *prev;
    struct LND_NODE *next;
};

lnd_node *lnd_node_alloc(size_t size, const lnd_node_vt *vt, int32_t type, uint32_t channels, uint32_t sample_rate_hz);
void lnd_node_destroy(lnd_node *n);
lnd_node *lnd_node_create_bus(uint32_t channels, uint32_t sample_rate_hz, lnd_instance *instance);
int32_t lnd_node_status(lnd_node *n, const uint64_t *cursor);
int32_t lnd_node_status_locked(lnd_node *node);
void lnd_bus_finish(lnd_node *n, uint32_t frames, uint32_t got);
lnd_edge *lnd_bus_next(lnd_node *n, lnd_edge *edge);
void lnd_bus_render(lnd_node *n, float *dst, uint32_t frames);
uint32_t lnd_bus_pull_edge(lnd_node *n, lnd_edge *e, float *dst, uint32_t frames);
uint32_t lnd_bus_pull_edge_pcm(lnd_node *n, lnd_edge *e, const LND_PCM *pcm, size_t offset, uint32_t frames);
lnd_node *lnd_node_create_channel_splitter(uint32_t channels, uint32_t sample_rate_hz);
lnd_node *lnd_node_create_merger(uint32_t channels, uint32_t sample_rate_hz);
int32_t lnd_merger_set_channel(lnd_node *merger, lnd_node *src, uint32_t channel);
lnd_node *lnd_node_create_processor(const LND_PROCESSOR_PROCS *procs, void *user, uint32_t channels, uint32_t sample_rate_hz);
void *lnd_processor_user(const lnd_node *n);
const LND_PROCESSOR_PROCS *lnd_processor_procs(const lnd_node *n);
float lnd_processor_param(const lnd_node *n, int32_t param);
bool lnd_processor_validate(lnd_node *n, int32_t param, float value);
void lnd_processor_set_param(lnd_node *n, int32_t param, float value);
typedef void (*lnd_processor_planar)(void *user, float *const *planes, uint32_t frames, uint32_t channels, uint32_t sample_rate_hz);
void lnd_processor_set_planar(lnd_node *n, lnd_processor_planar process);
lnd_node *lnd_node_create_sink(struct LND_OUTPUT *output, lnd_instance *clock);
struct LND_OUTPUT *lnd_sink_output(const lnd_node *n);
void lnd_sinks_pump(lnd_instance *i, uint64_t frames);
void lnd_sinks_detach_instance(lnd_instance *i);
lnd_node *lnd_node_parent(const lnd_node *n);
bool lnd_node_is_branch(const lnd_node *n);
bool lnd_node_is_terminal(const lnd_node *n);
lnd_node *lnd_node_create_splitter(uint32_t channels, uint32_t sample_rate_hz, uint32_t outputs);
lnd_node *lnd_node_create_mixer(uint32_t channels, uint32_t sample_rate_hz, int32_t mode);
lnd_node *lnd_splitter_output(lnd_node *splitter, uint32_t index);
uint32_t lnd_splitter_outputs(const lnd_node *splitter);
uint64_t lnd_split_read(lnd_node *branch, float *dst, uint64_t frames);
int32_t lnd_split_state(const lnd_node *branch);
void lnd_split_set_state(lnd_node *branch, int32_t state);
uint64_t lnd_split_pos(const lnd_node *branch);
uint64_t lnd_node_available(lnd_node *n, const uint64_t *cursor);
uint64_t lnd_node_inputs_available(lnd_node *n, bool max);
int32_t lnd_node_connect(lnd_node *src, lnd_node *dst);
int32_t lnd_node_disconnect(lnd_node *src, lnd_node *dst);
int32_t lnd_node_set_output(lnd_node *src, lnd_node *dst);
void lnd_node_disconnect_all(lnd_node *n);
bool lnd_node_reaches(lnd_node *from, lnd_node *target);
void lnd_node_activate(lnd_node *n);
uint32_t lnd_node_pull(lnd_node *n, uint64_t *cursor, float *dst, uint32_t frames);
bool lnd_node_pull_planar(lnd_node *n, uint64_t *cursor, const LND_PCM *pcm, size_t offset, uint32_t frames, uint32_t *got);
void lnd_node_process(lnd_node *n, bool immediate);
int32_t lnd_node_post(lnd_node *n, uint32_t op, uint64_t u64, uint32_t u32, float f32);
bool lnd_node_drain(lnd_node *n, bool force);
void lnd_nodes_maintain(bool force, size_t limit);
int32_t lnd_node_ensure_ring(lnd_node *n);
int32_t lnd_node_reserve_input(lnd_node *node, uint32_t channels, bool mapping);
int32_t lnd_node_reconfigure(lnd_node *n, uint32_t channels, uint32_t sample_rate_hz);
const float *lnd_node_matrix(lnd_node *n, uint32_t src_channels);
uint64_t lnd_node_read(lnd_node *n, void *dst, int32_t format, uint64_t frames);
void lnd_nodes_gc(void);
void lnd_nodes_free_all(void);
bool lnd_context_has_node(const lnd_node *n);

lnd_node *lnd_node_create_source(lnd_source *origin, lnd_graph_source *owner);
lnd_source *lnd_source_node_origin(const lnd_node *n);
lnd_graph_source *lnd_source_node_owner(const lnd_node *n);
bool lnd_source_node_loop(const lnd_node *n);
int32_t lnd_source_node_state(const lnd_node *n);
uint64_t lnd_source_node_pos(const lnd_node *n);
void lnd_source_node_set_state(lnd_node *n, int32_t state);
void lnd_source_node_set_pos(lnd_node *n, uint64_t pos);
void lnd_source_node_reset(lnd_node *n);
uint64_t lnd_source_node_read(lnd_node *n, float *dst, uint64_t frames, bool loop);
void lnd_sound_free(lnd_graph_sound *s);

LND_SOUND *lnd_node_pcm_sound(const lnd_node *node);
lnd_node *lnd_pcm_input_node(LND_SOUND *sound, bool create);
uint64_t lnd_pcm_input_read(lnd_node *node, float *dst, uint64_t frames);

uint32_t lnd_node_pull_pcm(lnd_node *node, uint64_t *cursor, const LND_PCM *pcm, size_t offset, uint32_t frames);
uint64_t lnd_node_read_pcm(lnd_node *node, const LND_PCM *pcm, size_t offset, size_t frames);

uint32_t lnd_node_pull_pcm_valid(lnd_node *node, uint64_t *cursor, const LND_PCM *pcm, size_t offset, uint32_t frames);
bool lnd_bus_render_single(lnd_node *node, const LND_PCM *pcm, size_t offset, uint32_t frames);
