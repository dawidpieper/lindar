#pragma once
#include "playlist.h"
#include "lindar_demux.h"

typedef struct lnd_hls_session lnd_hls_session;

int32_t lnd_hls_begin(lnd_http_session *source, const uint8_t *data, size_t bytes, const char *url, lnd_hls_session **out);
int32_t lnd_hls_step(lnd_http_session *source, lnd_hls_session *hls, uint32_t budget);
int32_t lnd_hls_seek(lnd_http_session *source, lnd_hls_session *hls, int64_t time_us, bool live);
int32_t lnd_http_media_end(lnd_http_session *source, LND_DEMUX *demux, lnd_http_bytes *out);
size_t lnd_hls_buffered(const lnd_hls_session *hls);
void lnd_hls_free(lnd_hls_session *hls);
int32_t lnd_http_media(lnd_http_session *source, LND_DEMUX **demux, uint64_t offset, int64_t time_us, const uint8_t *data, size_t bytes, const uint8_t *init,
                       size_t init_bytes, lnd_http_bytes *out);
