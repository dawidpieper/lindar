#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lnd_bg_request {
    double position;
    double speed;
    double pitch_ratio;
    int32_t mode;
    int32_t reset;
} lnd_bg_request;

typedef struct lnd_bg_chunk {
    const float *data;
    double begin;
    double end;
    intptr_t stride_samples;
    uint32_t frames;
} lnd_bg_chunk;

void *lnd_bg_create(uint32_t channels, uint32_t sample_rate_hz, int32_t grain_adjust, int32_t *error);
void lnd_bg_destroy(void *engine);
uint32_t lnd_bg_max_input(void *engine);
void lnd_bg_preroll(void *engine, lnd_bg_request *request);
void lnd_bg_next(void *engine, lnd_bg_request *request);
int32_t lnd_bg_specify(void *engine, const lnd_bg_request *request, double origin, int32_t *begin, int32_t *end);
int32_t lnd_bg_process(void *engine, const float *pcm, uint32_t stride_samples, lnd_bg_chunk *chunk);
const char *lnd_bg_version(void);

#ifdef __cplusplus
}
#endif
