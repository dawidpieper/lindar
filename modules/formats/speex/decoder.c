#include "src/alloc.h"
#include "src/platform.h"
#include "lindar_codecs.h"
#include "io/io.h"
#include "formats/decode/metadata.h"
#include <string.h>
#include <limits.h>
#include <speex/speex.h>
#include <speex/speex_header.h>
#include <speex/speex_stereo.h>
#include <speex/speex_callbacks.h>
#include "formats/ogg/stream.h"
typedef struct lnd_speex_stream {
    lnd_ogg_stream ogg;
    lnd_decode_tags tags;
    void *decoder;
    SpeexBits bits;
    SpeexStereoState stereo;
    SpeexHeader header;
    int16_t *pcm;
    uint32_t headers;
    uint32_t frame_size;
    uint32_t lookahead;
    uint32_t page_packet;
    int64_t last_granule;
    int64_t page_skip;
    bool scan;
    uint64_t position;
    uint64_t total;
} lnd_speex_stream;
static void *stream_create(void) {
    lnd_speex_stream *s = lnd_alloc_zero(sizeof *s);
    if (s) {
        lnd_ogg_stream_init(&s->ogg);
        speex_bits_init(&s->bits);
    }

    return s;
}

static void stream_close(void *state) {
    lnd_speex_stream *s = state;
    if (!s) return;
    if (s->decoder) speex_decoder_destroy(s->decoder);
    speex_bits_destroy(&s->bits);
    lnd_ogg_stream_clear(&s->ogg);
    lnd_decode_tags_clear(&s->tags);
    lnd_free(s->pcm);
    lnd_free(s);
}

static int32_t header(lnd_speex_stream *s, const ogg_packet *packet) {
    if (packet->bytes < 80 || packet->bytes > INT_MAX) return LND_ERR_FORMAT;
    SpeexHeader *h = speex_packet_to_header((char *)packet->packet, (int)packet->bytes);
    if (!h) return LND_ERR_FORMAT;
    s->header = *h;
    speex_header_free(h);
    h = &s->header;
    const SpeexMode *mode = h->mode >= 0 && h->mode <= 2 ? speex_lib_get_mode(h->mode) : nullptr;
    if (!mode || h->mode_bitstream_version != mode->bitstream_version || h->speex_version_id > 1 || h->nb_channels < 1 || h->nb_channels > 2 || h->rate < 1 ||
        h->rate > 768000 || h->frames_per_packet < 1 || h->frames_per_packet > 64 || h->extra_headers < 0 || h->extra_headers > 64)
        return LND_ERR_FORMAT;
    s->decoder = speex_decoder_init(mode);
    if (!s->decoder) return LND_ERR_OUT_OF_MEMORY;
    int frame, delay, enhance = 1;
    speex_decoder_ctl(s->decoder, SPEEX_GET_FRAME_SIZE, &frame);
    speex_decoder_ctl(s->decoder, SPEEX_GET_LOOKAHEAD, &delay);
    speex_decoder_ctl(s->decoder, SPEEX_SET_SAMPLING_RATE, &h->rate);
    speex_decoder_ctl(s->decoder, SPEEX_SET_ENH, &enhance);
    if (frame < 1 || frame > 640 || h->frame_size != frame) return LND_ERR_FORMAT;
    s->frame_size = (uint32_t)frame;
    s->lookahead = (uint32_t)LND_MAX(delay, 0);
    s->stereo = (SpeexStereoState)SPEEX_STEREO_STATE_INIT;
    if (h->nb_channels == 2) {
        SpeexCallback callback = {.callback_id = SPEEX_INBAND_STEREO, .func = speex_std_stereo_request_handler, .data = &s->stereo};
        speex_decoder_ctl(s->decoder, SPEEX_SET_HANDLER, &callback);
    }

    s->pcm = lnd_alloc((size_t)frame * h->frames_per_packet * h->nb_channels * sizeof(int16_t));
    return s->pcm ? LND_OK : LND_ERR_OUT_OF_MEMORY;
}

