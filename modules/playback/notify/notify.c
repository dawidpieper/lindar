#include "notify.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/native.h"
#if LND_MODULE_GRAPH
#include "playback/graph/node.h"
#include "playback/graph/sound.h"
#endif

struct LND_SUBSCRIPTION {
    LND_SUBSCRIPTION *next;
    LND_SUBSCRIPTION *owner_next;
    LND_SUBSCRIPTION **owner;
    lnd_spinlock *lock;
    LND_SUBSCRIPTION_CONFIG config;
    lnd_atomic_u32 head;
    lnd_atomic_u32 tail;
    lnd_atomic_u64 dropped;
    bool fired;
    bool automatic;
    bool source_position;
    uint32_t rate;
    LND_NOTIFICATION queue[];
};

static LND_SUBSCRIPTION *lnd_subscriptions;

static bool lnd_subscription_valid(const LND_SUBSCRIPTION *s) {
    for (LND_SUBSCRIPTION *p = lnd_subscriptions; p; p = p->next)
        if (p == s)
            return true;
    return false;
}

static uint64_t lnd_notify_scale(uint64_t frames, uint32_t from, uint32_t to) {
    if (!to || from == to)
        return frames;
    uint64_t whole = frames / from, fraction = ((frames % from) * to + from / 2) / from;
    return whole > (UINT64_MAX - fraction) / to ? UINT64_MAX : whole * to + fraction;
}

static void lnd_notify_push(LND_SUBSCRIPTION *s, const LND_NOTIFICATION *event) {
    if (s->fired && (s->config.flags & LND_NOTIFY_ONCE))
        return;
    uint32_t head = lnd_load_relaxed(&s->head);
    if (head - lnd_load(&s->tail) == s->config.notification_capacity) {
        lnd_add(&s->dropped, 1);
    } else {
        s->queue[head & (s->config.notification_capacity - 1)] = *event;
        lnd_store(&s->head, head + 1);
        if (s->automatic)
            lnd_notify_dispatch_wake();
    }
    s->fired = true;
}

void lnd_notify_emit(LND_SUBSCRIPTION *head, int32_t type, uint64_t position, uint32_t rate, int32_t param, float value) {
    for (LND_SUBSCRIPTION *s = head; s; s = s->owner_next) {
        if (s->config.type != type || (type == LND_NOTIFY_SLIDE_END && s->config.param != param))
            continue;
        LND_NOTIFICATION event = {.type = type,
                                  .position_frames = lnd_notify_scale(position, rate, s->rate),
                                  .sample_rate_hz = s->rate ? s->rate : rate,
                                  .param = param,
                                  .value = value};
        lnd_notify_push(s, &event);
    }
}

void lnd_notify_transport(LND_SUBSCRIPTION *head, int32_t type, uint64_t source_position, uint64_t node_position, uint32_t rate) {
    for (LND_SUBSCRIPTION *s = head; s; s = s->owner_next) {
        if (s->config.type != type)
            continue;
        uint64_t position = s->source_position ? source_position : node_position;
        LND_NOTIFICATION event = {.type = type, .position_frames = lnd_notify_scale(position, rate, s->rate), .sample_rate_hz = s->rate ? s->rate : rate};
        lnd_notify_push(s, &event);
    }
}

void lnd_notify_range(LND_SUBSCRIPTION *head, uint64_t start, uint64_t end, uint32_t rate, bool source_position) {
    if (end <= start)
        return;
    for (LND_SUBSCRIPTION *s = head; s; s = s->owner_next) {
        uint64_t at = s->config.position_frames;
        if (s->config.type != LND_NOTIFY_POSITION || s->source_position != source_position)
            continue;
        uint64_t from = lnd_notify_scale(start, rate, s->rate), to = lnd_notify_scale(end, rate, s->rate);
        if ((at > from && at <= to) || (!at && !start)) {
            LND_NOTIFICATION event = {.type = LND_NOTIFY_POSITION, .position_frames = at, .sample_rate_hz = s->rate ? s->rate : rate};
            lnd_notify_push(s, &event);
        }
    }
}

