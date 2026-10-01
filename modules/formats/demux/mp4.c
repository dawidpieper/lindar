#include "demux.h"
#include "formats/mp4/read.h"

#include <string.h>

static int32_t lnd_demux_mp4_reserve(LND_DEMUX *d, size_t count) {
    size_t limit = d->options.packet_bytes / sizeof *d->samples;
    if (count > limit) return LND_ERR_FORMAT;
    if (count <= d->sample_capacity) return LND_OK;
    size_t capacity = d->sample_capacity ? d->sample_capacity : LND_MIN(limit, (size_t)64);
    while (capacity < count) capacity = capacity > limit / 2 ? limit : capacity * 2;
    void *data = lnd_realloc(d->samples, capacity * sizeof *d->samples);
    if (!data) return LND_ERR_OUT_OF_MEMORY;
    d->samples = data;
    d->sample_capacity = capacity;
    return LND_OK;
}

static bool lnd_demux_mp4_descriptor(LND_DEMUX *d, const uint8_t *data, size_t bytes) {
    lnd_mp4_config config = {0};
    if (!lnd_mp4_descriptors(data, bytes, 0, &config) || config.size > sizeof d->config) return false;
    if (config.object_type == 0x69 || config.object_type == 0x6b) strcpy(d->codec, "mp3");
    if (config.data) {
        memcpy(d->config, config.data, config.size);
        d->config_size = config.size;
    }
    return true;
}

static void lnd_demux_mp4_location(void *user, uint32_t index, uint64_t offset, uint32_t bytes) {
    LND_DEMUX *d = user;
    d->samples[index] = (lnd_demux_mp4_sample){.offset = offset, .bytes = bytes};
}

static int32_t lnd_demux_mp4_edit(LND_DEMUX *d, const lnd_mp4_box *trak) {
    lnd_mp4_box edts, elst;
    d->edit = false;
    d->edit_start = d->edit_end = 0;
    if (!lnd_mp4_child(trak, LND_MP4_TAG('e', 'd', 't', 's'), &edts)) return LND_OK;
    if (!lnd_mp4_child(&edts, LND_MP4_TAG('e', 'l', 's', 't'), &elst) || elst.bytes < 8 || elst.data[0] > 1) return LND_ERR_FORMAT;
    bool wide = elst.data[0] != 0;
    uint32_t count = lnd_mp4_u32(elst.data + 4);
    size_t width = wide ? 20 : 12;
    if (count > (elst.bytes - 8) / width) return LND_ERR_FORMAT;
    if (!count) return LND_OK;
    if (count != 1 || !d->movie_timescale) return LND_ERR_UNSUPPORTED;
    const uint8_t *p = elst.data + 8;
    uint64_t duration = wide ? lnd_mp4_u64(p) : lnd_mp4_u32(p);
    int64_t start = wide ? (int64_t)lnd_mp4_u64(p + 8) : (int32_t)lnd_mp4_u32(p + 4);
    uint32_t media_rate_q16 = lnd_mp4_u32(p + (wide ? 16 : 8));
    if (start < 0 || media_rate_q16 != 0x10000) return LND_ERR_UNSUPPORTED;
    uint64_t seconds = duration / d->movie_timescale;
    if (seconds > (uint64_t)INT64_MAX / d->timescale) return LND_ERR_FORMAT;
    uint64_t ticks = seconds * d->timescale + (duration % d->movie_timescale * d->timescale + d->movie_timescale - 1) / d->movie_timescale;
    if (ticks > (uint64_t)INT64_MAX - (uint64_t)start) return LND_ERR_FORMAT;
    d->edit_start = (uint64_t)start;
    d->edit_end = duration ? (uint64_t)start + ticks : 0;
    d->edit = true;
    return LND_OK;
}

