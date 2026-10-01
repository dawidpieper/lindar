#pragma once

#include "src/platform.h"
#include "src/thread.h"
#include "io/io.h"
#include "lindar_output.h"

struct LND_OUTPUT {
    const LND_ENCODER *encoder;
#if LND_MODULE_METADATA
    LND_METADATA *metadata;
#endif
    void *state;
    lnd_io *io;
    uint32_t channels;
    uint32_t sample_rate_hz;
    uint64_t frames;
    float *scratch;
    uint32_t scratch_frames;
    lnd_mutex lock;
    int32_t failed;
    int32_t finish_result;
    uint64_t bytes;
    bool finished;
    struct LND_OUTPUT *prev;
    struct LND_OUTPUT *next;
};

typedef struct LND_OUTPUT lnd_output;

void lnd_outputs_free_all(void);
bool lnd_encoder_option(const char *options, const char *key, char *out, size_t cap);
int64_t lnd_encoder_option_int(const char *options, const char *key, int64_t fallback);
uint32_t lnd_encoder_level(const LND_ENCODER_PARAMS *p, uint32_t fallback, uint32_t max);

int32_t lnd_output_validate(const LND_ENCODER_PARAMS *params, const char *ext);
bool lnd_output_params_valid(const LND_ENCODER_PARAMS *params);
lnd_output *lnd_output_create_io(lnd_io *io, const LND_ENCODER_PARAMS *params, const char *ext);
