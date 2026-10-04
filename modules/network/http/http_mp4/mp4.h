#pragma once

#include "network/http/http.h"

typedef struct lnd_http_mp4 lnd_http_mp4;

bool lnd_http_mp4_changed(const lnd_http_mp4 *mp4);
void lnd_http_mp4_cache_info(lnd_http_session *s, lnd_http_mp4 *mp4, LND_HTTP_INFO *info);
size_t lnd_http_mp4_buffered(const lnd_http_mp4 *mp4);
int32_t lnd_http_mp4_begin(lnd_http_session *s, const uint8_t *data, size_t bytes, lnd_http_mp4 **out);
int32_t lnd_http_mp4_step(lnd_http_session *s, lnd_http_mp4 *mp4, uint32_t budget);
int32_t lnd_http_mp4_seek(lnd_http_session *s, lnd_http_mp4 *mp4, int64_t time_us);
void lnd_http_mp4_free(lnd_http_mp4 *mp4);
