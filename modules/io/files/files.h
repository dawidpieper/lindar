#pragma once

#include "io/file_io/file_io.h"

void lnd_path_extension(const wchar_t *path, char *out, size_t cap);
void lnd_path_extension_utf8(const char *path, char *out, size_t cap);
