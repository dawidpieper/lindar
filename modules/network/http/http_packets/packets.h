#pragma once

#include "network/http/http.h"
#include "lindar_demux.h"

int32_t lnd_http_packet(lnd_http_session *s, const LND_DEMUX_PACKET *packet, lnd_http_bytes *out);
