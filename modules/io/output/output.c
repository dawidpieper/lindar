#include "output.h"
#include "lindar.h"
#if LND_MODULE_METADATA
#include "metadata/internal.h"
#endif
#if LND_MODULE_SINK
#include "io/sink/sink.h"
#endif
#include "src/alloc.h"
#include "playback/graph/context.h"
#include "src/error.h"
#include "pcm/audio/convert.h"
#include "src/format.h"
#include "src/pcm.h"

#include <stdlib.h>
#include <string.h>

#define LND_MAX_ENCODERS 32
#define LND_OUTPUT_BLOCK 4096

static lnd_output *lnd_output_list;
extern const LND_ENCODER *const lnd_builtin_encoders[];
extern const uint32_t lnd_builtin_encoders_count;

static const LND_ENCODER *lnd_encoders[LND_MAX_ENCODERS];
static uint32_t lnd_encoders_count;
static bool lnd_encoders_ready;

static void lnd_encoders_init(void) {
    if (lnd_encoders_ready) return;
    lnd_encoders_ready = true;
    for (uint32_t i = 0; i < lnd_builtin_encoders_count && lnd_encoders_count < LND_MAX_ENCODERS; i++)
        lnd_encoders[lnd_encoders_count++] = lnd_builtin_encoders[i];
}

