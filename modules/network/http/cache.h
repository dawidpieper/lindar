#pragma once

#include "http.h"
#include "io/io.h"

typedef struct lnd_http_cache lnd_http_cache;

bool lnd_http_cache_begin(lnd_http_session *s, const char *url, const LND_HTTP_RESPONSE *response);
void lnd_http_cache_free(lnd_http_session *s);
void lnd_http_cache_stop(lnd_http_session *s);
bool lnd_http_cache_pending(const lnd_http_session *s);
int32_t lnd_http_cache_step(lnd_http_session *s, uint64_t offset, uint64_t bytes);
uint64_t lnd_http_cache_revision(const lnd_http_cache *cache);
uint64_t lnd_http_cache_size(const lnd_http_cache *cache);
size_t lnd_http_cache_bytes(const lnd_http_cache *cache);
bool lnd_http_cache_complete(const lnd_http_cache *cache);
bool lnd_http_cache_fits(const lnd_http_cache *cache);
void lnd_http_cache_protect(lnd_http_cache *cache, uint64_t start, uint64_t end);
size_t lnd_http_cache_reserve(lnd_http_cache *cache, uint64_t offset);
bool lnd_http_cache_put(lnd_http_cache *cache, uint64_t offset, const void *data, size_t bytes);
size_t lnd_http_cache_read(lnd_http_cache *cache, uint64_t offset, void *data, size_t bytes);
bool lnd_http_cache_has(lnd_http_cache *cache, uint64_t offset, size_t bytes);
void lnd_http_cache_end(lnd_http_cache *cache, uint64_t size);
lnd_io *lnd_http_cache_io(lnd_http_cache *cache);
bool lnd_http_cache_prefetch(lnd_http_session *s, uint64_t unread);
