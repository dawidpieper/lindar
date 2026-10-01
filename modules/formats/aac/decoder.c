#include <aacdecoder_lib.h>

#include "io/reader.h"
#include "formats/mp4/read.h"
#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_codecs.h"

#include <string.h>

#define LND_AAC_MAX_PCM (2048 * 8)
#define LND_AAC_PREROLL_LC 2
#define LND_AAC_PREROLL_SBR 6

typedef struct lnd_aac_state {
    HANDLE_AACDECODER dec;
    lnd_io *io;
    bool adts;
    uint64_t *offsets;
    uint32_t *sizes;
    uint32_t count;
    uint32_t cur;
    uint32_t channels;
    uint32_t sample_rate_hz;
    uint32_t frame_size;
    uint64_t total;
    uint64_t pos;
    uint64_t delay;
    uint64_t end;
    uint64_t target;
    uint64_t edit_start;
    uint64_t edit_duration;
    uint32_t media_timescale;
    uint32_t output_delay;
    uint32_t flushed;
    uint32_t preroll;
    uint8_t asc[64];
    uint32_t asc_size;
    uint8_t *au;
    size_t au_cap;
    INT_PCM *pcm;
    size_t pending_frames;
    size_t pending_offset;
} lnd_aac_state;

typedef struct lnd_mp4_track {
    lnd_mp4_table chunks, runs, sizes;
    uint32_t sample_count;
    uint32_t uniform_size;
    uint8_t asc[64];
    uint32_t asc_len;
    uint32_t timescale;
    uint32_t movie_timescale;
    int64_t media_time;
    uint64_t segment_duration;
    bool has_mp4a;
} lnd_mp4_track;

static bool lnd_mp4_read_at(lnd_io *io, uint64_t at, void *dst, size_t n) { return n <= INT64_MAX && LND_IoSeekBytes(io, at) == LND_OK && LND_IoRead(io, dst, n) == (int64_t)n; }

static bool lnd_mp4_read_box(lnd_io *io, uint64_t position, uint64_t end, uint8_t data[16], lnd_mp4_header *header) {
    if (position > end || end - position < 8 || !lnd_mp4_read_at(io, position, data, 8)) return false;
    size_t bytes = lnd_mp4_u32(data) == 1 ? 16 : 8;
    if (bytes == 16 && (end - position < 16 || !lnd_mp4_read_at(io, position + 8, data + 8, 8))) return false;
    return lnd_mp4_box_bounds(data, bytes, end - position, header);
}

static bool lnd_mp4_parse_esds(const uint8_t *data, size_t size, lnd_mp4_track *track) {
    lnd_mp4_config config = {0};
    if (size < 4 || !lnd_mp4_descriptors(data + 4, size - 4, 0, &config) || !config.size || config.size > sizeof track->asc) return false;
    memcpy(track->asc, config.data, config.size);
    track->asc_len = (uint32_t)config.size;
    return true;
}

static bool lnd_mp4_parse_stsd(lnd_io *io, uint64_t start, uint64_t end, lnd_mp4_track *track) {
    uint8_t data[16];
    if (end - start < 8 || !lnd_mp4_read_at(io, start, data, 8)) return false;
    uint32_t entries = lnd_mp4_u32(data + 4);
    uint64_t position = start + 8;
    for (uint32_t entry = 0; entry < entries; entry++) {
        lnd_mp4_header header;
        if (!lnd_mp4_read_box(io, position, end, data, &header)) return false;
        uint64_t limit = position + header.size, body = position + header.bytes;
        if (header.type == LND_MP4_TAG('m', 'p', '4', 'a')) {
            uint8_t audio[28];
            if (limit - body < sizeof audio || !lnd_mp4_read_at(io, body, audio, sizeof audio)) return false;
            uint32_t version = lnd_rd_u16be(audio + 8);
            if (version > 2) return false;
            uint64_t child = body + 28 + (version == 1 ? 16 : version == 2 ? 36 : 0);
            if (child > limit) return false;
            track->has_mp4a = true;
            while (child < limit) {
                lnd_mp4_header config;
                if (!lnd_mp4_read_box(io, child, limit, data, &config)) return false;
                if (config.type == LND_MP4_TAG('e', 's', 'd', 's')) {
                    uint8_t payload[512];
                    uint64_t size = config.size - config.bytes;
                    if (size > sizeof payload || !lnd_mp4_read_at(io, child + config.bytes, payload, (size_t)size) ||
                        !lnd_mp4_parse_esds(payload, (size_t)size, track)) return false;
                }
                child += config.size;
            }
        }
        position = limit;
    }
    return position == end;
}

