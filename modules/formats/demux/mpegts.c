#include "demux.h"

#include <string.h>

static uint16_t lnd_ts_u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }

static uint32_t lnd_ts_crc(const uint8_t *data, size_t bytes) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < bytes; i++) {
        crc ^= (uint32_t)data[i] << 24;
        for (unsigned bit = 0; bit < 8; bit++) crc = crc << 1 ^ ((crc >> 31) ? 0x04c11db7u : 0);
    }
    return crc;
}

static int32_t lnd_ts_section(LND_DEMUX *d, uint16_t pid, const uint8_t *data, size_t bytes, bool start) {
    if (start) {
        if (!bytes || data[0] >= bytes) return LND_ERR_FORMAT;
        size_t pointer = (size_t)data[0] + 1;
        data += pointer;
        bytes -= pointer;
        d->section_size = 0;
        d->section_pid = pid;
    }
    if (pid != d->section_pid || !bytes) return LND_OK;
    size_t take = LND_MIN(bytes, sizeof d->section - d->section_size);
    memcpy(d->section + d->section_size, data, take);
    d->section_size += take;
    if (d->section_size < 3) return LND_OK;
    const uint8_t *section = d->section;
    size_t length = 3 + (lnd_ts_u16(section + 1) & 0xfff);
    if (length > sizeof d->section || length < 12) return LND_ERR_FORMAT;
    if (d->section_size < length) return LND_OK;
    if (lnd_ts_crc(section, length)) return LND_ERR_FORMAT;
    if (!(section[5] & 1)) return LND_OK;
    if (!pid && !section[0]) {
        for (size_t at = 8; at + 4 <= length - 4; at += 4) {
            if (lnd_ts_u16(section + at)) {
                d->pmt_pid = lnd_ts_u16(section + at + 2) & 0x1fff;
                break;
            }
        }
    } else if (pid == d->pmt_pid && section[0] == 2) {
        size_t at = 12 + (lnd_ts_u16(section + 10) & 0xfff);
        if (at > length - 4) return LND_ERR_FORMAT;
        for (; at + 5 <= length - 4;) {
            uint8_t type = section[at];
            uint16_t elementary = lnd_ts_u16(section + at + 1) & 0x1fff;
            size_t descriptors = lnd_ts_u16(section + at + 3) & 0xfff;
            if (descriptors > length - 4 - at - 5) return LND_ERR_FORMAT;
            if ((type == 0x0f || type == 0x03 || type == 0x04 || type == 0x81 || type == 0x87) && (!d->options.track_id || d->options.track_id == elementary) &&
                d->audio_pid == UINT16_MAX) {
                d->audio_pid = elementary;
                d->stream_type = type;
            }
            at += 5 + descriptors;
        }
    }
    d->section_size = 0;
    return LND_OK;
}

static int32_t lnd_ts_packet(LND_DEMUX *d, LND_DEMUX_PACKET *packet) {
    if (d->packet_size < 9 || memcmp(d->packet, "\0\0\1", 3)) return LND_ERR_FORMAT;
    const uint8_t *p = d->packet;
    size_t header = 9 + p[8];
    size_t declared = lnd_ts_u16(p + 4);
    size_t total = declared ? declared + 6 : d->packet_size;
    if (header > total || total > d->packet_size || (p[6] & 0xc0) != 0x80) return LND_ERR_FORMAT;
    if (p[7] & 0x80) {
        if (p[8] < 5 || !(p[9] & 1) || !(p[11] & 1) || !(p[13] & 1)) return LND_ERR_FORMAT;
        uint64_t pts = (uint64_t)(p[9] & 0x0e) << 29 | (uint64_t)p[10] << 22 | (uint64_t)(p[11] & 0xfe) << 14 | (uint64_t)p[12] << 7 | p[13] >> 1;
        if (d->previous_pts > pts && d->previous_pts - pts > (1ull << 32))
            d->wrap += 1ll << 33;
        else if (pts > d->previous_pts && pts - d->previous_pts > (1ull << 32) && d->wrap)
            d->wrap -= 1ll << 33;
        d->previous_pts = pts;
        int64_t ticks = (int64_t)pts + d->wrap;
        d->pts = ticks / 90000 * 1000000 + ticks % 90000 * 1000000 / 90000;
    }
    *packet = (LND_DEMUX_PACKET){.data = p + header,
                                 .bytes = total - header,
                                 .codec = d->stream_type == 0x0f   ? "aac"
                                          : d->stream_type == 0x81 ? "ac3"
                                          : d->stream_type == 0x87 ? "eac3"
                                                                   : "mp3",
                                 .track_id = d->audio_pid,
                                 .time_us = d->pts,
                                 .flags = d->discontinuity ? LND_DEMUX_DISCONTINUITY : 0};
    d->discontinuity = false;
    d->packet_pending = true;
    return LND_OK;
}

int32_t lnd_ts_read(LND_DEMUX *d, LND_DEMUX_PACKET *packet) {
    if (d->packet_pending) {
        d->packet_size = 0;
        d->packet_pending = false;
    }
    while (d->position < d->bytes) {
        const uint8_t *p = d->data + d->position;
        if (d->bytes - d->position < 188 || p[0] != 0x47 || p[1] & 0x80 || p[3] & 0xc0) return LND_ERR_FORMAT;
        uint16_t pid = (uint16_t)(p[1] & 0x1f) << 8 | p[2];
        bool start = (p[1] & 0x40) != 0;
        uint32_t adaptation = p[3] >> 4 & 3;
        size_t at = 4;
        if (!adaptation) return LND_ERR_FORMAT;
        if (adaptation & 2) {
            if (p[4] > 183) return LND_ERR_FORMAT;
            at = 5 + p[4];
            if (p[4] && (p[5] & 0x80) && pid == d->audio_pid) {
                d->continuity_valid = false;
                d->discontinuity = true;
            }
        }
        if (!(adaptation & 1) || at == 188) {
            d->position += 188;
            continue;
        }
        if (at > 188) return LND_ERR_FORMAT;
        if (!pid || pid == d->pmt_pid) {
            int32_t r = lnd_ts_section(d, pid, p + at, 188 - at, start);
            d->position += 188;
            if (r != LND_OK) return r;
            continue;
        }
        if (pid != d->audio_pid) {
            d->position += 188;
            continue;
        }
        if (start && d->packet_size) return lnd_ts_packet(d, packet);
        uint8_t counter = p[3] & 15;
        if (d->continuity_valid) {
            if (counter == d->continuity) {
                d->position += 188;
                continue;
            }
            if (counter != ((d->continuity + 1) & 15)) return LND_ERR_FORMAT;
        }
        d->continuity = counter;
        d->continuity_valid = true;
        if (!d->packet_size && !start) return LND_ERR_FORMAT;
        if (!lnd_demux_append(d, p + at, 188 - at)) return LND_ERR_OUT_OF_MEMORY;
        d->position += 188;
        if (d->packet_size >= 6) {
            size_t length = lnd_ts_u16(d->packet + 4);
            if (length && d->packet_size >= length + 6) return lnd_ts_packet(d, packet);
        }
    }
    if (d->packet_size && d->ended) return lnd_ts_packet(d, packet);
    return LND_DEMUX_END;
}
