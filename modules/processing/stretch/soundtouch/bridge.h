#pragma once

#include <stdint.h>

enum {
    LND_ST_INPUT_SEQUENCE_FRAMES = 6,
    LND_ST_OUTPUT_SEQUENCE_FRAMES = 7,
    LND_ST_INITIAL_LATENCY_FRAMES = 8,
};

#ifdef __cplusplus
extern "C" {
#endif

void *lnd_st_create(uint32_t channels, uint32_t sample_rate_hz, int32_t *error);
void lnd_st_destroy(void *engine);
int32_t lnd_st_param(void *engine, int32_t param, float value);
int32_t lnd_st_setting(void *engine, int32_t setting, int32_t value);
int32_t lnd_st_get_setting(void *engine, int32_t setting);
int32_t lnd_st_put(void *engine, const float *pcm, uint32_t frames);
int64_t lnd_st_receive(void *engine, float *pcm, uint32_t frames);
uint32_t lnd_st_available(void *engine);
uint32_t lnd_st_pending(void *engine);
int32_t lnd_st_clear(void *engine);
const char *lnd_st_version(void);

#ifdef __cplusplus
}
#endif