static bool lnd_mp4_read_table(lnd_io *io, uint64_t body, uint64_t end, uint32_t count, uint32_t width, lnd_mp4_table *table) {
    if (table->data || body > end || count > (1u << 24) || count > SIZE_MAX / width || (uint64_t)count * width > end - body) return false;
    size_t size = (size_t)count * width;
    uint8_t *data = lnd_alloc(size ? size : 1);
    if (!data || !lnd_mp4_read_at(io, body, data, size)) {
        lnd_free(data);
        return false;
    }
    return lnd_mp4_table_view(table, data, size, count, width);
}

static void lnd_mp4_track_free(lnd_mp4_track *track) {
    lnd_free((void *)track->chunks.data);
    lnd_free((void *)track->runs.data);
    lnd_free((void *)track->sizes.data);
}

static bool lnd_mp4_parse_boxes(lnd_io *io, uint64_t start, uint64_t end, lnd_mp4_track *track, unsigned depth) {
    if (depth >= 8) return false;
    for (uint64_t position = start; position < end;) {
        uint8_t data[24];
        lnd_mp4_header header;
        if (!lnd_mp4_read_box(io, position, end, data, &header)) return false;
        uint64_t body = position + header.bytes, limit = position + header.size;
        const uint8_t *tag = data + 4;
        if (lnd_tag_is(tag, "moov") || lnd_tag_is(tag, "mdia") || lnd_tag_is(tag, "minf") || lnd_tag_is(tag, "stbl") || lnd_tag_is(tag, "edts")) {
            if (!lnd_mp4_parse_boxes(io, body, limit, track, depth + 1)) return false;
        } else if (lnd_tag_is(tag, "trak")) {
            if (!track->has_mp4a) {
                lnd_mp4_track probe = {.movie_timescale = track->movie_timescale};
                bool valid = lnd_mp4_parse_boxes(io, body, limit, &probe, depth + 1);
                if (valid && probe.has_mp4a && probe.sample_count) {
                    lnd_mp4_track_free(track);
                    *track = probe;
                } else {
                    bool malformed_audio = !valid && probe.has_mp4a;
                    lnd_mp4_track_free(&probe);
                    if (malformed_audio) return false;
                }
            }
        } else if (lnd_tag_is(tag, "mdhd") || lnd_tag_is(tag, "mvhd")) {
            bool movie = lnd_tag_is(tag, "mvhd");
            if (limit - body < 24 || !lnd_mp4_read_at(io, body, data, 24) || data[0] > 1) return false;
            uint32_t timescale = lnd_mp4_u32(data + (data[0] ? 20 : 12));
            if (movie) track->movie_timescale = timescale;
            else track->timescale = timescale;
        } else if (lnd_tag_is(tag, "elst")) {
            if (limit - body < 8 || !lnd_mp4_read_at(io, body, data, 8) || data[0] > 1) return false;
            uint32_t count = lnd_mp4_u32(data + 4), width = data[0] ? 20 : 12;
            if (count > (limit - body - 8) / width) return false;
            for (uint32_t i = 0; i < count && i < 8; i++) {
                if (!lnd_mp4_read_at(io, body + 8 + (uint64_t)i * width, data, width)) return false;
                int64_t media_time = width == 20 ? (int64_t)lnd_mp4_u64(data + 8) : (int32_t)lnd_mp4_u32(data + 4);
                if (media_time < 0) continue;
                track->media_time = media_time;
                track->segment_duration = width == 20 ? lnd_mp4_u64(data) : lnd_mp4_u32(data);
                break;
            }
        } else if (lnd_tag_is(tag, "stsd")) {
            if (!lnd_mp4_parse_stsd(io, body, limit, track)) return false;
        } else if (lnd_tag_is(tag, "stsz")) {
            if (track->sample_count || limit - body < 12 || !lnd_mp4_read_at(io, body, data, 12)) return false;
            track->uniform_size = lnd_mp4_u32(data + 4);
            track->sample_count = lnd_mp4_u32(data + 8);
            if (!track->uniform_size && !lnd_mp4_read_table(io, body + 12, limit, track->sample_count, 4, &track->sizes)) return false;
        } else if (lnd_tag_is(tag, "stsc") || lnd_tag_is(tag, "stco") || lnd_tag_is(tag, "co64")) {
            bool runs = lnd_tag_is(tag, "stsc");
            uint32_t width = runs ? 12 : lnd_tag_is(tag, "co64") ? 8 : 4;
            if (limit - body < 8 || !lnd_mp4_read_at(io, body, data, 8) ||
                !lnd_mp4_read_table(io, body + 8, limit, lnd_mp4_u32(data + 4), width, runs ? &track->runs : &track->chunks)) return false;
        }
        position = limit;
    }
    return true;
}

