#pragma once

#include "http.h"

typedef struct lnd_http_ogg lnd_http_ogg;

lnd_http_ogg *lnd_http_ogg_begin(lnd_http_session *s, const uint8_t *data, size_t bytes);
bool lnd_http_ogg_matches(const lnd_http_session *s, const lnd_http_ogg *ogg);
bool lnd_http_ogg_collecting(const lnd_http_ogg *ogg);
void lnd_http_ogg_end(lnd_http_ogg *ogg);
bool lnd_http_ogg_step(lnd_http_session *s, lnd_http_ogg *ogg);
size_t lnd_http_ogg_buffered(const lnd_http_ogg *ogg);
void lnd_http_ogg_stop(lnd_http_session *s, lnd_http_ogg *ogg);
void lnd_http_ogg_free(lnd_http_session *s, lnd_http_ogg *ogg);