static bool lnd_list_has(const char *list, const char *item) {
    if (!list || !item || !*item) return false;
    size_t n = strlen(item);
    for (const char *p = list; *p;) {
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, item, n) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

static const LND_ENCODER *lnd_encoder_lookup(const char *name, const char *ext) {
    lnd_encoders_init();
    const LND_ENCODER *fallback = nullptr;
    for (uint32_t i = 0; i < lnd_encoders_count; i++) {
        const LND_ENCODER *e = lnd_encoders[i];
        if (name) {
            if (strcmp(e->name, name) == 0) return e;
            continue;
        }
        if (!lnd_list_has(e->extensions, ext)) continue;
        if (!(e->flags & LND_ENCODER_FLAG_FALLBACK)) return e;
        if (!fallback) fallback = e;
    }
    return fallback;
}

bool lnd_encoder_option(const char *options, const char *key, char *out, size_t cap) {
    if (!options || !key || !cap) return false;
    size_t klen = strlen(key);
    for (const char *p = options; *p;) {
        while (*p == ' ' || *p == ';' || *p == ',')
            p++;
        const char *end = p;
        while (*end && *end != ';' && *end != ',')
            end++;
        const char *eq = memchr(p, '=', (size_t)(end - p));
        size_t nlen = eq ? (size_t)(eq - p) : (size_t)(end - p);
        if (nlen == klen && strncmp(p, key, klen) == 0) {
            const char *v = eq ? eq + 1 : end;
            size_t vlen = (size_t)(end - v);
            if (vlen >= cap) vlen = cap - 1;
            memcpy(out, v, vlen);
            out[vlen] = 0;
            return true;
        }
        p = end;
    }
    return false;
}

int64_t lnd_encoder_option_int(const char *options, const char *key, int64_t fallback) {
    char buf[32];
    if (!lnd_encoder_option(options, key, buf, sizeof buf) || !buf[0]) return fallback;
    return strtoll(buf, nullptr, 10);
}

uint32_t lnd_encoder_level(const LND_ENCODER_PARAMS *p, uint32_t fallback, uint32_t max) {
    if (!p->quality) return fallback;
    uint32_t q = p->quality > 100 ? 100 : p->quality;
    return (uint32_t)(((q - 1) * max + 49) / 99);
}

static void lnd_output_link(lnd_output *o) {
    o->prev = nullptr;
    o->next = lnd_output_list;
    if (o->next) o->next->prev = o;
    lnd_output_list = o;
}

static void lnd_output_unlink(lnd_output *o) {
    if (o->prev)
        o->prev->next = o->next;
    else
        lnd_output_list = o->next;
    if (o->next) o->next->prev = o->prev;
    o->prev = o->next = nullptr;
}

static bool lnd_output_valid(const lnd_output *o) {
    for (lnd_output *p = lnd_output_list; p; p = p->next) {
        if (p == o) return true;
    }
    return false;
}

static int32_t lnd_output_finish_locked(lnd_output *o) {
    if (o->finished) return o->finish_result;
    int32_t result = o->failed;
    if (o->state) {
        lnd_callback_enter();
        int32_t r = o->encoder->close(o->state);
        lnd_callback_leave();
        if (!result) result = r;
    }
    o->state = nullptr;
    if (!result) result = LND_IoFlush(o->io);
    if (!result && o->io->vt->commit) result = o->io->vt->commit(o->io->state);
    o->bytes = o->io->size;
    int32_t closed = lnd_io_close(o->io);
    o->io = nullptr;
    o->finished = true;
    o->finish_result = result ? result : closed;
    return o->finish_result;
}

static int32_t lnd_output_destroy(lnd_output *o) {
#if LND_MODULE_SINK
    lnd_sinks_release_output(o);
#endif
    lnd_mutex_lock(&o->lock);
    int32_t result = lnd_output_finish_locked(o);
    lnd_mutex_unlock(&o->lock);
    lnd_mutex_free(&o->lock);
    lnd_free_aligned(o->scratch);
#if LND_MODULE_METADATA
    LND_MetadataFree(o->metadata);
#endif
    lnd_free(o);
    return result;
}

void lnd_outputs_free_all(void) {
    while (lnd_output_list) {
        lnd_output *o = lnd_output_list;
        lnd_output_unlink(o);
        lnd_output_destroy(o);
    }
}

#if LND_MODULE_METADATA
int32_t LND_OutputCopyMetadata(const LND_OUTPUT *output, LND_METADATA *metadata) {
    if (!output || !metadata) return LND_ERR_INVALID_ARG;
    if (lnd_callback_active() || !lnd_context_enter()) return LND_ERR_BUSY;
    int32_t r = !lnd_output_valid(output) ? LND_ERR_INVALID_ARG : output->metadata ? LND_OK : LND_METADATA_ERR_NOT_FOUND;
    LND_METADATA *snapshot = !r ? lnd_tag_ref(output->metadata) : nullptr;
    lnd_context_unlock();
    if (!r) r = lnd_tag_assign(metadata, snapshot);
    LND_MetadataFree(snapshot);
    return r;
}
#endif

bool lnd_output_params_valid(const LND_ENCODER_PARAMS *p) {
    if (!p || p->channels < 1 || p->channels > LND_MAX_CHANNELS || p->sample_rate_hz < 1) return false;
    if (p->format && !lnd_format_valid(p->format)) return false;
    if (p->metadata_flags & ~1u) return false;
    if (p->mode < LND_ENCODER_MODE_DEFAULT || p->mode > LND_ENCODER_MODE_ABR || p->quality > 100) return false;
    return (p->flags & ~(uint32_t)LND_OUTPUT_STREAMING) == 0;
}

static const LND_ENCODER *lnd_output_encoder(const LND_ENCODER_PARAMS *params, const char *ext) {
    const LND_ENCODER *encoder = lnd_encoder_lookup(params->encoder_name, ext);
    if (encoder && params->frame_size_frames && !(encoder->flags & LND_ENCODER_FLAG_FRAME_SIZE)) return nullptr;
    if (encoder && params->metadata && !(encoder->flags & LND_ENCODER_FLAG_METADATA)) return nullptr;
#if !LND_MODULE_METADATA
    if (params->metadata) return nullptr;
#endif
    return encoder;
}

int32_t lnd_output_validate(const LND_ENCODER_PARAMS *params, const char *ext) {
    if (lnd_callback_active()) return LND_ERR_BUSY;
    if (!lnd_output_params_valid(params)) return LND_ERR_INVALID_ARG;
    if (!lnd_context_enter()) return LND_ERR_BUSY;
    int32_t result = !lnd_ctx.initialized ? LND_ERR_STATE : lnd_output_encoder(params, ext) ? LND_OK : LND_ERR_UNSUPPORTED;
    lnd_context_unlock();
    return result;
}

lnd_output *lnd_output_create_io(lnd_io *io, const LND_ENCODER_PARAMS *params, const char *ext) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!io) return lnd_error_null(LND_ERR_IO);
    if (!lnd_context_enter()) {
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_BUSY);
    }
