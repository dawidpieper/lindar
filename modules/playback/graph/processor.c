#include "src/alloc.h"
#include "src/callback.h"
#include "src/platform.h"
#include "node.h"
#include "native.h"
#include "src/config.h"
#include "src/pcm.h"

typedef struct lnd_processor_node {
    lnd_node base;
    LND_PROCESSOR_PROCS procs;
    void *user;
    lnd_processor_planar process_planar;
    lnd_graph_atomic_float params[LND_PARAM_USER_COUNT];
    lnd_graph_pcm_buffer processing;
} lnd_processor_node;

static void lnd_processor_render_pcm(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_processor_node *p = (lnd_processor_node *)n;
    if (n->native)
        lnd_bus_render_pcm(n, pcm, offset, frames);
    else
        lnd_bus_render(n, (float *)lnd_pcm_at(pcm, 0, offset), frames);
    if (lnd_load(&n->status) < 0) return;
    uint32_t count = p->procs.flags & LND_PROCESSOR_BOUNDED ? n->rendered : frames;
    if (!count) return;
    bool convert = p->processing.pcm.data != nullptr;
    const LND_PCM *target = convert ? &p->processing.pcm : pcm;
    size_t at = convert ? 0 : offset;
    int32_t result = convert ? lnd_pcm_convert(target, 0, pcm, offset, count) : LND_OK;
    if (result == LND_OK) {
        lnd_callback_enter();
        result = p->procs.process_pcm(p->user, target, at, count, n->sample_rate_hz);
        lnd_callback_leave();
    }
    if (result == LND_OK && convert) result = LND_PcmConvert(pcm, offset, target, 0, count);
    if (!lnd_node_pcm_result(n, result)) LND_PcmSilence(pcm, offset, frames);
}

static void lnd_processor_render(lnd_node *n, float *dst, uint32_t frames) {
    lnd_processor_node *p = (lnd_processor_node *)n;
    if (p->procs.process_pcm) {
        LND_PCM pcm = {.data = dst, .frames = frames, .channels = n->channels, .format = LND_FORMAT_F32};
        lnd_processor_render_pcm(n, &pcm, 0, frames);
        return;
    }
    lnd_bus_render(n, dst, frames);
    lnd_callback_enter();
    uint32_t count = p->procs.flags & LND_PROCESSOR_BOUNDED ? n->rendered : frames;
    if (count) p->procs.process(p->user, dst, count, n->channels, n->sample_rate_hz);
    lnd_callback_leave();
}

static void lnd_processor_render_planar(lnd_node *n, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_processor_node *p = (lnd_processor_node *)n;
    lnd_bus_render_single(n, pcm, offset, frames);
    uint32_t count = p->procs.flags & LND_PROCESSOR_BOUNDED ? n->rendered : frames;
    if (!count) return;
    float *planes[LND_MAX_CHANNELS];
    for (uint32_t c = 0; c < n->channels; c++) planes[c] = (float *)lnd_pcm_at(pcm, c, offset);
    lnd_callback_enter();
    p->process_planar(p->user, planes, count, n->channels, n->sample_rate_hz);
    lnd_callback_leave();
}

static void lnd_processor_command(lnd_node *n, const lnd_cmd *c, bool immediate) {
    LND_UNUSED(immediate);
    lnd_processor_node *p = (lnd_processor_node *)n;
    if (c->op == LND_OP_PARAM && c->u32 < LND_PARAM_USER_COUNT && p->procs.param) {
        lnd_callback_enter();
        p->procs.param(p->user, LND_PARAM_USER + (int32_t)c->u32, c->f32);
        lnd_callback_leave();
    }
}

static void lnd_processor_destroy(lnd_node *n) {
    lnd_processor_node *p = (lnd_processor_node *)n;
    lnd_free_aligned(p->processing.pcm.data);
    if (p->procs.release) {
        lnd_callback_enter();
        p->procs.release(p->user);
        lnd_callback_leave();
    }
}

static const lnd_node_vt lnd_processor_vt = {
    .render = lnd_processor_render,
    .command = lnd_processor_command,
    .destroy = lnd_processor_destroy,
};

lnd_node *lnd_node_create_processor(const LND_PROCESSOR_PROCS *procs, void *user, uint32_t channels, uint32_t sample_rate_hz) {
    static const lnd_node_vt pcm_vt[] = {{.render = lnd_processor_render,
                                          .command = lnd_processor_command,
                                          .destroy = lnd_processor_destroy,
                                          .render_pcm = lnd_processor_render_pcm,
                                          .pcm_layouts = 1},
                                         {.render = lnd_processor_render,
                                          .command = lnd_processor_command,
                                          .destroy = lnd_processor_destroy,
                                          .render_pcm = lnd_processor_render_pcm,
                                          .pcm_layouts = 2},
                                         {.render = lnd_processor_render,
                                          .command = lnd_processor_command,
                                          .destroy = lnd_processor_destroy,
                                          .render_pcm = lnd_processor_render_pcm,
                                          .pcm_layouts = 3}};
    uint32_t layouts = procs->process_layouts ? procs->process_layouts : LND_LAYOUT_MASK_ALL;
    const lnd_node_vt *vt = procs->process_pcm ? &pcm_vt[layouts - 1] : &lnd_processor_vt;
    lnd_graph_pcm_buffer processing = {0};
    int32_t format = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT), layout = (int32_t)lnd_cfg_u32(LND_CFG_INTERNAL_LAYOUT);
    if (!(layouts & (1u << layout))) layout = layouts & 1 ? LND_LAYOUT_INTERLEAVED : LND_LAYOUT_PLANAR;
    if (procs->process_format && procs->process_format != format &&
        !lnd_graph_pcm_init_format(&processing, channels, lnd_cfg_u32(LND_CFG_GRAPH_MIX_BLOCK_FRAMES), procs->process_format, layout))
        return nullptr;
    lnd_processor_node *p = (lnd_processor_node *)lnd_node_alloc(sizeof *p, vt, LND_NODE_PROCESSOR, channels, sample_rate_hz);
    if (!p) {
        lnd_free_aligned(processing.pcm.data);
        return nullptr;
    }
    p->processing = processing;
    p->processing.pcm.planes = p->processing.planes;
    p->procs = *procs;
    p->user = user;
    return &p->base;
}

void *lnd_processor_user(const lnd_node *n) { return ((const lnd_processor_node *)n)->user; }

const LND_PROCESSOR_PROCS *lnd_processor_procs(const lnd_node *n) { return &((const lnd_processor_node *)n)->procs; }

bool lnd_processor_validate(lnd_node *n, int32_t param, float value) {
    const lnd_processor_node *p = (const lnd_processor_node *)n;
    if (!p->procs.validate) return true;
    lnd_spinlock_lock(&n->lock);
    lnd_callback_enter();
    bool result = p->procs.validate(p->user, param, value);
    lnd_callback_leave();
    lnd_spinlock_unlock(&n->lock);
    return result;
}

float lnd_processor_param(const lnd_node *n, int32_t param) { return ((const lnd_processor_node *)n)->params[param - LND_PARAM_USER]; }

void lnd_processor_set_param(lnd_node *n, int32_t param, float value) { ((lnd_processor_node *)n)->params[param - LND_PARAM_USER] = value; }

void lnd_processor_set_planar(lnd_node *n, lnd_processor_planar process) {
    static const lnd_node_vt vt = {
        .render = lnd_processor_render,
        .command = lnd_processor_command,
        .destroy = lnd_processor_destroy,
        .render_planar = lnd_processor_render_planar,
    };
    ((lnd_processor_node *)n)->process_planar = process;
    n->vt = &vt;
}
