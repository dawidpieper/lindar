#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include "src/alloc.h"
#include "ffmpeg.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_codecs.h"

#include <string.h>

#define LND_FF_IO_BUFFER 65536

typedef struct lnd_ff_state {
    lnd_io *io;
    AVIOContext *avio;
    AVFormatContext *fmt;
    AVCodecContext *codec;
    SwrContext *swr;
    AVChannelLayout layout;
    enum AVSampleFormat format;
    AVPacket *pkt;
    AVFrame *frame;
    int stream;
    uint32_t channels;
    uint32_t sample_rate_hz;
    uint64_t total;
    uint64_t pos;
    uint64_t skip_until;
    int64_t start_time;
    float *pending;
    size_t pending_cap;
    size_t pending_frames;
    size_t pending_offset;
    bool eof;
    bool draining;
    bool failed;
} lnd_ff_state;

static int lnd_ff_read_cb(void *opaque, uint8_t *buf, int size) {
    int64_t n = LND_IoRead(opaque, buf, (size_t)size);
    return n < 0 ? AVERROR(EIO) : n ? (int)n : AVERROR_EOF;
}

static int64_t lnd_ff_seek_cb(void *opaque, int64_t offset, int whence) {
    lnd_io *io = opaque;
    if (whence & AVSEEK_SIZE) return (int64_t)LND_IoGetSizeBytes(io);
    whence &= ~AVSEEK_FORCE;
    int64_t base = whence == SEEK_SET ? 0 : (whence == SEEK_CUR ? (int64_t)LND_IoGetPositionBytes(io) : (int64_t)LND_IoGetSizeBytes(io));
    int64_t target = base + offset;
    if (target < 0 || LND_IoSeekBytes(io, (uint64_t)target) != LND_OK) return AVERROR(EINVAL);
    return target;
}

static int32_t lnd_ff_probe(LND_IO *io) {
    lnd_ff_init();
    size_t cap = 8192;
    uint8_t *buf = lnd_alloc(cap + AVPROBE_PADDING_SIZE);
    if (!buf) return 0;
    int64_t n = LND_IoRead(io, buf, cap);
    if (n < 0) {
        lnd_free(buf);
        return 0;
    }
    memset(buf + n, 0, AVPROBE_PADDING_SIZE);
    AVProbeData pd = {.filename = "", .buf = buf, .buf_size = (int)n};
    const AVInputFormat *found = av_probe_input_format(&pd, 1);
    lnd_free(buf);
    return found ? 20 : 0;
}

static uint64_t lnd_ff_frames_from_ts(const lnd_ff_state *s, int64_t ts) {
    AVRational tb = s->fmt->streams[s->stream]->time_base;
    int64_t rel = ts - s->start_time;
    if (rel < 0) rel = 0;
    return (uint64_t)((double)rel * tb.num / tb.den * s->sample_rate_hz + 0.5);
}

static void lnd_ff_close(void *state);