#if LND_MODULE_GRAPH
    lnd_context_gc();
#endif
    if (!lnd_ctx.initialized) {
        lnd_context_unlock();
        lnd_io_close(io);
        return lnd_error_null(LND_ERR_STATE);
    }
    const LND_ENCODER *e = lnd_output_encoder(params, ext);
    lnd_output *o = e ? lnd_alloc_zero(sizeof *o) : nullptr;
    int32_t r = e ? (o ? LND_OK : LND_ERR_OUT_OF_MEMORY) : LND_ERR_UNSUPPORTED;
    if (r == LND_OK) {
        o->encoder = e;
        o->io = io;
        o->channels = params->channels;
        o->sample_rate_hz = params->sample_rate_hz;
        o->scratch_frames = LND_OUTPUT_BLOCK;
        o->scratch = lnd_alloc_aligned((size_t)LND_OUTPUT_BLOCK * params->channels * sizeof(float), LND_CACHE_LINE);
        if (!o->scratch) r = LND_ERR_OUT_OF_MEMORY;
    }
    LND_ENCODER_PARAMS copy = *params;
#if LND_MODULE_METADATA
    if (!r && params->metadata) {
        o->metadata = LND_MetadataClone(params->metadata);
        if (!o->metadata)
            r = LND_ERR_OUT_OF_MEMORY;
        else
            copy.metadata = o->metadata;
    }
#endif
    if (r == LND_OK) {
        lnd_callback_enter();
        r = e->open(io, &copy, ext && *ext ? ext : nullptr, &o->state);
        lnd_callback_leave();
    }
    if (r != LND_OK) {
        lnd_context_unlock();
        if (o) lnd_free_aligned(o->scratch);
#if LND_MODULE_METADATA
        if (o) LND_MetadataFree(o->metadata);
#endif
        lnd_free(o);
        lnd_io_close(io);
        return lnd_error_null(r);
    }
    lnd_mutex_init(&o->lock);
    lnd_output_link(o);
    lnd_context_unlock();
    return o;
}

LND_OUTPUT *LND_OutputCreateProc(const LND_IO_OUTPUT_PROCS *procs, void *user, const LND_ENCODER_PARAMS *params) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!procs || !procs->write || !lnd_output_params_valid(params) || !params->encoder_name) return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_io *io = lnd_io_create_procs(procs, user);
    if (!io) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    io->borrowed = true;
    LND_OUTPUT *output = lnd_output_create_io(lnd_io_ref(io), params, nullptr);
    io->borrowed = output == nullptr;
    lnd_io_close(io);
    return output;
}

int32_t LND_OutputWritePcm(LND_OUTPUT *o, const LND_PCM *pcm, size_t offset, size_t frames) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!o || !lnd_pcm_range(pcm, offset, frames) || pcm->channels != o->channels) return lnd_error(LND_ERR_INVALID_ARG);
    lnd_mutex_lock(&o->lock);
    int32_t r = o->state ? LND_OK : LND_ERR_STATE;
    if (r == LND_OK && o->failed) r = o->failed;
    bool direct = frames && pcm->format == LND_FORMAT_F32 && pcm->layout == LND_LAYOUT_INTERLEAVED && lnd_pcm_stride(pcm) == o->channels * sizeof(float) &&
                  (uintptr_t)lnd_pcm_at(pcm, 0, offset) % alignof(float) == 0;
    LND_PCM scratch = {.data = o->scratch, .frames = o->scratch_frames, .channels = o->channels, .format = LND_FORMAT_F32};
    for (size_t done = 0; r == LND_OK && done < frames;) {
        uint32_t nb = (uint32_t)LND_MIN(frames - done, o->scratch_frames);
        const float *data = direct ? (const float *)lnd_pcm_at(pcm, 0, offset + done) : o->scratch;
        if (!direct) r = LND_PcmConvert(&scratch, 0, pcm, offset + done, nb);
        if (r == LND_OK) {
            lnd_callback_enter();
            r = o->encoder->write(o->state, data, nb);
            lnd_callback_leave();
            if (r > 0) r = LND_ERR_IO;
        }
        if (r == LND_OK) o->frames += nb;
        done += nb;
    }
    if (r != LND_OK && r != LND_ERR_STATE) o->failed = r;
    lnd_mutex_unlock(&o->lock);
    return lnd_error(r);
}

