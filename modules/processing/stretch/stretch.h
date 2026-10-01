#pragma once

#include "lindar_stretch.h"

enum {
    LND_STRETCH_TEMPO_INDEX,
    LND_STRETCH_PITCH_INDEX,
    LND_STRETCH_RATE_INDEX,
    LND_STRETCH_PARAM_COUNT,
};

typedef struct LND_STRETCH_BACKEND lnd_stretch_backend;

struct LND_STRETCH_BACKEND {
    const char *name;
    int32_t *priority;
    int32_t default_priority;
    LND_NODE *(*create)(uint32_t channels, uint32_t sample_rate_hz, const LND_STRETCH_CONFIG *config);
    bool (*matches)(const LND_NODE *node);
    int32_t (*info)(const LND_NODE *node, LND_STRETCH_INFO *info);
    int32_t (*reset)(LND_NODE *node);
    int32_t (*flush)(LND_NODE *node);
};

bool lnd_stretch_param_valid(void *user, int32_t param, float value);