static int32_t lnd_demux_mp4_index(LND_DEMUX *d, const lnd_mp4_box *stbl) {
    lnd_mp4_box stsz, stsc, stco, stts, ctts;
    d->indexed_count = 0;
    if (!lnd_mp4_child(stbl, LND_MP4_TAG('s', 't', 's', 'z'), &stsz)) return LND_OK;
    if (stsz.bytes < 12) return LND_ERR_FORMAT;
    uint32_t fixed = lnd_mp4_u32(stsz.data + 4), count = lnd_mp4_u32(stsz.data + 8);
    if (!count) return LND_OK;
    if (count > d->options.packet_bytes / sizeof *d->samples || (!fixed && count > (stsz.bytes - 12) / 4) ||
        !lnd_mp4_child(stbl, LND_MP4_TAG('s', 't', 's', 'c'), &stsc) || stsc.bytes < 8 || !lnd_mp4_child(stbl, LND_MP4_TAG('s', 't', 't', 's'), &stts) ||
        stts.bytes < 8)
        return LND_ERR_FORMAT;
    bool wide = !lnd_mp4_child(stbl, LND_MP4_TAG('s', 't', 'c', 'o'), &stco);
    if ((wide && !lnd_mp4_child(stbl, LND_MP4_TAG('c', 'o', '6', '4'), &stco)) || stco.bytes < 8) return LND_ERR_FORMAT;
    uint32_t chunks = lnd_mp4_u32(stco.data + 4), runs = lnd_mp4_u32(stsc.data + 4);
    uint32_t times = lnd_mp4_u32(stts.data + 4);
    if (!chunks || chunks > (stco.bytes - 8) / (wide ? 8 : 4) || !runs || runs > (stsc.bytes - 8) / 12 || !times || times > (stts.bytes - 8) / 8)
        return LND_ERR_FORMAT;
    int32_t reserved = lnd_demux_mp4_reserve(d, count);
    if (reserved != LND_OK) return reserved;
    lnd_mp4_table chunk_table, run_table, size_table = {0};
    if (!lnd_mp4_table_view(&chunk_table, stco.data + 8, stco.bytes - 8, chunks, wide ? 8 : 4) ||
        !lnd_mp4_table_view(&run_table, stsc.data + 8, stsc.bytes - 8, runs, 12) ||
        (!fixed && !lnd_mp4_table_view(&size_table, stsz.data + 12, stsz.bytes - 12, count, 4)))
        return LND_ERR_FORMAT;
    int32_t result = lnd_mp4_locations(&chunk_table, &run_table, &size_table, fixed, count, d->options.packet_bytes, lnd_demux_mp4_location, d);
    if (result != LND_OK) return result;
    size_t index = 0;
    uint64_t time = 0;
    for (uint32_t i = 0; i < times; i++) {
        const uint8_t *entry = stts.data + 8 + (size_t)i * 8;
        uint32_t n = lnd_mp4_u32(entry), duration = lnd_mp4_u32(entry + 4);
        if (!n || !duration || n > count - index || (uint64_t)n * duration > (uint64_t)INT64_MAX - time) return LND_ERR_FORMAT;
        for (uint32_t j = 0; j < n; j++, index++) {
            d->samples[index].time = (int64_t)time;
            d->samples[index].duration = duration;
            time += duration;
        }
    }
    if (index != count) return LND_ERR_FORMAT;
    if (lnd_mp4_child(stbl, LND_MP4_TAG('c', 't', 't', 's'), &ctts)) {
        if (ctts.bytes < 8 || ctts.data[0] > 1) return LND_ERR_FORMAT;
        uint32_t entries = lnd_mp4_u32(ctts.data + 4);
        if (entries > (ctts.bytes - 8) / 8) return LND_ERR_FORMAT;
        index = 0;
        for (uint32_t i = 0; i < entries; i++) {
            const uint8_t *entry = ctts.data + 8 + (size_t)i * 8;
            uint32_t n = lnd_mp4_u32(entry), value = lnd_mp4_u32(entry + 4);
            int64_t offset = ctts.data[0] ? (int32_t)value : (int64_t)value;
            if (n > count - index) return LND_ERR_FORMAT;
            for (uint32_t j = 0; j < n; j++, index++) {
                if (offset > 0 && d->samples[index].time > INT64_MAX - offset) return LND_ERR_FORMAT;
                d->samples[index].time += offset;
            }
        }
        if (index != count) return LND_ERR_FORMAT;
    }
    d->indexed_count = d->sample_count = count;
    d->decode_time = time;
    return LND_OK;
}