static int32_t stream_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    lnd_speex_stream *s = state;
    *pcm = (LND_PCM){0};
    ogg_packet packet;
    int r = s->ogg.opened ? ogg_stream_packetout(&s->ogg.stream, &packet) : 0;
    if (r < 0) return LND_ERR_FORMAT;
    if (!r) {
        r = lnd_ogg_stream_feed(&s->ogg, data, bytes, end, used);
        if (s->ogg.chain) {
            if (s->decoder) speex_decoder_destroy(s->decoder);
            s->decoder = nullptr;
            lnd_free(s->pcm);
            s->pcm = nullptr;
            s->headers = 0;
            s->position = 0;
            s->last_granule = 0;
            s->ogg.chain = false;
        }

        if (r == LND_SOURCE_READY) {
            s->page_packet = 0;
            s->page_skip = 0;
            if (s->ogg.granule > 0 && s->frame_size) {
                s->page_skip = (int64_t)s->frame_size * s->header.frames_per_packet * s->ogg.packets - (s->ogg.granule - s->last_granule);
                if (s->ogg.page_eos) s->page_skip = -s->page_skip;
            }

            s->last_granule = s->ogg.granule;
        }

        if (r == LND_SOURCE_EOF) {
            info->length_frames = s->total;
            info->length_known = true;
        }

        return r;
    }

    if (!s->headers) {
        r = header(s, &packet);
        if (r) return r;
        *info = (LND_CODEC_INFO){.channels = (uint32_t)s->header.nb_channels, .sample_rate_hz = (uint32_t)s->header.rate, .format = LND_FORMAT_S16};
        ++s->headers;
        return LND_SOURCE_READY;
    }

    if (s->headers <= 1u + (uint32_t)s->header.extra_headers) {
        if (s->headers == 1) lnd_decode_tags_read(&s->tags, packet.packet, (size_t)packet.bytes, LND_METADATA_FLAC);
        ++s->headers;
        return LND_SOURCE_READY;
    }

    if (packet.bytes > INT_MAX) return LND_ERR_FORMAT;
    if (!s->scan) speex_bits_read_from(&s->bits, (char *)packet.packet, (int)packet.bytes);
    uint32_t frames = s->frame_size * (uint32_t)s->header.frames_per_packet;
    for (int i = 0; !s->scan && i < s->header.frames_per_packet; ++i) {
        int16_t *out = s->pcm + (size_t)i * s->frame_size * info->channels;
        if (speex_decode_int(s->decoder, &s->bits, out) < 0 || speex_bits_remaining(&s->bits) < 0) return LND_ERR_FORMAT;
        if (info->channels == 2) speex_decode_stereo_int(out, (int)s->frame_size, &s->stereo);
    }

    ++s->page_packet;
    uint32_t skip = s->page_packet == 1 && s->page_skip > 0 ? (uint32_t)LND_MIN(frames, s->page_skip + s->lookahead) : 0;
    uint32_t end_frame = frames;
    if (s->page_packet == s->ogg.packets && s->page_skip < 0)
        end_frame = (uint32_t)LND_MAX(0, LND_MIN((int64_t)frames, (int64_t)frames + s->page_skip + s->lookahead));
    uint32_t take = end_frame > skip ? end_frame - skip : 0;
    *pcm = (LND_PCM){.data = s->pcm + (size_t)skip * info->channels, .frames = take, .channels = info->channels, .format = LND_FORMAT_S16};
    s->position += take;
    s->total += take;
    return LND_SOURCE_READY;
}

static int32_t stream_metadata(void *state, LND_METADATA *m, uint64_t *revision) {
    return lnd_decode_tags_get(&((lnd_speex_stream *)state)->tags, m, revision);
}

static const LND_CODEC_STREAM stream_ops = {.create = stream_create, .step = stream_step, .close = stream_close, .metadata = stream_metadata};
typedef struct lnd_speex_file {
    LND_IO *io;
    lnd_speex_stream *stream;
    LND_CODEC_INFO info;
    uint8_t input[4096];
    size_t offset;
    size_t bytes;
    LND_PCM pending;
    size_t pcm_offset;
    bool end;
    int32_t status;
} lnd_speex_file;
static int32_t file_step(lnd_speex_file *s) {
    if (s->status < 0) return s->status;
    LND_CODEC_INFO info = s->info;
    size_t used = 0;
    int32_t r = stream_step(s->stream, s->input + s->offset, s->bytes, s->end, &used, &s->pending, &info);
    if (r >= 0 && s->info.channels && (info.channels != s->info.channels || info.sample_rate_hz != s->info.sample_rate_hz)) r = LND_ERR_UNSUPPORTED;
    if (r < 0) {
        s->pending = (LND_PCM){0};
        s->pcm_offset = 0;
        return s->status = r;
    }
    s->info = info;
    s->offset += used;
    s->bytes -= used;
    s->pcm_offset = 0;
    if (r == LND_SOURCE_WAITING && !s->bytes && !s->end) {
        int64_t n = LND_IoReadSome(s->io, s->input, sizeof s->input);
        if (n < 0 && n != LND_READ_EOF) return s->status = (int32_t)n;
        s->bytes = n > 0 ? (size_t)n : 0;
        s->offset = 0;
        s->end = n <= 0;
        return LND_SOURCE_READY;
    }

    return r;
}