static void lnd_aac_location(void *user, uint32_t index, uint64_t offset, uint32_t bytes) {
    lnd_aac_state *state = user;
    state->offsets[index] = offset;
    state->sizes[index] = bytes;
}

static bool lnd_mp4_build(lnd_io *io, lnd_aac_state *state) {
    lnd_mp4_track track = {0};
    uint64_t size = LND_IoGetSizeBytes(io);
    bool ok = lnd_mp4_parse_boxes(io, 0, size, &track, 0) && track.has_mp4a && track.asc_len && track.sample_count &&
              sizeof(uint64_t) <= SIZE_MAX / track.sample_count;
    if (ok) {
        state->offsets = lnd_alloc(sizeof(uint64_t) * track.sample_count);
        state->sizes = lnd_alloc(sizeof(uint32_t) * track.sample_count);
        ok = state->offsets && state->sizes &&
             lnd_mp4_locations(&track.chunks, &track.runs, &track.sizes, track.uniform_size, track.sample_count,
                               UINT32_MAX, lnd_aac_location, state) == LND_OK;
    }
    for (uint32_t i = 0; ok && i < track.sample_count; i++)
        if (state->offsets[i] > size || state->sizes[i] > size - state->offsets[i]) ok = false;
    if (ok) {
        UCHAR *config = track.asc;
        UINT length = track.asc_len;
        ok = aacDecoder_ConfigRaw(state->dec, &config, &length) == AAC_DEC_OK;
        state->count = track.sample_count;
        memcpy(state->asc, track.asc, track.asc_len);
        state->asc_size = track.asc_len;
        state->media_timescale = track.timescale;
        if (track.media_time > 0) state->edit_start = (uint64_t)track.media_time;
        if (track.segment_duration && track.movie_timescale && track.timescale) {
            uint64_t whole = track.segment_duration / track.movie_timescale;
            uint64_t fraction = (track.segment_duration % track.movie_timescale * track.timescale + track.movie_timescale / 2) / track.movie_timescale;
            if (whole > (UINT64_MAX - fraction) / track.timescale) ok = false;
            else state->edit_duration = whole * track.timescale + fraction;
        }
    }
    lnd_mp4_track_free(&track);
    return ok;
}

static bool lnd_adts_build(lnd_io *io, lnd_aac_state *s) {
    uint64_t size = LND_IoGetSizeBytes(io);
    uint64_t pos = 0;
    uint32_t cap = 0;
    while (pos + 7 <= size) {
        uint8_t h[7];
        if (!lnd_mp4_read_at(io, pos, h, 7)) break;
        if (h[0] != 0xFF || (h[1] & 0xF6) != 0xF0) {
            pos++;
            continue;
        }
        uint32_t length = ((uint32_t)(h[3] & 3) << 11) | ((uint32_t)h[4] << 3) | (h[5] >> 5);
        if (length < 7 || pos + length > size) break;
        if (s->count == cap) {
            if (cap > UINT32_MAX / 2) return false;
            uint32_t capacity = cap ? cap * 2 : 1024;
            if (sizeof(uint64_t) > SIZE_MAX / capacity) return false;
            uint64_t *offsets = lnd_realloc(s->offsets, sizeof(uint64_t) * capacity);
            if (!offsets) return false;
            s->offsets = offsets;
            uint32_t *sizes = lnd_realloc(s->sizes, sizeof(uint32_t) * capacity);
            if (!sizes) return false;
            s->sizes = sizes;
            cap = capacity;
        }
        s->offsets[s->count] = pos;
        s->sizes[s->count] = length;
        s->count++;
        pos += length;
    }
    return s->count > 0;
}