int32_t LND_OutputWrite(LND_OUTPUT *o, const void *data, int32_t format, uint64_t frames) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!o || frames > SIZE_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    LND_PCM pcm = {.data = (void *)data, .frames = (size_t)frames, .channels = o->channels, .format = format};
    return LND_OutputWritePcm(o, &pcm, 0, (size_t)frames);
}

int64_t LND_OutputWriteSource(LND_OUTPUT *o, LND_SOURCE *s, uint64_t frames) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!o || !s || frames > INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    uint32_t channels = LND_SourceGetChannels(s);
    if (channels != o->channels || LND_SourceGetSampleRateHz(s) != o->sample_rate_hz) return lnd_error(LND_ERR_FORMAT);
    lnd_mutex_lock(&o->lock);
    int32_t error = o->finished ? LND_ERR_STATE : o->failed;
    lnd_mutex_unlock(&o->lock);
    if (error) return lnd_error(error);
    if (!frames) {
        LND_SOURCE_INFO info;
        int32_t result = LND_SourceGetInfo(s, &info);
        if (result != LND_OK) return result;
        if (info.length_kind == LND_LENGTH_UNKNOWN) return lnd_error(LND_ERR_UNSUPPORTED);
        uint64_t length = info.length_frames, position = info.position_frames;
        frames = info.length_kind == LND_LENGTH_ESTIMATED ? INT64_MAX : length > position ? length - position : 0;
        if (frames > INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    }
    if (!frames) return 0;
    float *data = lnd_alloc((size_t)LND_OUTPUT_BLOCK * channels * sizeof *data);
    if (!data) return lnd_error(LND_ERR_OUT_OF_MEMORY);
    LND_PCM pcm = {.data = data, .frames = LND_OUTPUT_BLOCK, .channels = channels, .format = LND_FORMAT_F32};
    uint64_t total = 0;
    while (total < frames) {
        uint32_t count = (uint32_t)LND_MIN(frames - total, (uint64_t)LND_OUTPUT_BLOCK);
        int64_t got = LND_SourceReadPcm(s, &pcm, 0, count);
        if (got < 0) {
            error = (int32_t)got;
            break;
        }
        if (got && (error = LND_OutputWrite(o, data, LND_FORMAT_F32, (uint64_t)got)) != LND_OK) break;
        total += (uint64_t)got;
        if ((uint64_t)got < count) break;
    }
    lnd_free(data);
    if (error) {
        lnd_mutex_lock(&o->lock);
        if (!o->failed) o->failed = error;
        lnd_mutex_unlock(&o->lock);
    }
    return error ? lnd_error(error) : (int64_t)total;
}

int32_t LND_OutputFlush(LND_OUTPUT *o) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!o) return lnd_error(LND_ERR_INVALID_ARG);
    lnd_mutex_lock(&o->lock);
    int32_t r = o->state ? LND_OK : LND_ERR_STATE;
    if (r == LND_OK && o->failed) r = o->failed;
    if (r == LND_OK && o->encoder->flush) {
        lnd_callback_enter();
        r = o->encoder->flush(o->state);
        lnd_callback_leave();
    }
    if (r == LND_OK) r = LND_IoFlush(o->io);
    if (r != LND_OK && r != LND_ERR_STATE) o->failed = r;
    lnd_mutex_unlock(&o->lock);
    return lnd_error(r);
}

int32_t LND_OutputFinish(LND_OUTPUT *o) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!o) return lnd_error(LND_ERR_INVALID_ARG);
    lnd_mutex_lock(&o->lock);
    int32_t result = lnd_output_finish_locked(o);
    lnd_mutex_unlock(&o->lock);
    return lnd_error(result);
}

