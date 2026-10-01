#include "io.h"
#include "src/alloc.h"
#include "src/callback.h"
#include "src/error.h"

#include <string.h>

LND_IO *LND_IoCreateInput(const LND_IO_INPUT_PROCS *procs, void *user, uint64_t size) {
    if (!procs || !procs->read_at || size == LND_IO_SIZE_UNKNOWN) return lnd_error_null(LND_ERR_INVALID_ARG);
    LND_IO *io = lnd_io_open_input(procs, user, size);
    return io ? io : lnd_error_null(LND_ERR_OUT_OF_MEMORY);
}
LND_IO *LND_IoOpenMemory(const void *data, size_t size) {
    if (!data && size) return lnd_error_null(LND_ERR_INVALID_ARG);
    LND_IO *io = lnd_io_open_memory(data, size);
    return io ? io : lnd_error_null(LND_ERR_OUT_OF_MEMORY);
}
LND_IO *LND_IoCreateOutput(const LND_IO_OUTPUT_PROCS *procs, void *user) {
    if (!procs || !procs->write) return lnd_error_null(LND_ERR_INVALID_ARG);
    LND_IO *io = lnd_io_create_procs(procs, user);
    return io ? io : lnd_error_null(LND_ERR_OUT_OF_MEMORY);
}

typedef struct lnd_io_input {
    LND_IO_INPUT_PROCS procs;
    void *user;
} lnd_io_input;

static int64_t lnd_io_input_read_at(void *state, uint64_t pos, void *dst, size_t size) {
    lnd_io_input *s = state;
    lnd_callback_enter();
    size_t got = s->procs.read_at(s->user, pos, dst, size);
    lnd_callback_leave();
    return got <= size ? (int64_t)got : LND_ERR_IO;
}

static int32_t lnd_io_input_close(void *state, bool borrowed) {
    lnd_io_input *s = state;
    if (!borrowed && s->procs.close) {
        lnd_callback_enter();
        s->procs.close(s->user);
        lnd_callback_leave();
    }
    return LND_OK;
}

static const lnd_io_vt lnd_io_input_vt = {.read_at = lnd_io_input_read_at, .close = lnd_io_input_close};

lnd_io *lnd_io_open_input(const LND_IO_INPUT_PROCS *procs, void *user, uint64_t size) {
    lnd_io *io = lnd_alloc_zero(sizeof *io + sizeof(lnd_io_input));
    if (!io) return nullptr;
    lnd_io_input *s = (lnd_io_input *)(io + 1);
    s->procs = *procs;
    s->user = user;
    *io = (lnd_io){.references = 1, .vt = &lnd_io_input_vt, .state = s, .size = size, .seekable = true};
    return io;
}

typedef struct lnd_io_memory {
    const uint8_t *data;
    size_t size;
} lnd_io_memory;

typedef struct lnd_io_procs {
    LND_IO_OUTPUT_PROCS procs;
    void *user;
    uint64_t cur;
} lnd_io_procs;

static int64_t lnd_io_memory_read_at(void *state, uint64_t pos, void *dst, size_t size) {
    lnd_io_memory *m = state;
    if (pos >= m->size) return 0;
    size_t n = LND_MIN(size, (size_t)(m->size - pos));
    memcpy(dst, m->data + pos, n);
    return n;
}

static int32_t lnd_io_memory_close(void *state, bool borrowed) { return LND_OK; }

static const lnd_io_vt lnd_io_memory_vt = {
    .read_at = lnd_io_memory_read_at,
    .close = lnd_io_memory_close,
};

static size_t lnd_io_procs_write_at(void *state, uint64_t pos, const void *src, size_t size) {
    lnd_io_procs *p = state;
    if (p->cur != pos) {
        if (!p->procs.seek) return 0;
        lnd_callback_enter();
        int32_t result = p->procs.seek(p->user, pos);
        lnd_callback_leave();
        if (result != LND_OK) return 0;
        p->cur = pos;
    }
    lnd_callback_enter();
    size_t w = p->procs.write(p->user, src, size);
    lnd_callback_leave();
    if (w > size || w > UINT64_MAX - p->cur) {
        lnd_error(LND_ERR_IO);
        return 0;
    }
    p->cur += w;
    return w;
}

