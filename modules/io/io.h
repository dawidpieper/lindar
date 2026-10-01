#pragma once

#include "src/platform.h"
#include "lindar_io.h"

typedef struct lnd_io_vt {
    int64_t (*read_at)(void *state, uint64_t pos, void *dst, size_t size);
    size_t (*write_at)(void *state, uint64_t pos, const void *src, size_t size);
    int32_t (*close)(void *state, bool borrowed);
    int32_t (*flush)(void *state);
    int32_t (*commit)(void *state);
    int64_t (*read)(void *state, void *dst, size_t size);
    int32_t (*seek)(void *state, uint64_t position);
} lnd_io_vt;

struct LND_IO {
    const lnd_io_vt *vt;
    void *state;
    uint64_t base;
    uint64_t size;
    uint64_t pos;
    bool writable;
    bool seekable;
    int32_t status;
    uint32_t references;
    bool borrowed;
};

typedef struct LND_IO lnd_io;

lnd_io *lnd_io_open_input(const LND_IO_INPUT_PROCS *procs, void *user, uint64_t size);
lnd_io *lnd_io_open_memory(const void *data, size_t size);
lnd_io *lnd_io_create_procs(const LND_IO_OUTPUT_PROCS *procs, void *user);
int32_t lnd_io_window(lnd_io *io, uint64_t offset, uint64_t length);
lnd_io *lnd_io_ref(lnd_io *io);
int32_t lnd_io_close(lnd_io *io);