static LND_SUBSCRIPTION *lnd_subscribe(LND_SUBSCRIPTION **owner, lnd_spinlock *lock, const LND_SUBSCRIPTION_CONFIG *config, bool source_position, uint32_t rate) {
    if (!lnd_ctx.initialized)
        return lnd_error_null(LND_ERR_STATE);
    if (!config || config->type < LND_NOTIFY_END || config->type > LND_NOTIFY_RESUMED || (config->flags & ~(LND_NOTIFY_ONCE | LND_NOTIFY_MANUAL)) ||
        config->notification_capacity > 65536)
        return lnd_error_null(LND_ERR_INVALID_ARG);
    uint32_t capacity = lnd_next_pow2_u32(config->notification_capacity ? config->notification_capacity : 16);
    LND_SUBSCRIPTION *s = lnd_alloc_zero(sizeof *s + (size_t)capacity * sizeof *s->queue);
    if (!s)
        return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    s->automatic = LND_THREADS && config->proc && !(config->flags & LND_NOTIFY_MANUAL) && lnd_cfg_u32(LND_CFG_RUN_MODE) == LND_MODE_REALTIME;
    if (s->automatic) {
        int32_t result = lnd_notify_dispatch_start();
        if (result != LND_OK) {
            lnd_free(s);
            return lnd_error_null(result);
        }
    }
    s->config = *config;
    s->config.notification_capacity = capacity;
    s->source_position = source_position;
    s->rate = rate;
    s->owner = owner;
    s->lock = lock;
    lnd_spinlock_lock(lock);
    s->owner_next = *owner;
    *owner = s;
    lnd_spinlock_unlock(lock);
    s->next = lnd_subscriptions;
    lnd_subscriptions = s;
    return s;
}

#if LND_MODULE_GRAPH
static LND_SUBSCRIPTION *lnd_node_subscribe(LND_NODE *node, const LND_SUBSCRIPTION_CONFIG *config, bool source_position, uint32_t rate) {
    if (!node || !lnd_context_has_node(node))
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (config && config->type == LND_NOTIFY_SLIDE_END) {
#if LND_MODULE_SLIDE
        if (config->param != LND_PARAM_GAIN &&
            (node->type != LND_NODE_PROCESSOR || config->param < LND_PARAM_USER || config->param >= LND_PARAM_USER + LND_PARAM_USER_COUNT))
            return lnd_error_null(LND_ERR_INVALID_ARG);
#else
        return lnd_error_null(LND_ERR_UNSUPPORTED);
#endif
    }
    return lnd_subscribe(&node->notifications, &node->lock, config, source_position, rate);
}

LND_SUBSCRIPTION *LND_NodeSubscribe(LND_NODE *node, const LND_SUBSCRIPTION_CONFIG *config) {
    if (!lnd_context_enter())
        return lnd_error_null(LND_ERR_BUSY);
    LND_SUBSCRIPTION *s = lnd_node_subscribe(node, config, false, 0);
    lnd_context_unlock();
    return s;
}

void lnd_notify_node(LND_NODE *n, uint32_t frames) {
    uint64_t start = n->notification_position;
    n->notification_position += frames;
    if (n->notifications)
        lnd_notify_range(n->notifications, start, n->notification_position, n->sample_rate_hz, false);
    if (n->type == LND_NODE_SOURCE)
        return;
    LND_SOUND *sound = lnd_node_pcm_sound(n);
    if (sound) {
        lnd_native_sound *s = (lnd_native_sound *)sound;
        uint32_t state = lnd_load(&s->state);
        if (state == LND_SOUND_PAUSED || (state == LND_SOUND_STOPPED && lnd_load(&s->source->status) != LND_SOURCE_EOF))
            return;
    }
    int32_t status = lnd_node_status(n, nullptr);
    if (status == n->notification_status)
        return;
    int32_t previous = n->notification_status;
    n->notification_status = status;
    int32_t event = status == LND_SOURCE_EOF                                       ? LND_NOTIFY_END
                    : status == LND_SOURCE_WAITING                                 ? LND_NOTIFY_STALLED
                    : status == LND_SOURCE_READY && previous == LND_SOURCE_WAITING ? LND_NOTIFY_RESUMED
                                                                                   : -1;
    if (event >= 0)
        lnd_notify_emit(n->notifications, event, n->notification_position, n->sample_rate_hz, 0, 0);
}
#else
LND_SUBSCRIPTION *LND_NodeSubscribe(LND_NODE *node, const LND_SUBSCRIPTION_CONFIG *config) {
    LND_UNUSED(node);
    LND_UNUSED(config);
    return lnd_error_null(LND_ERR_UNSUPPORTED);
}
#endif

LND_SUBSCRIPTION *LND_SoundSubscribe(LND_SOUND *sound, const LND_SUBSCRIPTION_CONFIG *config) {
    if (!sound)
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter())
        return lnd_error_null(LND_ERR_BUSY);
    LND_SUBSCRIPTION *s = nullptr;
#if LND_MODULE_GRAPH
    sound = lnd_sound_view(sound);
    if (sound->ops) {
        lnd_node *node = sound->ops->SoundGetNode(sound);
        LND_SOUND *pcm = node ? lnd_node_pcm_sound(node) : nullptr;
        uint32_t rate = LND_SoundGetSampleRateHz(sound);
        if (pcm && config && config->type != LND_NOTIFY_SLIDE_END) {
            lnd_native_sound *native = (lnd_native_sound *)pcm;
            s = lnd_subscribe(&native->notifications, &native->source->lock, config, true, rate);
        } else if (config && config->type == LND_NOTIFY_POSITION && node && node->type != LND_NODE_SOURCE) {
            lnd_error(LND_ERR_UNSUPPORTED);
        } else {
            s = lnd_node_subscribe(node, config, node && node->type == LND_NODE_SOURCE, node && node->type == LND_NODE_SOURCE ? rate : 0);
        }
    } else
