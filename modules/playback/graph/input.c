#include "context.h"
#include "node.h"
#include "sound.h"
#include "src/error.h"
#include "src/render.h"
#include "src/native.h"

#include <string.h>

typedef struct lnd_pcm_input {
    lnd_node base;
    LND_SOUND *sound;
    LND_RENDERER *renderer;
} lnd_pcm_input;

LND_SOUND *lnd_node_pcm_sound(const lnd_node *node) { return node && node->type == LND_NODE_PCM_INPUT ? ((const lnd_pcm_input *)node)->sound : nullptr; }

static void lnd_pcm_input_render_pcm(lnd_node *node, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_pcm_input *input = (lnd_pcm_input *)node;
    int64_t got = input->renderer ? lnd_renderer_read(input->renderer, pcm, offset, frames, nullptr) : LND_ERR_STATE;
    node->rendered = got > 0 ? (uint32_t)got : 0;
    if (node->rendered < frames) LND_PcmSilence(pcm, offset + node->rendered, frames - node->rendered);
}

static void lnd_pcm_input_render(lnd_node *node, float *dst, uint32_t frames) {
    LND_PCM pcm = {.data = dst, .frames = frames, .channels = node->channels, .format = LND_FORMAT_F32};
    lnd_pcm_input_render_pcm(node, &pcm, 0, frames);
}

static void lnd_pcm_input_close(void *sound) {
    lnd_node *node = ((lnd_native_sound *)sound)->node;
    if (node) ((lnd_pcm_input *)node)->renderer = nullptr;
}

static void lnd_pcm_input_destroy(lnd_node *node) {
    lnd_pcm_input *input = (lnd_pcm_input *)node;
    if (input->renderer) lnd_renderer_free(input->renderer);
    lnd_store(&((lnd_native_sound *)input->sound)->view, nullptr);
    ((lnd_native_sound *)input->sound)->node = nullptr;
}

static const lnd_node_vt lnd_pcm_input_vt = {.render = lnd_pcm_input_render, .render_pcm = lnd_pcm_input_render_pcm, .destroy = lnd_pcm_input_destroy};

lnd_node *lnd_pcm_input_node(LND_SOUND *sound, bool create) {
    if (!sound) return create ? lnd_error_null(LND_ERR_INVALID_ARG) : nullptr;
    lnd_node *existing = ((lnd_native_sound *)sound)->node;
    if (existing) return existing;
    if (!create) return nullptr;
    if (!LND_SoundGetSource(sound)) return lnd_error_null(LND_ERR_STATE);
    uint32_t channels = LND_SoundGetChannels(sound), sample_rate_hz = LND_SoundGetSampleRateHz(sound);
    lnd_pcm_input *input = (lnd_pcm_input *)lnd_node_alloc(sizeof *input, &lnd_pcm_input_vt, LND_NODE_PCM_INPUT, channels, sample_rate_hz);
    if (!input) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    input->sound = sound;
    LND_RENDERER_CONFIG config = {
        .render = lnd_native_sound_render_pcm, .close = lnd_pcm_input_close, .user = sound, .channels = channels, .sample_rate_hz = sample_rate_hz, .block_frames = input->base.block};
    input->renderer = lnd_renderer_create(&config);
    if (!input->renderer) {
        int32_t error = LND_ErrorGetLast();
        lnd_node_destroy(&input->base);
        return lnd_error_null(error);
    }
    ((lnd_native_sound *)sound)->node = &input->base;
    return &input->base;
}

LND_SOUND *lnd_graph_pcm_sound(LND_SOURCE *source, const LND_SOUND_CONFIG *config, bool configure) {
    const LND_SOUND_CONFIG c = config ? *config : (LND_SOUND_CONFIG){0};
    if (!source || source->ops || c.channels > LND_MAX_CHANNELS || (c.flags & ~(uint32_t)LND_SOUND_RESAMPLE_MASK) ||
        (c.flags & LND_SOUND_RESAMPLE_MASK) > LND_SOUND_RESAMPLE_SINC32) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_native_source *native = (lnd_native_source *)source;
    bool existed = native->sound != nullptr;
    LND_SOUND *sound = (LND_SOUND *)lnd_native_source_ensure_sound(native, nullptr);
    LND_SOUND *view = lnd_sound_view(sound);
    int32_t result = sound ? LND_OK : LND_ErrorGetLast();
    bool conversion = c.channel_matrix || c.flags || (c.sample_rate_hz && c.sample_rate_hz != native->config.sample_rate_hz) ||
                      (c.channels && c.channels != native->config.channels);
    if (sound && view != sound) {
        if (configure) result = view->ops->SoundSetConfig(view, config);
        else if (config && !lnd_graph_node_ensure_sound(view->ops->SoundGetNode(view), config)) result = LND_ErrorGetLast();
    } else if (sound && conversion) {
        lnd_node *node = lnd_pcm_input_node(sound, false);
        if (existed && !configure) result = LND_ERR_STATE;
        else if (native->sound->references > (node != nullptr)) result = LND_ERR_BUSY;
        else {
            bool created = node == nullptr;
            if (!node) node = lnd_pcm_input_node(sound, true);
            view = node ? (LND_SOUND *)lnd_graph_node_ensure_sound(node, config) : nullptr;
            if (view) lnd_store(&native->sound->view, view);
            else {
                result = LND_ErrorGetLast();
                if (created && node) lnd_node_destroy(node);
            }
        }
    }
    if (result != LND_OK && sound && !existed) LND_SoundFree(sound);
    lnd_context_unlock();
    return result == LND_OK ? sound : lnd_error_null(result);
}

uint64_t lnd_pcm_input_read(lnd_node *node, float *dst, uint64_t frames) {
    lnd_pcm_input *input = (lnd_pcm_input *)node;
    if (!input->renderer || frames > SIZE_MAX) return 0;
    LND_PCM pcm = {.data = dst, .frames = (size_t)frames, .channels = node->channels, .format = LND_FORMAT_F32};
    int64_t got = lnd_renderer_read(input->renderer, &pcm, 0, (size_t)frames, lnd_native_sound_read);
    return got > 0 ? (uint64_t)got : 0;
}
