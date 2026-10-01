#include "bridge.h"
#include "src/alloc.h"
#include "src/pcm.h"
#include "src/config.h"
#include "src/error.h"
#include "src/callback.h"
#include "playback/graph/node.h"
#include "playback/graph/native.h"
#include "playback/graph/context.h"
#include <string.h>
#include "utility/text/text.h"

typedef struct lnd_vst3 {
    void *engine;
    const lnd_node_vt *processor;
    uint64_t tail;
    uint64_t tail_limit;
    bool ended;
    bool signal;
    int32_t error;
} lnd_vst3;
static const lnd_node_vt vst_vt, vst_float_vt;

static void release(void *user) {
    lnd_vst3 *s = user;
    lnd_vst3_destroy(s->engine);
    lnd_free(s);
}
static void noop_float(void *user, float *pcm, uint32_t frames, uint32_t channels, uint32_t rate) {}
static int32_t noop(void *user, const LND_PCM *pcm, size_t offset, uint32_t frames, uint32_t rate) { return LND_OK; }
static bool validate(void *user, int32_t param, float value) { return false; }
static const LND_PROCESSOR_PROCS float_procs = {.process = noop_float, .release = release, .validate = validate, .flags = LND_PROCESSOR_BOUNDED};
static const LND_PROCESSOR_PROCS procs = {.process_pcm = noop, .release = release, .validate = validate, .flags = LND_PROCESSOR_BOUNDED};

static void render_pcm(lnd_node *node, const LND_PCM *pcm, size_t offset, uint32_t frames) {
    lnd_vst3 *s = lnd_processor_user(node);
    uint32_t count = 0;
    if (!s->ended && !s->error) {
        if (node->native && node->vt == &vst_vt) lnd_bus_render_pcm(node, pcm, offset, frames);
        else lnd_bus_render(node, (float *)lnd_pcm_at(pcm, 0, offset), frames);
        count = node->rendered;
        int32_t status = lnd_load(&node->status);
        if (count) s->signal = true;
        if (status < 0) s->error = status;
        if (status == LND_SOURCE_EOF) {
            s->ended = true;
            if (s->signal) s->tail = LND_MIN(s->tail_limit, lnd_vst3_tail(s->engine));
        }
    } else
        lnd_pcm_silence(pcm, offset, frames);
    if (s->ended && !s->error && count < frames && s->tail) {
        uint32_t extra = (uint32_t)LND_MIN(frames - count, s->tail);
        lnd_pcm_silence(pcm, offset + count, extra);
        count += extra;
        s->tail -= extra;
    }
    if (count && !s->error) {
        lnd_callback_enter();
        s->error = lnd_vst3_process(s->engine, pcm, offset, count);
        lnd_callback_leave();
    }
    if (s->error) count = 0;
    bool ended = s->ended && !s->tail;
    node->rendered = count;
    lnd_store(&node->status, s->error ? s->error : ended ? LND_SOURCE_EOF : count == frames ? LND_SOURCE_READY : LND_SOURCE_WAITING);
    lnd_store(&node->active, !s->error && !ended);
    if (count < frames) lnd_pcm_silence(pcm, offset + count, frames - count);
}
static void render(lnd_node *node, float *data, uint32_t frames) {
    LND_PCM pcm = {.data = data, .frames = frames, .channels = node->channels, .format = LND_FORMAT_F32};
    render_pcm(node, &pcm, 0, frames);
}
static void command(lnd_node *node, const lnd_cmd *cmd, bool immediate) { ((lnd_vst3 *)lnd_processor_user(node))->processor->command(node, cmd, immediate); }
static void destroy(lnd_node *node) { ((lnd_vst3 *)lnd_processor_user(node))->processor->destroy(node); }
static const lnd_node_vt vst_float_vt = {.render = render, .command = command, .destroy = destroy};
static const lnd_node_vt vst_vt = {.render = render, .command = command, .destroy = destroy, .render_pcm = render_pcm};

