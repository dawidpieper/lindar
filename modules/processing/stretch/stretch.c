#include "stretch.h"
#include "src/context.h"
#include "src/error.h"
#include "playback/graph/context.h"
#include "playback/graph/node.h"
#include "lnd_stretch_backends.h"

#include <math.h>
#include <string.h>

static const lnd_stretch_backend *lnd_stretch_find(const LND_STRETCH_BACKEND *backend) {
    for (size_t i = 0; lnd_stretch_backends[i]; i++)
        if (lnd_stretch_backends[i] == backend) return lnd_stretch_backends[i];
    return nullptr;
}

uint32_t LND_StretchBackendGetCount(void) { return sizeof lnd_stretch_backends / sizeof *lnd_stretch_backends - 1; }

const LND_STRETCH_BACKEND *LND_StretchBackendGet(uint32_t index) {
    return index < LND_StretchBackendGetCount() ? lnd_stretch_backends[index] : nullptr;
}

const LND_STRETCH_BACKEND *LND_StretchBackendFind(const char *name) {
    if (!name) return nullptr;
    for (size_t i = 0; lnd_stretch_backends[i]; i++)
        if (!strcmp(lnd_stretch_backends[i]->name, name)) return lnd_stretch_backends[i];
    return nullptr;
}

const char *LND_StretchBackendGetName(const LND_STRETCH_BACKEND *backend) {
    return lnd_stretch_find(backend) ? backend->name : nullptr;
}

static const lnd_stretch_backend *lnd_stretch_match(const LND_NODE *node) {
    if (!lnd_context_has_node(node))
        return nullptr;
    for (size_t i = 0; lnd_stretch_backends[i]; i++)
        if (lnd_stretch_backends[i]->matches(node))
            return lnd_stretch_backends[i];
    return nullptr;
}

bool lnd_stretch_param_valid(void *user, int32_t param, float value) {
    LND_UNUSED(user);
    return param >= LND_STRETCH_PARAM_TEMPO_RATIO && param <= LND_STRETCH_PARAM_RATE_RATIO && isfinite(value) && value >= 0.25f && value <= 4;
}

int32_t LND_StretchBackendSetPriority(const LND_STRETCH_BACKEND *backend, int32_t priority) {
    const lnd_stretch_backend *impl = lnd_stretch_find(backend);
    if (!impl || priority < 0) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = lnd_ctx.initialized ? LND_ERR_STATE : LND_OK;
    if (result == LND_OK) *impl->priority = priority;
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_StretchBackendGetPriority(const LND_STRETCH_BACKEND *backend) {
    if (!lnd_stretch_find(backend)) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = *backend->priority;
    lnd_context_unlock();
    return result;
}

LND_NODE *LND_NodeCreateStretch(uint32_t channels, uint32_t sample_rate_hz, const LND_STRETCH_CONFIG *config) {
    LND_STRETCH_CONFIG c = config ? *config : (LND_STRETCH_CONFIG){0};
    if (!c.tempo_ratio)
        c.tempo_ratio = 1;
    if (!c.pitch_ratio)
        c.pitch_ratio = 1;
    if (!c.rate_ratio)
        c.rate_ratio = 1;
    if ((c.backend && !lnd_stretch_find(c.backend)) ||
        !lnd_stretch_param_valid(nullptr, LND_STRETCH_PARAM_TEMPO_RATIO, c.tempo_ratio) || !lnd_stretch_param_valid(nullptr, LND_STRETCH_PARAM_PITCH_RATIO, c.pitch_ratio) ||
        !lnd_stretch_param_valid(nullptr, LND_STRETCH_PARAM_RATE_RATIO, c.rate_ratio))
        return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter())
        return lnd_error_null(LND_ERR_BUSY);
    const lnd_stretch_backend *selected = c.backend;
    if (!c.backend) {
        int32_t priority = 0;
        for (size_t i = 0; lnd_stretch_backends[i]; i++) {
            const lnd_stretch_backend *candidate = lnd_stretch_backends[i];
            if (*candidate->priority > priority || (priority && *candidate->priority == priority && strcmp(candidate->name, selected->name) < 0)) {
                priority = *candidate->priority;
                selected = candidate;
            }
        }
    }
    LND_NODE *node = !lnd_ctx.initialized ? lnd_error_null(LND_ERR_STATE)
                     : selected           ? selected->create(channels, sample_rate_hz, &c)
                                          : lnd_error_null(LND_ERR_UNSUPPORTED);
    lnd_context_unlock();
    return node;
}