static int32_t lnd_io_procs_flush(void *state) {
    lnd_io_procs *p = state;
    lnd_callback_enter();
    int32_t result = p->procs.flush ? p->procs.flush(p->user) : LND_OK;
    lnd_callback_leave();
    return result;
}

static int32_t lnd_io_procs_close(void *state, bool borrowed) {
    lnd_io_procs *p = state;
    lnd_callback_enter();
    int32_t result = !borrowed && p->procs.close ? p->procs.close(p->user) : LND_OK;
    lnd_callback_leave();
    return result;
}

static const lnd_io_vt lnd_io_procs_vt = {
    .write_at = lnd_io_procs_write_at,
    .close = lnd_io_procs_close,
    .flush = lnd_io_procs_flush,
};

lnd_io *lnd_io_open_memory(const void *data, size_t size) {
    if (!data && size) return nullptr;
    lnd_io *io = lnd_alloc_zero(sizeof *io + sizeof(lnd_io_memory));
    if (!io) return nullptr;
    lnd_io_memory *m = (lnd_io_memory *)(io + 1);
    m->data = data;
    m->size = size;
    io->references = 1;
    io->vt = &lnd_io_memory_vt;
    io->state = m;
    io->size = size;
    io->seekable = true;
    return io;
}

lnd_io *lnd_io_create_procs(const LND_IO_OUTPUT_PROCS *procs, void *user) {
    if (!procs || !procs->write) return nullptr;
    lnd_io *io = lnd_alloc_zero(sizeof *io + sizeof(lnd_io_procs));
    if (!io) return nullptr;
    lnd_io_procs *p = (lnd_io_procs *)(io + 1);
    p->procs = *procs;
    p->user = user;
    io->references = 1;
    io->vt = &lnd_io_procs_vt;
    io->state = p;
    io->writable = true;
    io->seekable = procs->seek != nullptr;
    return io;
}

int32_t lnd_io_window(lnd_io *io, uint64_t offset, uint64_t length) {
    if (offset > io->size) return LND_ERR_INVALID_ARG;
    uint64_t avail = io->size - offset;
    io->base += offset;
    io->size = length && length < avail ? length : avail;
    io->pos = 0;
    return LND_OK;
}

lnd_io *lnd_io_ref(lnd_io *io) {
    if (io) ++io->references;
    return io;
}

int32_t lnd_io_close(lnd_io *io) {
    if (!io || --io->references) return LND_OK;
    int32_t result = io->vt->close(io->state, io->borrowed);
    lnd_free(io);
    return result;
}

int64_t LND_IoRead(LND_IO *io, void *dst, size_t size) {
    if (!io || (!dst && size) || size > (uint64_t)INT64_MAX) return lnd_error(LND_ERR_INVALID_ARG);
    size_t done = 0;
    while (done < size) {
        int64_t got = LND_IoReadSome(io, (uint8_t *)dst + done, size - done);
        if (got <= 0) return done ? (int64_t)done : got;
        done += (size_t)got;
        if (io->vt->read) break;
    }
    return (int64_t)done;
}

size_t LND_IoWrite(LND_IO *io, const void *data, size_t size) {
    if (!io || (!data && size) || !io->writable || !io->vt->write_at) {
        lnd_error(LND_ERR_INVALID_ARG);
        return 0;
    }
    if (!size) return 0;
    if (io->status < 0) {
        lnd_error(io->status);
        return 0;
    }
    if (size > UINT64_MAX - io->pos || io->base > UINT64_MAX - io->pos - size) {
        io->status = lnd_error(LND_ERR_INVALID_ARG);
        return 0;
    }
    size_t written = io->vt->write_at(io->state, io->base + io->pos, data, size);
    if (written > size) written = 0;
    io->pos += written;
    if (io->pos > io->size) io->size = io->pos;
    io->status = written ? LND_SOURCE_READY : lnd_error(LND_ERR_IO);
    return written;
}

int32_t LND_IoSeekBytes(LND_IO *io, uint64_t position) {
    if (!io || position > io->size) return lnd_error(LND_ERR_INVALID_ARG);
    if (!io->seekable && position != io->pos) return lnd_error(LND_ERR_UNSUPPORTED);
    if (io->vt->seek && position != io->pos) {
        int32_t result = io->vt->seek(io->state, position);
        if (result != LND_OK) return result;
    }
    io->status = LND_SOURCE_READY;
    io->pos = position;
    return LND_OK;
}

