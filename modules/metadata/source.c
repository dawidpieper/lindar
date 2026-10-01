#include "internal.h"
#include "src/native.h"
#if LND_MODULE_GRAPH
#include "playback/graph/sound.h"
#include "playback/graph/context.h"
#endif
#include "src/context.h"
#include "src/callback.h"

static bool valid_source(const LND_SOURCE *source) {
    if (lnd_native_source_registered(source)) return true;
#if LND_MODULE_GRAPH
    for (lnd_graph_source *s = lnd_graph_ctx.sources; s; s = s->next)
        if (&s->base == source) return true;
    for (lnd_node *n = lnd_graph_ctx.nodes; n; n = n->next)
        if (n->view && &n->view->base == source) return true;
#endif
    return false;
}

int32_t LND_SourceSetMetadata(LND_SOURCE *source, const LND_METADATA *metadata) {
    if (!source) return LND_ERR_INVALID_ARG;
    if (lnd_callback_active()) return LND_ERR_BUSY;
    LND_METADATA *copy = metadata ? LND_MetadataClone(metadata) : nullptr;
    if (metadata && !copy) return LND_ERR_OUT_OF_MEMORY;
    if (!lnd_context_enter()) {
        LND_MetadataFree(copy);
        return LND_ERR_BUSY;
    }
    int32_t r = valid_source(source) ? LND_OK : LND_ERR_INVALID_ARG;
    LND_METADATA *old = nullptr;
    if (!r) {
        source->metadata_provider = nullptr;
        old = source->metadata;
        source->metadata = copy;
        source->metadata_status = copy ? LND_OK : LND_METADATA_ERR_NOT_FOUND;
    }
    lnd_context_unlock();
    LND_MetadataFree(r ? copy : old);
    return r;
}

int32_t LND_SourceCopyMetadata(const LND_SOURCE *source, LND_METADATA *metadata) {
    if (!source || !metadata) return LND_ERR_INVALID_ARG;
    if (lnd_callback_active() || !lnd_context_enter()) return LND_ERR_BUSY;
    if (valid_source(source) && source->metadata_provider) {
        int32_t result = source->metadata_provider(source, metadata);
        lnd_context_unlock();
        return result;
    }
    int32_t r = !valid_source(source)     ? LND_ERR_INVALID_ARG
                : source->metadata        ? LND_OK
                : source->metadata_status ? source->metadata_status
                                          : LND_METADATA_ERR_NOT_FOUND;
    LND_METADATA *snapshot = !r ? lnd_tag_ref(source->metadata) : nullptr;
    lnd_context_unlock();
    if (!r) r = lnd_tag_assign(metadata, snapshot);
    LND_MetadataFree(snapshot);
    return r;
}

int32_t LND_SourceGetMetadataStatus(const LND_SOURCE *source) {
    if (!source) return LND_ERR_INVALID_ARG;
    if (lnd_callback_active() || !lnd_context_enter()) return LND_ERR_BUSY;
    if (valid_source(source) && source->metadata_provider) {
        int32_t result = source->metadata_provider(source, nullptr);
        lnd_context_unlock();
        return result;
    }
    int32_t r = !valid_source(source)     ? LND_ERR_INVALID_ARG
                : source->metadata        ? LND_OK
                : source->metadata_status ? source->metadata_status
                                          : LND_METADATA_ERR_NOT_FOUND;
    lnd_context_unlock();
    return r;
}
