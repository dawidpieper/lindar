#include "demux.h"

#include <string.h>

bool lnd_demux_append(LND_DEMUX *d, const void *data, size_t bytes) {
    if (bytes > d->options.packet_bytes - d->packet_size) return false;
    size_t need = d->packet_size + bytes;
    if (need > d->packet_capacity) {
        size_t cap = d->packet_capacity ? d->packet_capacity : LND_MIN((size_t)4096, d->options.packet_bytes);
        while (cap < need) cap = cap > d->options.packet_bytes / 2 ? d->options.packet_bytes : cap * 2;
        void *grown = lnd_realloc(d->packet, cap);
        if (!grown) return false;
        d->packet = grown;
        d->packet_capacity = cap;
    }
    if (bytes) memcpy(d->packet + d->packet_size, data, bytes);
    d->packet_size += bytes;
    return true;
}

LND_DEMUX *LND_DemuxCreate(const LND_DEMUX_OPTIONS *options) {
    LND_DEMUX_OPTIONS o = options ? *options : (LND_DEMUX_OPTIONS){0};
    if (!o.packet_bytes) o.packet_bytes = 1024 * 1024;
    if (o.container < LND_DEMUX_AUTO || o.container > LND_DEMUX_FMP4 || o.packet_bytes < 188) return lnd_error_null(LND_ERR_INVALID_ARG);
    LND_DEMUX *d = lnd_alloc_zero(sizeof *d);
    if (!d) return nullptr;
    d->options = o;
    d->pmt_pid = d->audio_pid = UINT16_MAX;
    return d;
}

int32_t LND_DemuxSetInit(LND_DEMUX *d, const void *data, size_t bytes) {
    if (!d || !data || !bytes) return LND_ERR_INVALID_ARG;
    d->indexed_count = 0;
    return lnd_demux_mp4_init(d, data, bytes);
}

int32_t LND_DemuxBegin(LND_DEMUX *d, const void *data, size_t bytes) { return LND_DemuxBeginAt(d, data, bytes, 0); }

int32_t LND_DemuxBeginAt(LND_DEMUX *d, const void *data, size_t bytes, uint64_t offset_bytes) {
    if (!d || !data || !bytes || d->ended || d->error) return d && d->error ? d->error : LND_ERR_INVALID_ARG;
    d->data = data;
    d->bytes = bytes;
    d->position = 0;
    d->file_offset = offset_bytes;
    d->kind = d->options.container;
    if (d->kind == LND_DEMUX_AUTO) d->kind = ((const uint8_t *)data)[0] == 0x47 ? LND_DEMUX_MPEGTS : LND_DEMUX_FMP4;
    if (d->kind == LND_DEMUX_MPEGTS) return bytes % 188 ? LND_ERR_FORMAT : LND_OK;
    return lnd_demux_mp4_begin(d);
}

int32_t LND_DemuxEnd(LND_DEMUX *d) {
    if (!d || !d->data) return LND_ERR_INVALID_ARG;
    d->ended = true;
    return d->error ? d->error : LND_OK;
}

int32_t LND_DemuxRead(LND_DEMUX *d, LND_DEMUX_PACKET *packet) {
    if (!d || !packet || !d->data) return LND_ERR_INVALID_ARG;
    if (d->error) return d->error;
    *packet = (LND_DEMUX_PACKET){0};
    int32_t r = d->kind == LND_DEMUX_MPEGTS ? lnd_ts_read(d, packet) : lnd_demux_mp4_read(d, packet);
    if (r < 0) d->error = r;
    return r;
}

uint64_t LND_DemuxGetSampleCount(const LND_DEMUX *d) { return d ? d->indexed_count : 0; }

int32_t LND_DemuxGetSample(const LND_DEMUX *object, uint64_t index, LND_DEMUX_SAMPLE *sample) {
    LND_DEMUX *d = (LND_DEMUX *)object;
    if (!d || !sample || index >= d->indexed_count) return LND_ERR_INVALID_ARG;
    const lnd_demux_mp4_sample *entry = &d->samples[(size_t)index];
    *sample = (LND_DEMUX_SAMPLE){.offset_bytes = entry->offset};
    return lnd_demux_mp4_sample_info(d, entry, &sample->packet);
}

void LND_DemuxReset(LND_DEMUX *d) {
    if (!d) return;
    d->data = nullptr;
    d->bytes = d->position = 0;
    d->packet_size = d->section_size = 0;
    d->continuity_valid = d->packet_pending = false;
    d->error = 0;
    d->ended = false;
    d->sample_count = d->sample_index = 0;
    d->previous_pts = 0;
    d->wrap = 0;
    d->discontinuity = true;
}

void LND_DemuxFree(LND_DEMUX *d) {
    if (!d) return;
    lnd_free(d->packet);
    lnd_free(d->samples);
    lnd_free(d);
}