static int32_t lnd_ff_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    lnd_ff_init();
    lnd_ff_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->stream = -1;
    LND_IoSeekBytes(io, 0);
    uint8_t *buffer = av_malloc(LND_FF_IO_BUFFER);
    if (!buffer) {
        lnd_free(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    s->avio = avio_alloc_context(buffer, LND_FF_IO_BUFFER, 0, io, lnd_ff_read_cb, nullptr, lnd_ff_seek_cb);
    if (!s->avio) {
        av_free(buffer);
        lnd_free(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    s->fmt = avformat_alloc_context();
    if (!s->fmt) {
        lnd_ff_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    s->fmt->pb = s->avio;
    if (avformat_open_input(&s->fmt, "", nullptr, nullptr) < 0) {
        s->fmt = nullptr;
        lnd_ff_close(s);
        return LND_ERR_FORMAT;
    }
    if (avformat_find_stream_info(s->fmt, nullptr) < 0) {
        lnd_ff_close(s);
        return LND_ERR_FORMAT;
    }
    const AVCodec *decoder = nullptr;
    s->stream = av_find_best_stream(s->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (s->stream < 0 || !decoder) {
        lnd_ff_close(s);
        return LND_ERR_FORMAT;
    }
    AVStream *st = s->fmt->streams[s->stream];
    s->codec = avcodec_alloc_context3(decoder);
    if (!s->codec || avcodec_parameters_to_context(s->codec, st->codecpar) < 0 || avcodec_open2(s->codec, decoder, nullptr) < 0) {
        lnd_ff_close(s);
        return LND_ERR_FORMAT;
    }
    s->channels = (uint32_t)s->codec->ch_layout.nb_channels;
    s->sample_rate_hz = (uint32_t)s->codec->sample_rate;
    if (s->channels < 1 || s->channels > LND_MAX_CHANNELS || s->sample_rate_hz < 1) {
        lnd_ff_close(s);
        return LND_ERR_FORMAT;
    }
    s->format = s->codec->sample_fmt;
    if (av_channel_layout_copy(&s->layout, &s->codec->ch_layout) < 0) {
        lnd_ff_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    if (swr_alloc_set_opts2(&s->swr, &s->codec->ch_layout, AV_SAMPLE_FMT_FLT, (int)s->sample_rate_hz, &s->codec->ch_layout, s->codec->sample_fmt, (int)s->sample_rate_hz, 0,
                            nullptr) < 0 ||
        swr_init(s->swr) < 0) {
        lnd_ff_close(s);
        return LND_ERR_FORMAT;
    }
    s->pkt = av_packet_alloc();
    s->frame = av_frame_alloc();
    if (!s->pkt || !s->frame) {
        lnd_ff_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    s->start_time = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    if (st->duration != AV_NOPTS_VALUE && st->duration > 0) {
        s->total = (uint64_t)((double)st->duration * st->time_base.num / st->time_base.den * s->sample_rate_hz + 0.5);
    } else if (s->fmt->duration != AV_NOPTS_VALUE && s->fmt->duration > 0) {
        s->total = (uint64_t)((double)s->fmt->duration / AV_TIME_BASE * s->sample_rate_hz + 0.5);
    }
    info->format = LND_FORMAT_F32;
    info->channels = s->channels;
    info->sample_rate_hz = s->sample_rate_hz;
    info->length_frames = s->total;
    info->length_estimated = s->fmt->duration_estimation_method == AVFMT_DURATION_FROM_BITRATE;
    info->seekable = (s->fmt->ctx_flags & AVFMTCTX_UNSEEKABLE) == 0;
    *state = s;
    return LND_OK;
}

static bool lnd_ff_store(lnd_ff_state *s) {
    if (s->frame->nb_samples < 0 || s->frame->sample_rate != (int)s->sample_rate_hz || s->frame->format != s->format ||
        av_channel_layout_compare(&s->layout, &s->frame->ch_layout) ||
        (size_t)s->frame->nb_samples > SIZE_MAX / (s->channels * sizeof(float))) {
        s->failed = true;
        return false;
    }
    size_t frames = (size_t)s->frame->nb_samples;
    if (!frames) return false;
    if (frames > s->pending_cap) {
        float *grown = lnd_realloc(s->pending, frames * s->channels * sizeof(float));
        if (!grown) {
            s->failed = true;
            return false;
        }
        s->pending = grown;
        s->pending_cap = frames;
    }
    uint8_t *out = (uint8_t *)s->pending;
    int got = swr_convert(s->swr, &out, (int)frames, (const uint8_t **)s->frame->extended_data, (int)frames);
    if (got < 0 || (size_t)got > frames) s->failed = true;
    if (s->failed || !got) return false;
    size_t skip = 0;
    if (s->skip_until) {
        int64_t ts = s->frame->best_effort_timestamp != AV_NOPTS_VALUE ? s->frame->best_effort_timestamp : s->frame->pts;
        if (ts != AV_NOPTS_VALUE) {
            uint64_t first = lnd_ff_frames_from_ts(s, ts);
            if (first < s->skip_until) skip = (size_t)LND_MIN((uint64_t)got, s->skip_until - first);
        }
        if (skip < (size_t)got) s->skip_until = 0;
    }
    s->pending_frames = (size_t)got;
    s->pending_offset = skip;
    return skip < (size_t)got;
}

static bool lnd_ff_fill(lnd_ff_state *s) {
    while (!s->eof && !s->failed) {
        int r = avcodec_receive_frame(s->codec, s->frame);
        if (r == 0) {
            bool stored = lnd_ff_store(s);
            av_frame_unref(s->frame);
            if (stored) return true;
            continue;
        }
        if (r == AVERROR_EOF) {
            s->eof = true;
            return false;
        }
        if (r != AVERROR(EAGAIN)) {
            s->eof = true;
            return false;
        }
        if (s->draining) continue;
        r = av_read_frame(s->fmt, s->pkt);
        if (r < 0) {
            s->draining = true;
            avcodec_send_packet(s->codec, nullptr);
            continue;
        }
        if (s->pkt->stream_index == s->stream) avcodec_send_packet(s->codec, s->pkt);
        av_packet_unref(s->pkt);
    }
    return false;
}

static uint64_t lnd_ff_read(void *state, void *dst, uint64_t frames) {
    lnd_ff_state *s = state;
    if (s->failed) return LND_CODEC_READ_ERROR;
    float *out = dst;
    uint64_t total = 0;
    while (total < frames) {
        if (s->pending_offset >= s->pending_frames && !lnd_ff_fill(s)) break;
        size_t avail = s->pending_frames - s->pending_offset;
        size_t n = (size_t)LND_MIN((uint64_t)avail, frames - total);
        memcpy(out + total * s->channels, s->pending + s->pending_offset * s->channels, n * s->channels * sizeof(float));
        s->pending_offset += n;
        total += n;
    }
    s->pos += total;
    return s->failed ? LND_CODEC_READ_ERROR : total;
}

static int32_t lnd_ff_seek(void *state, uint64_t frame) {
    lnd_ff_state *s = state;
    if (s->total && frame > s->total) frame = s->total;
    AVRational tb = s->fmt->streams[s->stream]->time_base;
    double offset = (double)frame / s->sample_rate_hz * tb.den / tb.num;
    if (!(offset >= 0 && offset < 0x1p63)) return LND_ERR_INVALID_ARG;
    int64_t delta = (int64_t)offset;
    if (s->start_time > INT64_MAX - delta) return LND_ERR_INVALID_ARG;
    int64_t ts = s->start_time + delta;
    if (av_seek_frame(s->fmt, s->stream, ts, AVSEEK_FLAG_BACKWARD) < 0) return LND_ERR_UNSUPPORTED;
    avcodec_flush_buffers(s->codec);
    s->pending_frames = s->pending_offset = 0;
    s->eof = false;
    s->failed = false;
    s->draining = false;
    s->skip_until = frame;
    s->pos = frame;
    return LND_OK;
}

static void lnd_ff_close(void *state) {
    lnd_ff_state *s = state;
    if (s->swr) swr_free(&s->swr);
    av_channel_layout_uninit(&s->layout);
    if (s->codec) avcodec_free_context(&s->codec);
    if (s->frame) av_frame_free(&s->frame);
    if (s->pkt) av_packet_free(&s->pkt);
    if (s->fmt) avformat_close_input(&s->fmt);
    if (s->avio) {
        av_freep(&s->avio->buffer);
        avio_context_free(&s->avio);
    }
    lnd_free(s->pending);
    lnd_free(s);
}

#if LND_MODULE_DECODE
#include "stream.h"
#endif

const LND_CODEC lnd_codec_ffmpeg = {
#if LND_MODULE_DECODE
    .stream = &lnd_ff_stream_ops,
#endif
    .name = "ffmpeg",
    .flags = LND_CODEC_FLAG_FALLBACK,
    .stream_identifiers = "aac;flac;ac3;eac3;ac-3;ec-3;alac;mp4a.40.*;mp4a.69;mp4a.6B;mp3;opus;fLaC;vorbis",
    .extensions = "",
    .probe = lnd_ff_probe,
    .open = lnd_ff_open,
    .read = lnd_ff_read,
    .seek = lnd_ff_seek,
    .close = lnd_ff_close,
};