static int32_t probe(LND_IO *io) {
    uint8_t h[36];
    if (LND_IoRead(io, h, 27) != 27 || memcmp(h, "OggS", 4)) return 0;
    if (LND_IoSeekBytes(io, 27u + h[26]) || LND_IoRead(io, h, 8) != 8) return 0;
    return !memcmp(h, "Speex   ", 8) ? 100 : 0;
}

static void file_close(void *state) {
    lnd_speex_file *s = state;
    stream_close(s->stream);
    lnd_free(s);
}

static int32_t file_length(lnd_speex_file *s) {
    uint64_t saved = LND_IoGetPositionBytes(s->io);
    int32_t r = LND_IoSeekBytes(s->io, 0);
    if (r) return r;
    lnd_speex_file scan = {.io = s->io, .stream = stream_create()};
    if (!scan.stream) return LND_ERR_OUT_OF_MEMORY;
    scan.stream->scan = true;
    do {
        r = file_step(&scan);
    } while (r >= 0 && r != LND_SOURCE_EOF);
    if (r == LND_SOURCE_EOF) {
        s->info.length_frames = scan.stream->total;
        s->info.length_known = true;
        r = LND_OK;
    }

    stream_close(scan.stream);
    int32_t restore = LND_IoSeekBytes(s->io, saved);
    return r ? r : restore;
}

static int32_t file_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    lnd_speex_file *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->stream = stream_create();
    if (!s->stream) {
        file_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }

    int32_t r = LND_IoSeekBytes(io, 0);
    while (!r && !s->info.sample_rate_hz) {
        int32_t result = file_step(s);
        if (result < 0 || result == LND_SOURCE_EOF) r = result < 0 ? result : LND_ERR_FORMAT;
    }

    if (r) {
        file_close(s);
        return r;
    }

    if (LND_IoCanSeek(io) && (r = file_length(s))) {
        file_close(s);
        return r;
    }

    *info = s->info;
    info->seekable = LND_IoCanSeek(io);
    *state = s;
    return LND_OK;
}

static uint64_t file_read(void *state, void *dst, uint64_t frames) {
    lnd_speex_file *s = state;
    uint64_t done = 0;
    while (done < frames) {
        size_t take = (size_t)LND_MIN(frames - done, s->pending.frames - s->pcm_offset);
        if (take) {
            size_t width = s->info.channels * sizeof(int16_t);
            memcpy((uint8_t *)dst + done * width, (uint8_t *)s->pending.data + s->pcm_offset * width, take * width);
            s->pcm_offset += take;
            done += take;
        } else {
            int32_t r = file_step(s);
            if (r < 0) return LND_CODEC_READ_ERROR;
            if (r == LND_SOURCE_EOF) break;
        }
    }

    return done;
}

static int32_t file_seek(void *state, uint64_t frame) {
    lnd_speex_file *s = state;
    int32_t r = LND_IoSeekBytes(s->io, 0);
    if (r) return r;
    lnd_speex_stream *stream = stream_create();
    if (!stream) return LND_ERR_OUT_OF_MEMORY;
    stream_close(s->stream);
    s->stream = stream;
    s->offset = s->bytes = s->pcm_offset = 0;
    s->pending = (LND_PCM){0};
    s->end = false;
    s->status = LND_OK;
    int16_t scratch[1024];
    while (frame) {
        uint64_t n = file_read(s, scratch, LND_MIN(frame, 512));
        if (!n || n == LND_CODEC_READ_ERROR) return LND_ERR_FORMAT;
        frame -= n;
    }

    return LND_OK;
}

const LND_CODEC lnd_codec_speex = {.name = "speex",
    .stream_identifiers = "speex",
                                   .extensions = "spx;ogg;oga",
                                   .probe = probe,
                                   .open = file_open,
                                   .read = file_read,
                                   .seek = file_seek,
                                   .close = file_close,
                                   .stream = &stream_ops};