int32_t LND_Vst3ModuleScanClassCount(const char *path, uint32_t *count) {
    if (!path || !count) return LND_ERR_INVALID_ARG;
    if (lnd_callback_active()) return LND_ERR_BUSY;
    *count = 0;
    return lnd_vst3_scan(path, 0, nullptr, count);
}
int32_t LND_Vst3ModuleScanClass(const char *path, uint32_t index, LND_VST3_CLASS *info) {
    if (!path || !info) return LND_ERR_INVALID_ARG;
    if (lnd_callback_active()) return LND_ERR_BUSY;
    return lnd_vst3_scan(path, index, info, nullptr);
}
LND_NODE *LND_NodeCreateVst3(const LND_VST3_OPTIONS *options) {
    if (!options || !options->path) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    LND_VST3_OPTIONS o = *options;
    if (!o.channels) o.channels = 2;
    if (!o.sample_rate_hz) o.sample_rate_hz = 48000;
    if (!o.block_frames) o.block_frames = 1024;
    if (!o.tail_limit_ms) o.tail_limit_ms = 10000;
    if (o.channels > LND_MAX_CHANNELS || o.sample_rate_hz < 8000 || o.sample_rate_hz > 384000 || o.block_frames > 65536 || o.tail_limit_ms > 3600000)
        return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_vst3 *s = lnd_alloc_zero(sizeof *s);
    if (!s) return nullptr;
    int32_t r = lnd_vst3_create(&o, &s->engine);
    if (r) {
        release(s);
        return lnd_error_null(r);
    }
    s->tail_limit = (uint64_t)o.tail_limit_ms * o.sample_rate_hz / 1000;
    if (!lnd_context_enter()) {
        release(s);
        return lnd_error_null(LND_ERR_BUSY);
    }
    bool native = lnd_cfg_u32(LND_CFG_INTERNAL_FORMAT) == LND_FORMAT_F32;
    LND_NODE *node = LND_NodeCreateProcessor(native ? &procs : &float_procs, s, o.channels, o.sample_rate_hz);
    if (node) {
        s->processor = node->vt;
        node->vt = native ? &vst_vt : &vst_float_vt;
    }
    lnd_context_unlock();
    if (!node) release(s);
    return node;
}
static int32_t action(const LND_NODE *node, int32_t type, lnd_vst3_args args) {
    if (!node) return LND_ERR_INVALID_ARG;
    if (lnd_callback_active() || !lnd_context_enter()) return LND_ERR_BUSY;
    if (!lnd_context_has_node(node) || (node->vt != &vst_vt && node->vt != &vst_float_vt)) {
        lnd_context_unlock();
        return LND_ERR_INVALID_ARG;
    }
    lnd_node *n = (lnd_node *)node;
    lnd_spinlock_lock(&n->lock);
    lnd_vst3 *s = lnd_processor_user(n);
    lnd_callback_enter();
    int32_t r = lnd_vst3_action(s->engine, type, &args);
    lnd_callback_leave();
    if (!r && (type == VST_RESET || type == VST_LOAD)) {
        s->tail = 0;
        s->ended = s->signal = false;
        s->error = 0;
        lnd_store(&n->status, LND_SOURCE_READY);
        lnd_store(&n->active, 1);
        lnd_add(&n->revision, 1);
    }
    lnd_spinlock_unlock(&n->lock);
    lnd_context_unlock();
    return r;
}
int32_t LND_NodeGetVst3Info(const LND_NODE *n, LND_VST3_INFO *info) { return info ? action(n, VST_INFO, (lnd_vst3_args){.data = info}) : LND_ERR_INVALID_ARG; }
int32_t LND_NodeGetVst3Parameter(const LND_NODE *n, uint32_t index, LND_VST3_PARAMETER *info) {
    return info ? action(n, VST_PARAMETER, (lnd_vst3_args){.index = index, .data = info}) : LND_ERR_INVALID_ARG;
}
int32_t LND_NodeGetVst3ParameterValue(const LND_NODE *n, uint32_t id, double *value) {
    return value ? action(n, VST_GET, (lnd_vst3_args){.id = id, .data = value}) : LND_ERR_INVALID_ARG;
}
int32_t LND_NodeSetVst3ParameterValue(LND_NODE *n, uint32_t id, double value) { return action(n, VST_SET, (lnd_vst3_args){.id = id, .value = value}); }
int32_t LND_NodeFormatVst3ParameterValue(const LND_NODE *n, uint32_t id, double value, char *text, size_t capacity) {
    return text && capacity ? action(n, VST_FORMAT, (lnd_vst3_args){.id = id, .value = value, .data = text, .bytes = capacity}) : LND_ERR_INVALID_ARG;
}
int32_t LND_NodeParseVst3ParameterValue(const LND_NODE *n, uint32_t id, const char *text, double *value) {
    return text && value ? action(n, VST_PARSE, (lnd_vst3_args){.id = id, .input = text, .data = value}) : LND_ERR_INVALID_ARG;
}
int32_t LND_NodeSetVst3Bypass(LND_NODE *n, bool bypass) { return action(n, VST_BYPASS, (lnd_vst3_args){.id = bypass}); }
int32_t LND_NodeGetVst3ProgramList(const LND_NODE *n, uint32_t index, LND_VST3_PROGRAM_LIST *info) {
    return info ? action(n, VST_PROGRAM_LIST, (lnd_vst3_args){.index = index, .data = info}) : LND_ERR_INVALID_ARG;
}
int32_t LND_NodeGetVst3ProgramName(const LND_NODE *n, int32_t list, uint32_t index, char *name, size_t capacity) {
    return name && capacity ? action(n, VST_PROGRAM_NAME, (lnd_vst3_args){.id = (uint32_t)list, .index = index, .data = name, .bytes = capacity})
                            : LND_ERR_INVALID_ARG;
}
int32_t LND_NodeSetVst3Program(LND_NODE *n, int32_t list, uint32_t index) { return action(n, VST_PROGRAM, (lnd_vst3_args){.id = (uint32_t)list, .index = index}); }
int32_t LND_NodeSaveVst3State(LND_NODE *n, void *data, size_t capacity, size_t *bytes) {
    return bytes && (data || !capacity) ? action(n, VST_SAVE, (lnd_vst3_args){.data = data, .bytes = capacity, .size = bytes}) : LND_ERR_INVALID_ARG;
}
int32_t LND_NodeLoadVst3State(LND_NODE *n, const void *data, size_t bytes) {
    return data ? action(n, VST_LOAD, (lnd_vst3_args){.input = data, .bytes = bytes}) : LND_ERR_INVALID_ARG;
}
int32_t LND_NodeSetVst3Transport(LND_NODE *n, const LND_VST3_TRANSPORT *t) {
    return t ? action(n, VST_TRANSPORT, (lnd_vst3_args){.input = t}) : LND_ERR_INVALID_ARG;
}
int32_t LND_NodeResetVst3(LND_NODE *n) { return action(n, VST_RESET, (lnd_vst3_args){0}); }
int32_t LND_NodeDispatchVst3(LND_NODE *n) { return action(n, VST_DISPATCH, (lnd_vst3_args){0}); }
int32_t LND_NodeOpenVst3Editor(LND_NODE *n, void *parent, int32_t platform, LND_VST3_RESIZE_PROC resize, void *user) {
    return action(n, VST_EDITOR_OPEN, (lnd_vst3_args){.parent = parent, .platform = platform, .resize = resize, .user = user});
}
int32_t LND_NodeCloseVst3Editor(LND_NODE *n) { return action(n, VST_EDITOR_CLOSE, (lnd_vst3_args){0}); }
int32_t LND_NodeGetVst3EditorSize(const LND_NODE *n, uint32_t *width, uint32_t *height) {
    if (!width || !height) return LND_ERR_INVALID_ARG;
    uint32_t size[2];
    int32_t r = action(n, VST_EDITOR_SIZE, (lnd_vst3_args){.data = size});
    if (!r) {
        *width = size[0];
        *height = size[1];
    }
    return r;
}
int32_t LND_NodeResizeVst3Editor(LND_NODE *n, uint32_t width, uint32_t height) {
    return action(n, VST_EDITOR_RESIZE, (lnd_vst3_args){.id = width, .index = height});
}