int32_t LND_OutputGetInfo(const LND_OUTPUT *output, LND_OUTPUT_INFO *info) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!output || !info) return lnd_error(LND_ERR_INVALID_ARG);
    lnd_output *o = (lnd_output *)output;
    lnd_mutex_lock(&o->lock);
    *info = (LND_OUTPUT_INFO){
        .frames = o->frames, .bytes = o->io ? o->io->size : o->bytes, .finished = o->finished, .result = o->finished ? o->finish_result : o->failed};
    lnd_mutex_unlock(&o->lock);
    return LND_OK;
}

int32_t LND_OutputFree(LND_OUTPUT *o) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!o) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    if (!lnd_output_valid(o)) {
        lnd_context_unlock();
        return lnd_error(LND_ERR_INVALID_ARG);
    }
    lnd_output_unlink(o);
    int32_t r = lnd_output_destroy(o);
    lnd_context_unlock();
    return lnd_error(r);
}

const LND_ENCODER *LND_OutputGetEncoder(const LND_OUTPUT *o) { return o ? o->encoder : nullptr; }

uint32_t LND_OutputGetChannels(const LND_OUTPUT *o) { return o ? o->channels : 0; }

uint32_t LND_OutputGetSampleRateHz(const LND_OUTPUT *o) { return o ? o->sample_rate_hz : 0; }

uint64_t LND_OutputGetFrames(const LND_OUTPUT *o) {
    LND_OUTPUT_INFO info;
    return LND_OutputGetInfo(o, &info) == LND_OK ? info.frames : 0;
}

uint64_t LND_OutputGetBytes(const LND_OUTPUT *o) {
    LND_OUTPUT_INFO info;
    return LND_OutputGetInfo(o, &info) == LND_OK ? info.bytes : 0;
}

int32_t LND_EncoderRegister(const LND_ENCODER *encoder) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);
    if (!encoder || !encoder->name || !*encoder->name || !encoder->open || !encoder->write || !encoder->close) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_encoders_init();
    int32_t r = LND_OK;
    for (uint32_t i = 0; i < lnd_encoders_count; i++) {
        if (lnd_encoders[i] == encoder) {
            lnd_context_unlock();
            return LND_OK;
        }
        if (!strcmp(lnd_encoders[i]->name, encoder->name)) {
            lnd_context_unlock();
            return lnd_error(LND_ERR_INVALID_ARG);
        }
    }
    if (lnd_encoders_count >= LND_MAX_ENCODERS)
        r = LND_ERR_BUSY;
    else
        lnd_encoders[lnd_encoders_count++] = encoder;
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t LND_EncoderUnregister(const LND_ENCODER *encoder) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!encoder) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_encoders_init();
    int32_t r = LND_ERR_INVALID_ARG;
    for (uint32_t i = 0; i < lnd_encoders_count; i++) {
        if (lnd_encoders[i] == encoder) {
            memmove(&lnd_encoders[i], &lnd_encoders[i + 1], sizeof(lnd_encoders[0]) * (lnd_encoders_count - i - 1));
            lnd_encoders_count--;
            r = LND_OK;
            break;
        }
    }
    lnd_context_unlock();
    return lnd_error(r);
}

uint32_t LND_EncoderGetCount(void) {
    if (lnd_callback_active()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }

    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }
    lnd_encoders_init();
    uint32_t n = lnd_encoders_count;
    lnd_context_unlock();
    return n;
}

const LND_ENCODER *LND_EncoderGet(uint32_t index) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_encoders_init();
    const LND_ENCODER *e = index < lnd_encoders_count ? lnd_encoders[index] : nullptr;
    lnd_context_unlock();
    return e;
}

const LND_ENCODER *LND_EncoderFind(const char *name) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!name) return nullptr;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    const LND_ENCODER *e = lnd_encoder_lookup(name, nullptr);
    lnd_context_unlock();
    return e;
}

const char *LND_EncoderGetName(const LND_ENCODER *encoder) { return encoder ? encoder->name : nullptr; }

const char *LND_EncoderGetExtensions(const LND_ENCODER *encoder) { return encoder ? encoder->extensions : nullptr; }

uint32_t LND_EncoderGetFlags(const LND_ENCODER *encoder) { return encoder ? encoder->flags : 0; }