static int32_t lnd_demux_mp4_track(LND_DEMUX *d, const lnd_mp4_box *trak) {
    lnd_mp4_box tkhd, mdia, mdhd, hdlr, minf, stbl, stsd, entry;
    if (!lnd_mp4_child(trak, LND_MP4_TAG('t', 'k', 'h', 'd'), &tkhd) || tkhd.bytes < 24 || !lnd_mp4_child(trak, LND_MP4_TAG('m', 'd', 'i', 'a'), &mdia) ||
        !lnd_mp4_child(&mdia, LND_MP4_TAG('h', 'd', 'l', 'r'), &hdlr) || hdlr.bytes < 12 || lnd_mp4_u32(hdlr.data + 8) != LND_MP4_TAG('s', 'o', 'u', 'n'))
        return LND_ERR_UNSUPPORTED;
    if (tkhd.data[0] > 1) return LND_ERR_FORMAT;
    uint32_t track = lnd_mp4_u32(tkhd.data + (tkhd.data[0] ? 20 : 12));
    if (d->options.track_id && d->options.track_id != track) return LND_ERR_UNSUPPORTED;
    if (!lnd_mp4_child(&mdia, LND_MP4_TAG('m', 'd', 'h', 'd'), &mdhd) || mdhd.bytes < 24 || mdhd.data[0] > 1 ||
        !lnd_mp4_child(&mdia, LND_MP4_TAG('m', 'i', 'n', 'f'), &minf) || !lnd_mp4_child(&minf, LND_MP4_TAG('s', 't', 'b', 'l'), &stbl) ||
        !lnd_mp4_child(&stbl, LND_MP4_TAG('s', 't', 's', 'd'), &stsd) || stsd.bytes < 8)
        return LND_ERR_FORMAT;
    size_t at = 8;
    if (!lnd_mp4_next(stsd.data, stsd.bytes, &at, &entry) || entry.bytes < 28) return LND_ERR_FORMAT;
    const char *codec = entry.type == LND_MP4_TAG('m', 'p', '4', 'a')   ? "aac"
                        : entry.type == LND_MP4_TAG('O', 'p', 'u', 's') ? "opus"
                        : entry.type == LND_MP4_TAG('f', 'L', 'a', 'C') ? "flac"
                        : entry.type == LND_MP4_TAG('a', 'l', 'a', 'c') ? "alac"
                        : entry.type == LND_MP4_TAG('.', 'm', 'p', '3') ? "mp3"
                        : entry.type == LND_MP4_TAG('a', 'c', '-', '3') ? "ac3"
                        : entry.type == LND_MP4_TAG('e', 'c', '-', '3') ? "eac3"
                                                                        : nullptr;
    if (!codec) return LND_ERR_UNSUPPORTED;
    uint32_t timescale = lnd_mp4_u32(mdhd.data + (mdhd.data[0] ? 20 : 12));
    uint32_t channels = (uint32_t)entry.data[16] << 8 | entry.data[17];
    uint32_t sample_rate_hz = lnd_mp4_u32(entry.data + 24) >> 16;
    if (!timescale || !sample_rate_hz || !channels || channels > LND_MAX_CHANNELS) return LND_ERR_FORMAT;
    d->track = track;
    d->channels = channels;
    d->sample_rate_hz = sample_rate_hz;
    d->timescale = timescale;
    strcpy(d->codec, codec);
    d->config_size = 0;
    uint32_t version = (uint32_t)entry.data[8] << 8 | entry.data[9];
    at = 28 + (version == 1 ? 16 : version == 2 ? 36 : 0);
    if (at > entry.bytes) return LND_ERR_FORMAT;
    while (at < entry.bytes) {
        lnd_mp4_box config;
        if (!lnd_mp4_next(entry.data, entry.bytes, &at, &config)) return LND_ERR_FORMAT;
        if (config.type == LND_MP4_TAG('e', 's', 'd', 's')) {
            if (config.bytes < 4 || !lnd_demux_mp4_descriptor(d, config.data + 4, config.bytes - 4)) return LND_ERR_FORMAT;
        } else if (config.type == LND_MP4_TAG('d', 'O', 'p', 's') || config.type == LND_MP4_TAG('d', 'f', 'L', 'a') ||
                   config.type == LND_MP4_TAG('a', 'l', 'a', 'c')) {
            if (config.bytes > sizeof d->config) return LND_ERR_UNSUPPORTED;
            memcpy(d->config, config.data, config.bytes);
            d->config_size = config.bytes;
        }
    }
    if (!strcmp(d->codec, "aac") && d->config_size < 2) return LND_ERR_FORMAT;
    int32_t r = lnd_demux_mp4_edit(d, trak);
    return r == LND_OK ? lnd_demux_mp4_index(d, &stbl) : r;
}