#endif
    {
        lnd_native_sound *native = (lnd_native_sound *)sound;
        if (!native->source)
            lnd_error(LND_ERR_STATE);
        else if (config && config->type == LND_NOTIFY_SLIDE_END)
            lnd_error(LND_ERR_UNSUPPORTED);
        else
            s = lnd_subscribe(&native->notifications, &native->source->lock, config, true, 0);
    }
    lnd_context_unlock();
    return s;
}

static bool lnd_subscription_pop(LND_SUBSCRIPTION *s, LND_NOTIFICATION *event) {
    uint32_t tail = lnd_load_relaxed(&s->tail);
    if (tail == lnd_load(&s->head))
        return false;
    *event = s->queue[tail & (s->config.notification_capacity - 1)];
    lnd_store(&s->tail, tail + 1);
    return true;
}

int32_t LND_SubscriptionRead(LND_SUBSCRIPTION *s, LND_NOTIFICATION *event) {
    if (!s || !event)
        return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter())
        return lnd_error(LND_ERR_BUSY);
    int32_t result = !lnd_subscription_valid(s) ? LND_ERR_INVALID_ARG : s->automatic ? LND_ERR_STATE : lnd_subscription_pop(s, event) ? 1 : 0;
    lnd_context_unlock();
    return result < 0 ? lnd_error(result) : result;
}

uint64_t LND_SubscriptionGetDroppedCount(const LND_SUBSCRIPTION *s) {
    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }
    uint64_t count = lnd_subscription_valid(s) ? lnd_load(&s->dropped) : 0;
    lnd_context_unlock();
    return count;
}

bool LND_SubscriptionIsAttached(const LND_SUBSCRIPTION *s) {
    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return false;
    }
    bool attached = lnd_subscription_valid(s) && s->owner;
    lnd_context_unlock();
    return attached;
}

bool LND_SubscriptionIsAutomatic(const LND_SUBSCRIPTION *s) {
    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return false;
    }
    bool automatic = lnd_subscription_valid(s) && s->automatic;
    lnd_context_unlock();
    return automatic;
}

void lnd_notify_detach(LND_SUBSCRIPTION **head) {
    for (LND_SUBSCRIPTION *s = *head; s; s = s->owner_next) {
        s->owner = nullptr;
        s->lock = nullptr;
    }
    *head = nullptr;
}

static void lnd_subscription_unlink(LND_SUBSCRIPTION *s) {
    if (s->owner) {
        lnd_spinlock_lock(s->lock);
        for (LND_SUBSCRIPTION **p = s->owner; *p; p = &(*p)->owner_next)
            if (*p == s) {
                *p = s->owner_next;
                break;
            }
        lnd_spinlock_unlock(s->lock);
        s->owner = nullptr;
        s->lock = nullptr;
    }
}

int32_t LND_SubscriptionFree(LND_SUBSCRIPTION *s) {
    if (!lnd_context_enter())
        return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_INVALID_ARG;
    for (LND_SUBSCRIPTION **p = &lnd_subscriptions; *p; p = &(*p)->next) {
        if (*p != s)
            continue;
        *p = s->next;
        lnd_subscription_unlink(s);
        lnd_free(s);
        result = LND_OK;
        break;
    }
    lnd_context_unlock();
    return lnd_error(result);
}

void lnd_notify_dispatch(bool automatic) {
    for (LND_SUBSCRIPTION *s = lnd_subscriptions; s; s = s->next) {
        if (!s->config.proc || s->automatic != automatic)
            continue;
        uint32_t count = lnd_load(&s->head) - lnd_load_relaxed(&s->tail);
        LND_NOTIFICATION event;
        lnd_callback_enter();
        while (count-- && lnd_subscription_pop(s, &event))
            s->config.proc(s->config.user, &event);
        lnd_callback_leave();
    }
}

void lnd_notify_update(void) { lnd_notify_dispatch(false); }

void lnd_notify_stop(void) {
    for (LND_SUBSCRIPTION *s = lnd_subscriptions; s; s = s->next)
        lnd_subscription_unlink(s);
    lnd_notify_dispatch_stop();
}

void lnd_notify_free(void) {
    while (lnd_subscriptions) {
        LND_SUBSCRIPTION *s = lnd_subscriptions;
        lnd_subscriptions = s->next;
        lnd_subscription_unlink(s);
        lnd_free(s);
    }
}
