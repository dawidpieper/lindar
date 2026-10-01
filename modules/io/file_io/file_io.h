#pragma once

#include "io/io.h"
#include "lindar_file_io.h"
#include <stdio.h>

lnd_io *lnd_io_open_file(const wchar_t *path);
lnd_io *lnd_io_open_file_utf8(const char *path);
lnd_io *lnd_io_create_file(const wchar_t *path);
lnd_io *lnd_io_create_file_utf8(const char *path);
lnd_io *lnd_io_from_file(FILE *file, bool writable);

lnd_io *lnd_io_create_atomic_file(const wchar_t *path);
lnd_io *lnd_io_create_atomic_file_utf8(const char *path);
