#include "buffer.h"
#include "src/alloc.h"
#include "src/config.h"
#include "src/context.h"
#include "src/error.h"

#define LND_POOL_MIN_CLASS 12

static lnd_buffer *lnd_buffer_list;
static lnd_pool lnd_buffer_pool;

static uint32_t lnd_pool_class(size_t bytes) {
    size_t cap = lnd_next_pow2_size(bytes < ((size_t)1 << LND_POOL_MIN_CLASS) ? ((size_t)1 << LND_POOL_MIN_CLASS) : bytes);
    return cap ? lnd_log2_size(cap) : UINT32_MAX;
}

static void *lnd_pool_acquire(lnd_pool *p, uint32_t cls) {
    void *block = p->free_list[cls];
    if (block) {
        p->free_list[cls] = *(void **)block;
        p->bytes -= (size_t)1 << cls;
        return block;
    }
    return lnd_alloc_aligned((size_t)1 << cls, lnd_cfg_u32(LND_CFG_BUFFERS_ALIGN_BYTES));
}

static void lnd_pool_release(lnd_pool *p, void *block, uint32_t cls) {
    size_t size = (size_t)1 << cls;
    uint64_t limit = lnd_cfg_u64(LND_CFG_BUFFERS_POOL_MAX_BYTES);
    if (p->bytes <= limit && size <= limit - p->bytes && size <= SIZE_MAX - p->bytes) {
        *(void **)block = p->free_list[cls];
        p->free_list[cls] = block;
        p->bytes += size;
    } else {
        lnd_free_aligned(block);
    }
}

void lnd_pool_free_all(lnd_pool *p) {
    for (uint32_t i = 0; i < LND_COUNTOF(p->free_list); i++) {
        void *block = p->free_list[i];
        while (block) {
            void *next = *(void **)block;
            lnd_free_aligned(block);
            block = next;
        }
        p->free_list[i] = nullptr;
    }
    p->bytes = 0;
}

static void lnd_buffer_unlink(lnd_buffer *b) {
    if (b->prev) b->prev->next = b->next;
    else lnd_buffer_list = b->next;
    if (b->next) b->next->prev = b->prev;
    b->prev = b->next = nullptr;
}

static void lnd_buffer_destroy(lnd_buffer *b) {
    lnd_buffer_unlink(b);
    if (b->data) lnd_pool_release(&lnd_buffer_pool, b->data, b->size_class);
    lnd_free(b);
}

void lnd_buffer_ref(lnd_buffer *b) { lnd_add(&b->refcount, 1); }

void lnd_buffer_unref(lnd_buffer *b) {
    if (lnd_sub(&b->refcount, 1) == 1) lnd_buffer_destroy(b);
}

void lnd_buffers_free_all(void) {
    while (lnd_buffer_list) lnd_buffer_destroy(lnd_buffer_list);
}

static bool lnd_buffer_args_valid(int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint64_t frames) {
    return lnd_format_valid(format) && channels >= 1 && channels <= LND_MAX_CHANNELS && sample_rate_hz >= 1 && frames >= 1 &&
           frames <= SIZE_MAX / (lnd_format_bytes(format) * channels);
}

static int32_t lnd_buffer_assign(lnd_buffer *b, int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint64_t frames) {
    size_t bytes = (size_t)frames * lnd_format_bytes(format) * channels;
    uint32_t cls = lnd_pool_class(bytes);
    if (cls >= LND_COUNTOF(lnd_buffer_pool.free_list)) return LND_ERR_INVALID_ARG;
    if (!b->data || bytes > b->capacity) {
        void *data = lnd_pool_acquire(&lnd_buffer_pool, cls);
        if (!data) return LND_ERR_OUT_OF_MEMORY;
        if (b->data) lnd_pool_release(&lnd_buffer_pool, b->data, b->size_class);
        b->data = data;
        b->capacity = (size_t)1 << cls;
        b->size_class = cls;
    }
    b->format = format;
    b->channels = channels;
    b->sample_rate_hz = sample_rate_hz;
    b->frames = frames;
    return LND_OK;
}

lnd_buffer *lnd_buffer_new(int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint64_t frames) {
    if (!lnd_buffer_args_valid(format, channels, sample_rate_hz, frames)) return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_buffer *b = lnd_alloc_zero(sizeof *b);
    if (!b) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    int32_t r = lnd_buffer_assign(b, format, channels, sample_rate_hz, frames);
    if (r != LND_OK) {
        lnd_free(b);
        return lnd_error_null(r);
    }
    lnd_store_relaxed(&b->refcount, 1);
    b->next = lnd_buffer_list;
    if (b->next) b->next->prev = b;
    lnd_buffer_list = b;
    return b;
}

LND_BUFFER *LND_BufferCreate(int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint64_t frames) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_buffer_args_valid(format, channels, sample_rate_hz, frames)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_buffer *b = lnd_buffer_new(format, channels, sample_rate_hz, frames);
    lnd_context_unlock();
    return b;
}

LND_BUFFER *LND_BufferReuse(LND_BUFFER *b, int32_t format, uint32_t channels, uint32_t sample_rate_hz, uint64_t frames) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!b) return LND_BufferCreate(format, channels, sample_rate_hz, frames);
    if (!lnd_buffer_args_valid(format, channels, sample_rate_hz, frames)) return lnd_error_null(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    if (lnd_load(&b->refcount) != 1) {
        lnd_buffer *fresh = lnd_buffer_new(format, channels, sample_rate_hz, frames);
        if (fresh) lnd_buffer_unref(b);
        lnd_context_unlock();
        return fresh;
    }
    int32_t r = lnd_buffer_assign(b, format, channels, sample_rate_hz, frames);
    if (r != LND_OK) {
        lnd_context_unlock();
        return lnd_error_null(r);
    }
    lnd_context_unlock();
    return b;
}

void LND_BufferFree(LND_BUFFER *b) {
    if (lnd_callback_active()) {
        lnd_error(LND_ERR_BUSY);
        return;
    }

    if (!b) return;
    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return;
    }
    lnd_buffer_unref(b);
    lnd_context_unlock();
}

void *LND_BufferGetData(const LND_BUFFER *b) { return b ? b->data : nullptr; }

uint64_t LND_BufferGetFrames(const LND_BUFFER *b) { return b ? b->frames : 0; }

int32_t LND_BufferGetFormat(const LND_BUFFER *b) { return b ? b->format : LND_FORMAT_NONE; }

uint32_t LND_BufferGetChannels(const LND_BUFFER *b) { return b ? b->channels : 0; }

uint32_t LND_BufferGetSampleRateHz(const LND_BUFFER *b) { return b ? b->sample_rate_hz : 0; }

size_t LND_BufferGetBytes(const LND_BUFFER *b) { return b ? (size_t)b->frames * lnd_buffer_frame_bytes(b) : 0; }

void lnd_buffers_cleanup(void) {
    lnd_buffers_free_all();
    lnd_pool_free_all(&lnd_buffer_pool);
}

int32_t lnd_buffers_config_set(LND_CONFIG_KEY *key, uint64_t value) {
    return key == LND_CFG_BUFFERS_ALIGN_BYTES && (value & (value - 1)) ? LND_ERR_INVALID_ARG : LND_OK;
}