static int32_t lnd_aac_probe(LND_IO *io) {
    uint8_t h[12];
    if (LND_IoRead(io, h, sizeof h) != sizeof h) return 0;
    if (memcmp(h + 4, "ftyp", 4) == 0) return 80;
    if (h[0] == 0xFF && (h[1] & 0xF6) == 0xF0) return 80;
    return 0;
}

static bool lnd_aac_decode_next(lnd_aac_state *s) {
    for (;;) {
        uint32_t index;
        UINT flags;
        bool flushing = s->cur >= s->count;
        if (flushing) {
            if (!s->frame_size || (uint64_t)s->flushed * s->frame_size >= s->output_delay) return false;
            index = s->count + s->flushed++;
            flags = AACDEC_FLUSH;
        } else {
            index = s->cur++;
            uint32_t size = s->sizes[index];
            if (size > s->au_cap) {
                uint8_t *grown = lnd_realloc(s->au, size);
                if (!grown) return false;
                s->au = grown;
                s->au_cap = size;
            }
            if (!lnd_mp4_read_at(s->io, s->offsets[index], s->au, size)) return false;
            UCHAR *buf = s->au;
            UINT len = size, valid = size;
            if (aacDecoder_Fill(s->dec, &buf, &len, &valid) != AAC_DEC_OK) return false;
            flags = 0;
        }
        AAC_DECODER_ERROR err = aacDecoder_DecodeFrame(s->dec, s->pcm, LND_AAC_MAX_PCM, flags);
        if (err != AAC_DEC_OK) {
            if (flushing) return false;
            continue;
        }
        CStreamInfo *info = aacDecoder_GetStreamInfo(s->dec);
        if (!info || info->numChannels < 1 || info->frameSize < 1) continue;
        if (!s->sample_rate_hz) {
            s->sample_rate_hz = (uint32_t)info->sampleRate;
            s->channels = (uint32_t)info->numChannels;
            s->frame_size = (uint32_t)info->frameSize;
            s->preroll = info->extAot == AOT_NULL_OBJECT ? LND_AAC_PREROLL_LC : LND_AAC_PREROLL_SBR;
        }
        if ((uint32_t)info->numChannels != s->channels || (uint32_t)info->frameSize != s->frame_size) continue;
        s->output_delay = info->outputDelay;
        int64_t first = (int64_t)index * s->frame_size - (int64_t)s->output_delay;
        int64_t end = (int64_t)(s->end ? s->end : (uint64_t)s->count * s->frame_size);
        int64_t frames = (int64_t)info->frameSize;
        if (first + frames > end) frames = end - first;
        if (frames <= 0) return false;
        int64_t skip = first < (int64_t)s->target ? LND_MIN(frames, (int64_t)s->target - first) : 0;
        s->pending_frames = (size_t)frames;
        s->pending_offset = (size_t)skip;
        if (skip < frames) return true;
    }
}

static void lnd_aac_close(void *state);
static int32_t lnd_aac_seek(void *state, uint64_t frame);

static int32_t lnd_aac_decoder_init(lnd_aac_state *s) {
    HANDLE_AACDECODER dec = aacDecoder_Open(s->adts ? TT_MP4_ADTS : TT_MP4_RAW, 1);
    if (!dec) return LND_ERR_OUT_OF_MEMORY;
    aacDecoder_SetParam(dec, AAC_PCM_OUTPUT_CHANNEL_MAPPING, 1);
    aacDecoder_SetParam(dec, AAC_PCM_MAX_OUTPUT_CHANNELS, LND_MAX_CHANNELS < 8 ? LND_MAX_CHANNELS : 8);
    aacDecoder_SetParam(dec, AAC_PCM_LIMITER_ENABLE, 0);
    aacDecoder_SetParam(dec, AAC_CONCEAL_METHOD, 1);
    if (s->asc_size) {
        UCHAR *config = s->asc;
        UINT bytes = s->asc_size;
        if (aacDecoder_ConfigRaw(dec, &config, &bytes) != AAC_DEC_OK) {
            aacDecoder_Close(dec);
            return LND_ERR_FORMAT;
        }
    }
    if (s->dec) aacDecoder_Close(s->dec);
    s->dec = dec;
    return LND_OK;
}