uint64_t LND_IoGetPositionBytes(const LND_IO *io) { return io ? io->pos : 0; }

uint64_t LND_IoGetSizeBytes(const LND_IO *io) { return io ? io->size : 0; }

bool LND_IoCanSeek(const LND_IO *io) { return io && io->seekable; }

typedef struct lnd_io_stream {
    LND_IO_STREAM_INPUT_PROCS procs;
    void *user;
} lnd_io_stream;

static int64_t lnd_io_stream_read(void *state, void *dst, size_t size) {
    lnd_io_stream *s = state;
    lnd_callback_enter();
    int64_t got = s->procs.read(s->user, dst, size);
    lnd_callback_leave();
    return got;
}

static int32_t lnd_io_stream_seek(void *state, uint64_t position) {
    lnd_io_stream *s = state;
    if (!s->procs.seek) return LND_ERR_UNSUPPORTED;
    lnd_callback_enter();
    int32_t result = s->procs.seek(s->user, position);
    lnd_callback_leave();
    return result;
}

static int32_t lnd_io_stream_close(void *state, bool borrowed) {
    lnd_io_stream *s = state;
    if (!borrowed && s->procs.close) {
        lnd_callback_enter();
        s->procs.close(s->user);
        lnd_callback_leave();
    }
    return LND_OK;
}

static const lnd_io_vt lnd_io_stream_vt = {.read = lnd_io_stream_read, .seek = lnd_io_stream_seek, .close = lnd_io_stream_close};

LND_IO *LND_IoCreateStream(const LND_IO_STREAM_INPUT_PROCS *procs, void *user, uint64_t size) {
    if (!procs || !procs->read) return lnd_error_null(LND_ERR_INVALID_ARG);
    lnd_io *io = lnd_alloc_zero(sizeof *io + sizeof(lnd_io_stream));
    if (!io) return lnd_error_null(LND_ERR_OUT_OF_MEMORY);
    lnd_io_stream *s = (lnd_io_stream *)(io + 1);
    *s = (lnd_io_stream){.procs = *procs, .user = user};
    *io = (lnd_io){.references = 1, .vt = &lnd_io_stream_vt, .state = s, .size = size, .seekable = procs->seek != nullptr};
    return io;
}

int64_t LND_IoReadSome(LND_IO *io, void *dst, size_t size) {
    if (!io || (!dst && size) || (uint64_t)size > INT64_MAX || (!io->vt->read && !io->vt->read_at))
        return lnd_error(LND_ERR_INVALID_ARG);
    if (!size) return 0;
    if (io->status < 0) return lnd_error(io->status);
    if (io->status == LND_SOURCE_EOF || io->pos == io->size) {
        io->status = LND_SOURCE_EOF;
        return 0;
    }
    size = (size_t)LND_MIN((uint64_t)size, io->size - io->pos);
    if (io->base > UINT64_MAX - io->pos) return io->status = lnd_error(LND_ERR_IO);
    int64_t got = io->vt->read ? io->vt->read(io->state, dst, size)
                              : io->vt->read_at(io->state, io->base + io->pos, dst, size);
    if (got == LND_READ_EOF || (!got && !io->vt->read)) {
        io->status = LND_SOURCE_EOF;
        return 0;
    }
    if (got < 0 || (uint64_t)got > size)
        return io->status = lnd_error(got < 0 && got >= INT32_MIN ? (int32_t)got : LND_ERR_IO);
    io->pos += (uint64_t)got;
    io->status = io->pos == io->size ? LND_SOURCE_EOF : got ? LND_SOURCE_READY : LND_SOURCE_WAITING;
    return got;
}

int32_t LND_IoGetStatus(const LND_IO *io) { return io ? io->status : LND_ERR_INVALID_ARG; }
int32_t LND_IoFlush(LND_IO *io) {
    if (!io) return lnd_error(LND_ERR_INVALID_ARG);
    int32_t result = io->status < 0 ? io->status : io->vt->flush ? io->vt->flush(io->state) : LND_OK;
    if (result < 0) io->status = result;
    return lnd_error(result);
}
int32_t LND_IoFree(LND_IO *io) { return lnd_error(lnd_io_close(io)); }