int32_t lnd_demux_mp4_init(LND_DEMUX *d, const uint8_t *data, size_t bytes) {
    size_t at = 0;
    lnd_mp4_box moov = {0}, box;
    while (at < bytes) {
        if (!lnd_mp4_next(data, bytes, &at, &box)) return LND_ERR_FORMAT;
        if (box.type == LND_MP4_TAG('m', 'o', 'o', 'v')) moov = box;
    }
    if (!moov.data) return LND_ERR_FORMAT;
    lnd_mp4_box mvhd;
    d->movie_timescale = 0;
    if (lnd_mp4_child(&moov, LND_MP4_TAG('m', 'v', 'h', 'd'), &mvhd)) {
        if (mvhd.bytes < 20 || mvhd.data[0] > 1 || (mvhd.data[0] && mvhd.bytes < 32)) return LND_ERR_FORMAT;
        d->movie_timescale = lnd_mp4_u32(mvhd.data + (mvhd.data[0] ? 20 : 12));
    }
    int32_t result = LND_ERR_UNSUPPORTED;
    at = 0;
    while (at < moov.bytes) {
        if (!lnd_mp4_next(moov.data, moov.bytes, &at, &box)) return LND_ERR_FORMAT;
        if (box.type != LND_MP4_TAG('t', 'r', 'a', 'k')) continue;
        result = lnd_demux_mp4_track(d, &box);
        if (result == LND_OK) break;
        if (result != LND_ERR_UNSUPPORTED) return result;
    }
    if (result != LND_OK) return result;
    lnd_mp4_box mvex;
    d->default_duration = d->default_size = 0;
    if (lnd_mp4_child(&moov, LND_MP4_TAG('m', 'v', 'e', 'x'), &mvex)) {
        at = 0;
        while (at < mvex.bytes) {
            if (!lnd_mp4_next(mvex.data, mvex.bytes, &at, &box)) return LND_ERR_FORMAT;
            if (box.type == LND_MP4_TAG('t', 'r', 'e', 'x') && box.bytes >= 24 && lnd_mp4_u32(box.data + 4) == d->track) {
                d->default_duration = lnd_mp4_u32(box.data + 12);
                d->default_size = lnd_mp4_u32(box.data + 16);
            }
        }
    }
    return LND_OK;
}

static bool lnd_demux_mp4_media_range(LND_DEMUX *d, size_t offset, size_t bytes) {
    if (offset >= d->media_start && offset < d->media_end) return bytes <= d->media_end - offset;
    size_t at = offset >= d->media_end ? d->media_end : 0;
    lnd_mp4_box box;
    while (at < d->bytes) {
        if (!lnd_mp4_next(d->data, d->bytes, &at, &box)) return false;
        size_t start = (size_t)(box.data - d->data);
        if (box.type == LND_MP4_TAG('m', 'd', 'a', 't') && offset >= start && offset <= box.end && bytes <= box.end - offset) {
            d->media_start = start;
            d->media_end = box.end;
            return true;
        }
    }
    return false;
}

