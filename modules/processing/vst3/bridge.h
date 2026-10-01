#pragma once
#include "lindar_vst3.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {
    VST_INFO,
    VST_PARAMETER,
    VST_GET,
    VST_SET,
    VST_FORMAT,
    VST_PARSE,
    VST_BYPASS,
    VST_PROGRAM_LIST,
    VST_PROGRAM_NAME,
    VST_PROGRAM,
    VST_SAVE,
    VST_LOAD,
    VST_TRANSPORT,
    VST_RESET,
    VST_DISPATCH,
    VST_EDITOR_OPEN,
    VST_EDITOR_CLOSE,
    VST_EDITOR_SIZE,
    VST_EDITOR_RESIZE
};
typedef struct lnd_vst3_args {
    uint32_t index;
    uint32_t id;
    double value;
    void *data;
    const void *input;
    size_t bytes;
    size_t *size;
    void *parent;
    int32_t platform;
    LND_VST3_RESIZE_PROC resize;
    void *user;
} lnd_vst3_args;
int32_t lnd_vst3_scan(const char *path, uint32_t index, LND_VST3_CLASS *info, uint32_t *count);
int32_t lnd_vst3_create(const LND_VST3_OPTIONS *options, void **engine);
void lnd_vst3_destroy(void *engine);
int32_t lnd_vst3_process(void *engine, const LND_PCM *pcm, size_t offset, uint32_t frames);
int32_t lnd_vst3_action(void *engine, int32_t action, lnd_vst3_args *args);
uint32_t lnd_vst3_tail(void *engine);
int32_t lnd_vst3_utf8(const uint16_t *src, char *dst, size_t capacity);
int32_t lnd_vst3_utf16(const char *src, uint16_t *dst, size_t capacity);
#ifdef __cplusplus
}
#endif
