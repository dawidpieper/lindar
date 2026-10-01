#include "lindar_graph.h"
#include "src/native.h"
#include "context.h"
#include "node.h"
#include "src/error.h"

LND_NODE *LND_SourceGetNode(const LND_SOURCE *source) {
    if (!source) return nullptr;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    LND_NODE *node = source->ops ? source->ops->SourceGetNode(source) :
        lnd_pcm_input_node((LND_SOUND *)((const lnd_native_source *)source)->sound, false);
    lnd_context_unlock();
    return node;
}

LND_NODE *LND_SoundGetNode(const LND_SOUND *sound) {
    if (!sound) return nullptr;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    LND_NODE *node = sound->ops ? sound->ops->SoundGetNode(sound) : lnd_pcm_input_node((LND_SOUND *)sound, false);
    lnd_context_unlock();
    return node;
}

LND_NODE *LND_SourceEnsureNode(LND_SOURCE *s) {
    if (s && s->ops) return s->ops->SourceEnsureNode(s);
    LND_SOUND *sound = LND_SourceEnsureSound(s, nullptr);
    return sound ? LND_SoundEnsureNode(sound) : nullptr;
}

LND_NODE *LND_SoundEnsureNode(LND_SOUND *s) {
    if (s && s->ops) return s->ops->SoundGetNode(s);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_node *node = lnd_pcm_input_node((LND_SOUND *)s, true);
    lnd_context_unlock();
    return node;
}

int32_t LND_SoundSetOutput(LND_SOUND *s, LND_NODE *dst) {
    if (s && s->ops) return s->ops->SoundSetOutput(s, dst);
    if (!s) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_OK;
    lnd_node *node = nullptr;
    if (dst && !lnd_context_has_node(dst)) result = LND_ERR_INVALID_ARG;
    else {
        node = lnd_pcm_input_node(s, dst != nullptr);
        if (!node && dst) result = LND_ErrorGetLast();
        if (node && dst && (dst == node || lnd_node_reaches(dst, node))) result = LND_ERR_CYCLE;
        if (result == LND_OK && node) {
            result = lnd_node_set_output(node, dst);
        }
    }
    lnd_context_unlock();
    return lnd_error(result);
}

LND_NODE *LND_SoundGetOutput(const LND_SOUND *s) {
    if (s && s->ops) return s->ops->SoundGetOutput(s);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_node *node = lnd_pcm_input_node((LND_SOUND *)s, false);
    LND_NODE *output = node && node->outputs ? node->outputs->dst : nullptr;
    lnd_context_unlock();
    return output;
}