static int32_t lnd_demux_mp4_traf(LND_DEMUX *d, const lnd_mp4_box *moof, const lnd_mp4_box *traf) {
    lnd_mp4_box tfhd, tfdt, box;
    if (!lnd_mp4_child(traf, LND_MP4_TAG('t', 'f', 'h', 'd'), &tfhd) || tfhd.bytes < 8) return LND_ERR_FORMAT;
    if (lnd_mp4_u32(tfhd.data + 4) != d->track) return LND_OK;
    uint32_t flags = lnd_mp4_u32(tfhd.data) & 0xffffff;
    size_t at = 8;
    uint64_t base = moof->offset;
    uint32_t duration = d->default_duration, size = d->default_size;
    if (flags & 1) {
        if (tfhd.bytes - at < 8) return LND_ERR_FORMAT;
        base = lnd_mp4_u64(tfhd.data + at);
        if (base < d->file_offset) return LND_ERR_FORMAT;
        base -= d->file_offset;
        at += 8;
    }
    if (flags & 2) at += 4;
    if (at > tfhd.bytes) return LND_ERR_FORMAT;
    if (flags & 8) {
        if (tfhd.bytes - at < 4) return LND_ERR_FORMAT;
        duration = lnd_mp4_u32(tfhd.data + at);
        at += 4;
    }
    if (flags & 16) {
        if (tfhd.bytes - at < 4) return LND_ERR_FORMAT;
        size = lnd_mp4_u32(tfhd.data + at);
        at += 4;
    }
    if (flags & 32) at += 4;
    if (at > tfhd.bytes || base > d->bytes) return LND_ERR_FORMAT;
    uint64_t time = d->decode_time;
    if (lnd_mp4_child(traf, LND_MP4_TAG('t', 'f', 'd', 't'), &tfdt)) {
        if (tfdt.bytes < 8 || tfdt.data[0] > 1 || (tfdt.data[0] && tfdt.bytes < 12)) return LND_ERR_FORMAT;
        time = tfdt.data[0] ? lnd_mp4_u64(tfdt.data + 4) : lnd_mp4_u32(tfdt.data + 4);
    }
    size_t position = moof->end;
    size_t scan = moof->end;
    while (scan < d->bytes) {
        if (!lnd_mp4_next(d->data, d->bytes, &scan, &box)) return LND_ERR_FORMAT;
        if (box.type == LND_MP4_TAG('m', 'd', 'a', 't')) {
            position = (size_t)(box.data - d->data);
            break;
        }
    }
    at = 0;
    while (at < traf->bytes) {
        if (!lnd_mp4_next(traf->data, traf->bytes, &at, &box)) return LND_ERR_FORMAT;
        if (box.type != LND_MP4_TAG('t', 'r', 'u', 'n')) continue;
        if (box.bytes < 8 || box.data[0] > 1) return LND_ERR_FORMAT;
        uint32_t run_flags = lnd_mp4_u32(box.data) & 0xffffff;
        uint32_t count = lnd_mp4_u32(box.data + 4);
        if (count > d->options.packet_bytes / sizeof(lnd_demux_mp4_sample) - d->sample_count) return LND_ERR_FORMAT;
        size_t cursor = 8;
        if (run_flags & 1) {
            if (box.bytes - cursor < 4) return LND_ERR_FORMAT;
            int64_t offset = (int32_t)lnd_mp4_u32(box.data + cursor);
            int64_t absolute = (int64_t)base + offset;
            if (absolute < 0 || (uint64_t)absolute > d->bytes) return LND_ERR_FORMAT;
            position = (size_t)absolute;
            cursor += 4;
        }
        if (run_flags & 4) cursor += 4;
        size_t fields = !!(run_flags & 0x100) + !!(run_flags & 0x200) + !!(run_flags & 0x400) + !!(run_flags & 0x800);
        if (cursor > box.bytes || (fields && count > (box.bytes - cursor) / (fields * 4))) return LND_ERR_FORMAT;
        int32_t reserved = lnd_demux_mp4_reserve(d, d->sample_count + count);
        if (reserved != LND_OK) return reserved;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t frame_duration = duration, frame_size = size;
            int64_t composition = 0;
            if (run_flags & 0x100) {
                frame_duration = lnd_mp4_u32(box.data + cursor);
                cursor += 4;
            }
            if (run_flags & 0x200) {
                frame_size = lnd_mp4_u32(box.data + cursor);
                cursor += 4;
            }
            if (run_flags & 0x400) cursor += 4;
            if (run_flags & 0x800) {
                uint32_t value = lnd_mp4_u32(box.data + cursor);
                composition = box.data[0] ? (int32_t)value : (int64_t)value;
                cursor += 4;
            }
            if (!frame_size || !frame_duration || time > INT64_MAX || frame_duration > (uint64_t)INT64_MAX - time ||
                (composition > 0 && (uint64_t)composition > (uint64_t)INT64_MAX - time) || frame_size > d->options.packet_bytes ||
                !lnd_demux_mp4_media_range(d, position, frame_size))
                return LND_ERR_FORMAT;
            d->samples[d->sample_count++] =
                (lnd_demux_mp4_sample){.offset = position, .bytes = frame_size, .duration = frame_duration, .time = (int64_t)time + composition};
            position += frame_size;
            time += frame_duration;
        }
    }
    d->decode_time = time;
    return LND_OK;
}

