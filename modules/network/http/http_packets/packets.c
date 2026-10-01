#include "network/http/http.h"
#include "packets.h"
#include "lindar_demux.h"
#include <string.h>

static void lnd_packet_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

int32_t lnd_http_packet(lnd_http_session *source, const LND_DEMUX_PACKET *packet, lnd_http_bytes *out) {
    bool elementary = !(packet->flags & LND_DEMUX_CONFIG) && (!strcmp(packet->codec, "ac3") || !strcmp(packet->codec, "eac3"));
    if (((packet->flags & LND_DEMUX_CONFIG) && strcmp(packet->codec, "mp3")) || elementary) {
        LND_CODEC_STREAM_CONFIG config = {.codec = packet->codec,
                                          .data = packet->config,
                                          .bytes = packet->config_bytes,
                                          .sample_rate_hz = packet->sample_rate_hz,
                                          .channels = packet->channels,
                                          .elementary = elementary,
                                          .trim_known = (packet->flags & LND_DEMUX_EDIT) != 0,
                                          .trim_start_frames = packet->trim_start_frames};
        int32_t r = lnd_decoder_configure(source->decoder, &config);
        if (r != LND_OK) return r;
        uint64_t samples = packet->timescale ? (uint64_t)packet->duration_ticks * packet->sample_rate_hz / packet->timescale : 0;
        samples -= LND_MIN(samples, packet->trim_end_frames);
        if (packet->bytes > UINT32_MAX || samples > UINT32_MAX) return LND_ERR_FORMAT;
        uint8_t header[8];
        lnd_packet_u32(header, (uint32_t)packet->bytes);
        lnd_packet_u32(header + 4, (uint32_t)samples);
        if (!lnd_http_bytes_append(out, header, sizeof header)) return LND_ERR_OUT_OF_MEMORY;
    }
    return lnd_http_bytes_append(out, packet->data, packet->bytes) ? LND_OK : LND_ERR_OUT_OF_MEMORY;
}