static int32_t lnd_aac_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    uint8_t h[12];
    if (LND_IoRead(io, h, sizeof h) != sizeof h) return LND_ERR_FORMAT;
    lnd_aac_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->adts = memcmp(h + 4, "ftyp", 4) != 0;
    s->pcm = lnd_alloc(sizeof(INT_PCM) * LND_AAC_MAX_PCM);
    int32_t initialized = lnd_aac_decoder_init(s);
    if (!s->pcm || initialized != LND_OK) {
        lnd_aac_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    bool ok = s->adts ? lnd_adts_build(io, s) : lnd_mp4_build(io, s);
    if (!ok || !lnd_aac_decode_next(s) || !s->sample_rate_hz || s->channels > LND_MAX_CHANNELS) {
        lnd_aac_close(s);
        return LND_ERR_FORMAT;
    }
    uint64_t coded = (uint64_t)s->count * s->frame_size;
    if (s->media_timescale) {
        s->delay = (s->edit_start * s->sample_rate_hz + s->media_timescale / 2) / s->media_timescale;
        if (s->edit_duration) s->end = s->delay + (s->edit_duration * s->sample_rate_hz + s->media_timescale / 2) / s->media_timescale;
    }
    if (!s->end || s->end > coded) s->end = coded;
    s->total = s->end > s->delay ? s->end - s->delay : 0;
    if (!s->adts) {
        int32_t result = lnd_aac_seek(s, 0);
        if (result != LND_OK) {
            lnd_aac_close(s);
            return result;
        }
    }
    info->format = LND_FORMAT_S16;
    info->channels = s->channels;
    info->sample_rate_hz = s->sample_rate_hz;
    info->length_frames = s->total;
    info->seekable = true;
    *state = s;
    return LND_OK;
}

static uint64_t lnd_aac_read(void *state, void *dst, uint64_t frames) {
    lnd_aac_state *s = state;
    uint8_t *out = dst;
    size_t frame_bytes = s->channels * sizeof(INT_PCM);
    uint64_t total = 0;
    while (total < frames) {
        if (s->pending_offset >= s->pending_frames && !lnd_aac_decode_next(s)) break;
        size_t avail = s->pending_frames - s->pending_offset;
        size_t n = (size_t)LND_MIN((uint64_t)avail, frames - total);
        memcpy(out + total * frame_bytes, (uint8_t *)s->pcm + s->pending_offset * frame_bytes, n * frame_bytes);
        s->pending_offset += n;
        total += n;
    }
    s->pos += total;
    return total;
}

static int32_t lnd_aac_seek(void *state, uint64_t frame) {
    lnd_aac_state *s = state;
    int32_t result = lnd_aac_decoder_init(s);
    if (result != LND_OK) return result;
    if (s->total && frame > s->total) frame = s->total;
    uint64_t absolute = frame + s->delay;
    uint64_t au = (absolute + s->output_delay) / s->frame_size;
    s->cur = au > s->preroll ? (uint32_t)(au - s->preroll) : 0;
    s->target = absolute;
    s->pending_frames = s->pending_offset = 0;
    s->flushed = 0;

    s->pos = frame;
    return LND_OK;
}

static void lnd_aac_close(void *state) {
    lnd_aac_state *s = state;
    if (s->dec) aacDecoder_Close(s->dec);
    lnd_free(s->offsets);
    lnd_free(s->sizes);
    lnd_free(s->au);
    lnd_free(s->pcm);
    lnd_free(s);
}

#if LND_MODULE_DECODE
#include "stream.h"
#endif

const LND_CODEC lnd_codec_aac = {
    .name = "aac",
    .stream_identifiers = "aac;mp4a.40.*",
    .extensions = "m4a;aac;mp4;m4b;adts",
    .probe = lnd_aac_probe,
    .open = lnd_aac_open,
    .read = lnd_aac_read,
    .seek = lnd_aac_seek,
    .close = lnd_aac_close,
#if LND_MODULE_DECODE
    .stream = &lnd_aac_stream_ops,
#endif
};