int32_t lnd_vst3_utf8(const uint16_t *src, char *dst, size_t capacity) {
    size_t n = 0, written = 0;
    while (n < 128 && src[n])
        ++n;
    const uint16_t endian = 1;
    const uint8_t *begin = (const uint8_t *)src;
    lnd_text_reader reader = {.next = begin, .end = begin + n * 2, .encoding = 2, .big_endian = *(const uint8_t *)&endian == 0};
    uint32_t code;
    int32_t result;
    while ((result = lnd_text_next(&reader, &code)) > 0) {
        uint8_t encoded[4];
        size_t bytes = lnd_text_utf8_code(encoded, code);
        if (bytes >= capacity - written) return LND_ERR_INVALID_ARG;
        memcpy(dst + written, encoded, bytes);
        written += bytes;
    }
    if (result < 0 || written >= capacity) return LND_ERR_INVALID_ARG;
    dst[written] = 0;
    return LND_OK;
}
int32_t lnd_vst3_utf16(const char *src, uint16_t *dst, size_t capacity) {
    size_t n = 0, written = 0;
    while (n < 512 && src[n])
        ++n;
    if (n == 512) return LND_ERR_INVALID_ARG;
    const uint8_t *next = (const uint8_t *)src, *end = next + n;
    while (next < end) {
        uint32_t code;
        if (!lnd_text_utf8_next(&next, end, &code)) return LND_ERR_INVALID_ARG;
        size_t words = code >= 65536 ? 2 : 1;
        if (words >= capacity - written) return LND_ERR_INVALID_ARG;
        if (words == 2) {
            code -= 65536;
            dst[written++] = (uint16_t)(0xd800 | (code >> 10));
            code = 0xdc00 | (code & 1023);
        }
        dst[written++] = (uint16_t)code;
    }
    if (written >= capacity) return LND_ERR_INVALID_ARG;
    dst[written] = 0;
    return (int32_t)written;
}