LND_NODE *LND_NodeCreatePitch(uint32_t channels, uint32_t sample_rate_hz, float pitch_ratio) {
    if (!lnd_stretch_param_valid(nullptr, LND_STRETCH_PARAM_PITCH_RATIO, pitch_ratio))
        return lnd_error_null(LND_ERR_INVALID_ARG);
    return LND_NodeCreateStretch(channels, sample_rate_hz, &(LND_STRETCH_CONFIG){.pitch_ratio = pitch_ratio});
}

LND_NODE *LND_NodeCreateTempo(uint32_t channels, uint32_t sample_rate_hz, float tempo_ratio) {
    if (!lnd_stretch_param_valid(nullptr, LND_STRETCH_PARAM_TEMPO_RATIO, tempo_ratio))
        return lnd_error_null(LND_ERR_INVALID_ARG);
    return LND_NodeCreateStretch(channels, sample_rate_hz, &(LND_STRETCH_CONFIG){.tempo_ratio = tempo_ratio});
}

const LND_STRETCH_BACKEND *LND_NodeGetStretchBackend(const LND_NODE *node) {
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    const lnd_stretch_backend *backend = lnd_stretch_match(node);
    if (!backend) lnd_error(LND_ERR_INVALID_ARG);
    lnd_context_unlock();
    return backend;
}

int32_t LND_NodeGetStretchInfo(const LND_NODE *object, LND_STRETCH_INFO *info) {
    LND_NODE *node = (LND_NODE *)object;
    if (!info)
        return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter())
        return lnd_error(LND_ERR_BUSY);
    const lnd_stretch_backend *backend = lnd_stretch_match(node);
    int32_t result = backend ? backend->info(node, info) : LND_ERR_INVALID_ARG;
    lnd_context_unlock();
    return lnd_error(result);
}

static int32_t lnd_stretch_command(LND_NODE *node, bool reset) {
    if (!lnd_context_enter())
        return lnd_error(LND_ERR_BUSY);
    const lnd_stretch_backend *backend = lnd_stretch_match(node);
    int32_t result = !backend ? LND_ERR_INVALID_ARG : reset ? backend->reset(node) : backend->flush(node);
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_NodeResetStretch(LND_NODE *node) { return lnd_stretch_command(node, true); }
int32_t LND_NodeEndStretchInput(LND_NODE *node) { return lnd_stretch_command(node, false); }

static int32_t lnd_stretch_set(LND_NODE *node, int32_t param, float value) {
    if (!lnd_context_enter())
        return lnd_error(LND_ERR_BUSY);
    int32_t result = lnd_stretch_match(node) ? LND_NodeSetParam(node, param, value) : LND_ERR_INVALID_ARG;
    lnd_context_unlock();
    return lnd_error(result);
}

int32_t LND_NodeSetStretchPitchSemitones(LND_NODE *node, float semitones) {
    if (!isfinite(semitones) || semitones < -24 || semitones > 24)
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_stretch_set(node, LND_STRETCH_PARAM_PITCH_RATIO, exp2f(semitones / 12));
}

float LND_NodeGetStretchPitchSemitones(const LND_NODE *node) {
    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }
    float result = 0;
    if (lnd_stretch_match(node))
        result = 12 * log2f(LND_NodeGetParam(node, LND_STRETCH_PARAM_PITCH_RATIO));
    else
        lnd_error(LND_ERR_INVALID_ARG);
    lnd_context_unlock();
    return result;
}

int32_t LND_NodeSetStretchTempoChangePercent(LND_NODE *node, float percent) {
    if (!isfinite(percent))
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_stretch_set(node, LND_STRETCH_PARAM_TEMPO_RATIO, 1 + percent * 0.01f);
}

int32_t LND_NodeSetStretchRateChangePercent(LND_NODE *node, float percent) {
    if (!isfinite(percent))
        return lnd_error(LND_ERR_INVALID_ARG);
    return lnd_stretch_set(node, LND_STRETCH_PARAM_RATE_RATIO, 1 + percent * 0.01f);
}

void lnd_stretch_free(void) {
    for (size_t i = 0; lnd_stretch_backends[i]; i++)
        *lnd_stretch_backends[i]->priority = lnd_stretch_backends[i]->default_priority;
}