int32_t lnd_demux_mp4_begin(LND_DEMUX *d) {
    d->media_start = d->media_end = 0;
    d->indexed_count = 0;
    d->sample_count = d->sample_index = 0;
    if (!d->track) {
        int32_t r = lnd_demux_mp4_init(d, d->data, d->bytes);
        if (r != LND_OK) return r;
    }
    size_t at = 0;
    lnd_mp4_box box;
    while (at < d->bytes) {
        if (!lnd_mp4_next(d->data, d->bytes, &at, &box)) return LND_ERR_FORMAT;
        if (box.type != LND_MP4_TAG('m', 'o', 'o', 'f')) continue;
        size_t cursor = 0;
        lnd_mp4_box child;
        while (cursor < box.bytes) {
            if (!lnd_mp4_next(box.data, box.bytes, &cursor, &child)) return LND_ERR_FORMAT;
            if (child.type != LND_MP4_TAG('t', 'r', 'a', 'f')) continue;
            int32_t r = lnd_demux_mp4_traf(d, &box, &child);
            if (r != LND_OK) return r;
        }
    }
    return d->sample_count ? LND_OK : LND_ERR_FORMAT;
}

int32_t lnd_demux_mp4_sample_info(LND_DEMUX *d, const lnd_demux_mp4_sample *sample, LND_DEMUX_PACKET *packet) {
    if (d->edit_end && sample->time >= 0 && (uint64_t)sample->time >= d->edit_end) return LND_DEMUX_END;
    uint64_t trim = d->edit && sample->time >= 0 && d->edit_start > (uint64_t)sample->time ? d->edit_start - (uint64_t)sample->time : 0;
    if (trim > UINT64_MAX / d->sample_rate_hz || trim * d->sample_rate_hz / d->timescale > UINT32_MAX) return LND_ERR_FORMAT;
    uint64_t excess = d->edit_end && sample->time >= 0 && (uint64_t)sample->time + sample->duration > d->edit_end
                          ? (uint64_t)sample->time + sample->duration - d->edit_end
                          : 0;
    int64_t presentation = sample->time;
    if (d->edit_start) {
        if (sample->time < 0 && (uint64_t)(-(sample->time + 1)) + 1 > (uint64_t)INT64_MAX - d->edit_start) return LND_ERR_FORMAT;
        presentation -= (int64_t)d->edit_start;
    }
    int64_t seconds = presentation / d->timescale;
    if (seconds > INT64_MAX / 1000000 || seconds < INT64_MIN / 1000000) return LND_ERR_FORMAT;
    int64_t base_us = seconds * 1000000;
    int64_t fraction_us = presentation % d->timescale * 1000000 / d->timescale;
    if ((fraction_us > 0 && base_us > INT64_MAX - fraction_us) || (fraction_us < 0 && base_us < INT64_MIN - fraction_us)) return LND_ERR_FORMAT;
    *packet = (LND_DEMUX_PACKET){.bytes = sample->bytes,
                                 .codec = d->codec,
                                 .config = d->config,
                                 .config_bytes = d->config_size,
                                 .track_id = d->track,
                                 .channels = d->channels,
                                 .sample_rate_hz = d->sample_rate_hz,
                                 .flags = LND_DEMUX_CONFIG | ((d->edit_start || sample->time > 0) ? LND_DEMUX_EDIT : 0),
                                 .trim_start_frames = (uint32_t)(trim * d->sample_rate_hz / d->timescale),
                                 .trim_end_frames = (uint32_t)(excess * d->sample_rate_hz / d->timescale),
                                 .time_us = base_us + fraction_us,
                                 .duration_ticks = sample->duration,
                                 .timescale = d->timescale,
                                 .duration_us = (int64_t)((uint64_t)sample->duration * 1000000 / d->timescale)};
    return LND_OK;
}

int32_t lnd_demux_mp4_read(LND_DEMUX *d, LND_DEMUX_PACKET *packet) {
    if (d->sample_index == d->sample_count) return LND_DEMUX_END;
    const lnd_demux_mp4_sample *sample = &d->samples[d->sample_index++];
    if (sample->offset > d->bytes || sample->bytes > d->bytes - (size_t)sample->offset) return LND_ERR_FORMAT;
    int32_t r = lnd_demux_mp4_sample_info(d, sample, packet);
    if (r == LND_OK) packet->data = d->data + (size_t)sample->offset;
    return r;
}
