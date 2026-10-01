#pragma once

#include "network/http/http.h"

int32_t lnd_http_file_begin(lnd_http_session *s, LND_IO **out);
int32_t lnd_http_file_write(lnd_http_session *s, LND_IO *io, const void *data, size_t bytes);
