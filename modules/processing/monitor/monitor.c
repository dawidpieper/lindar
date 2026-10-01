#include "monitor.h"
#include "lindar_monitor.h"
#include "playback/graph/node.h"
#include "playback/graph/context.h"
#include "src/thread.h"
#include "src/callback.h"
static _Thread_local lnd_monitor_scope *current;

void lnd_monitor_begin(lnd_node *n, lnd_monitor_scope *s) {
    *s = (lnd_monitor_scope){.parent = current};
    if (!n->monitor.enabled && !current) return;
    s->start = lnd_time_ns();
    current = s;
}

void lnd_monitor_end(lnd_node *n, lnd_monitor_scope *s, uint32_t frames) {
    if (!s->start) return;
    uint64_t elapsed = lnd_time_ns() - s->start;
    current = s->parent;
    if (current) current->children += elapsed;
    if (!n->monitor.enabled) return;
    ++n->monitor.calls;
    n->monitor.frames += frames;
    n->monitor.render_ns += elapsed;
    n->monitor.self_ns += elapsed > s->children ? elapsed - s->children : 0;
    if (elapsed > n->monitor.peak_ns) n->monitor.peak_ns = elapsed;
}

static int32_t access(lnd_node *n, LND_NODE_STATS *out, int action) {
    if (!n || (action == 0 && !out)) return LND_ERR_INVALID_ARG;
    if (lnd_callback_active() || !lnd_context_enter()) return LND_ERR_BUSY;
    if (!lnd_context_has_node(n)) {
        lnd_context_unlock();
        return LND_ERR_INVALID_ARG;
    }
    lnd_spinlock_lock(&n->lock);
    if (action == 0) {
        *out = n->monitor;
        double scale = out->frames ? (double)n->sample_rate_hz / ((double)out->frames * 10000000.0) : 0;
        out->render_percent = out->render_ns * scale;
        out->self_percent = out->self_ns * scale;
    } else if (action == 1)
        n->monitor = (LND_NODE_STATS){.enabled = n->monitor.enabled};
    else
        n->monitor.enabled = action == 2;
    lnd_spinlock_unlock(&n->lock);
    lnd_context_unlock();
    return LND_OK;
}
int32_t LND_NodeSetMonitoring(LND_NODE *n, bool enabled) { return access(n, nullptr, enabled ? 2 : 3); }
int32_t LND_NodeGetStats(const LND_NODE *n, LND_NODE_STATS *stats) { return access((lnd_node *)n, stats, 0); }
int32_t LND_NodeResetStats(LND_NODE *n) { return access(n, nullptr, 1); }
