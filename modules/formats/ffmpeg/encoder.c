#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

#include "ffmpeg.h"
#include "src/alloc.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_output.h"
#include "io/output/output.h"

#include <stdio.h>
#include <string.h>

#define LND_FFE_IO_BUFFER 32768

typedef struct lnd_ff_enc {
    AVFormatContext *fmt;
    AVIOContext *avio;
    AVCodecContext *enc;
    AVStream *st;
    SwrContext *swr;
    AVAudioFifo *fifo;
    AVFrame *frame;
    AVPacket *pkt;
    lnd_io *io;
    uint32_t channels;
    uint32_t in_rate;
    uint32_t frame_size;
    int64_t pts;
    uint8_t **conv;
    int conv_frames;
    bool header;
} lnd_ff_enc;

static int lnd_ffe_write_cb(void *opaque, const uint8_t *buf, int size) {
    lnd_ff_enc *s = opaque;
    size_t w = LND_IoWrite(s->io, buf, (size_t)size);
    return w == (size_t)size ? size : AVERROR(EIO);
}

static int64_t lnd_ffe_seek_cb(void *opaque, int64_t offset, int whence) {
    lnd_ff_enc *s = opaque;
    if (whence == AVSEEK_SIZE) return (int64_t)LND_IoGetSizeBytes(s->io);
    int64_t target = offset;
    if (whence == SEEK_CUR) target += (int64_t)LND_IoGetPositionBytes(s->io);
    else if (whence == SEEK_END) target += (int64_t)LND_IoGetSizeBytes(s->io);
    else if (whence != SEEK_SET) return AVERROR(EINVAL);
    if (target < 0 || LND_IoSeekBytes(s->io, (uint64_t)target) != LND_OK) return AVERROR(EIO);
    return target;
}

static enum AVSampleFormat lnd_ffe_pick_format(const AVCodecContext *ctx, const AVCodec *codec) {
    static const enum AVSampleFormat prefer[] = {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S16,
                                                 AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_DBL};
    const enum AVSampleFormat *fmts = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void **)&fmts, &count) < 0 || !fmts || count <= 0)
        return AV_SAMPLE_FMT_FLTP;
    for (size_t p = 0; p < LND_COUNTOF(prefer); p++) {
        for (int i = 0; i < count; i++) {
            if (fmts[i] == prefer[p]) return prefer[p];
        }
    }
    return fmts[0];
}

static int lnd_ffe_pick_rate(const AVCodecContext *ctx, const AVCodec *codec, uint32_t wanted) {
    const int *rates = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0, (const void **)&rates, &count) < 0 || !rates || count <= 0) return (int)wanted;
    int best = rates[0];
    for (int i = 0; i < count; i++) {
        int64_t d = rates[i] > (int)wanted ? rates[i] - (int)wanted : (int)wanted - rates[i];
        int64_t bd = best > (int)wanted ? best - (int)wanted : (int)wanted - best;
        if (d < bd) best = rates[i];
    }
    return best;
}

static int32_t lnd_ffe_close(void *state);

static int32_t lnd_ffe_drain(lnd_ff_enc *s) {
    for (;;) {
        int r = avcodec_receive_packet(s->enc, s->pkt);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return LND_OK;
        if (r < 0) return LND_ERR_IO;
        s->pkt->stream_index = s->st->index;
        av_packet_rescale_ts(s->pkt, s->enc->time_base, s->st->time_base);
        r = av_interleaved_write_frame(s->fmt, s->pkt);
        av_packet_unref(s->pkt);
        if (r < 0) return LND_ERR_IO;
    }
}

static int32_t lnd_ffe_encode(lnd_ff_enc *s, int nb, bool pad_to) {
    int total = pad_to ? (int)s->frame_size : nb;
    s->frame->nb_samples = total;
    s->frame->format = s->enc->sample_fmt;
    s->frame->sample_rate = s->enc->sample_rate;
    if (av_channel_layout_copy(&s->frame->ch_layout, &s->enc->ch_layout) < 0 || av_frame_get_buffer(s->frame, 0) < 0) return LND_ERR_OUT_OF_MEMORY;
    if (nb > 0 && av_audio_fifo_read(s->fifo, (void **)s->frame->data, nb) < nb) {
        av_frame_unref(s->frame);
        return LND_ERR_IO;
    }
    if (total > nb) av_samples_set_silence(s->frame->data, nb, total - nb, s->enc->ch_layout.nb_channels, s->enc->sample_fmt);
    s->frame->pts = s->pts;
    s->pts += total;
    int r = avcodec_send_frame(s->enc, s->frame);
    av_frame_unref(s->frame);
    if (r < 0) return LND_ERR_IO;
    return lnd_ffe_drain(s);
}

static int32_t lnd_ffe_open(LND_IO *io, const LND_ENCODER_PARAMS *params, const char *extension, void **state) {
    lnd_ff_init();
    char fname[64] = "";
    char opt[64];
    const AVOutputFormat *ofmt = nullptr;
    if (lnd_encoder_option(params->options, "format", opt, sizeof opt)) ofmt = av_guess_format(opt, nullptr, nullptr);
    if (extension) snprintf(fname, sizeof fname, "out.%s", extension);
    if (!ofmt && fname[0]) ofmt = av_guess_format(nullptr, fname, nullptr);
    if (!ofmt) return LND_ERR_UNSUPPORTED;
    const AVCodec *codec = nullptr;
    if (lnd_encoder_option(params->options, "codec", opt, sizeof opt)) codec = avcodec_find_encoder_by_name(opt);
    if (!codec) {
        enum AVCodecID id = av_guess_codec(ofmt, nullptr, fname[0] ? fname : nullptr, nullptr, AVMEDIA_TYPE_AUDIO);
        if (id != AV_CODEC_ID_NONE) codec = avcodec_find_encoder(id);
    }
    if (!codec || codec->type != AVMEDIA_TYPE_AUDIO) return LND_ERR_UNSUPPORTED;
    lnd_ff_enc *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->channels = params->channels;
    s->in_rate = params->sample_rate_hz;
    if (avformat_alloc_output_context2(&s->fmt, ofmt, nullptr, fname[0] ? fname : nullptr) < 0 || !s->fmt) {
        lnd_ffe_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    s->enc = avcodec_alloc_context3(codec);
    if (!s->enc) {
        lnd_ffe_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    av_channel_layout_default(&s->enc->ch_layout, (int)params->channels);
    s->enc->sample_rate = lnd_ffe_pick_rate(s->enc, codec, params->sample_rate_hz);
    s->enc->sample_fmt = lnd_ffe_pick_format(s->enc, codec);
    s->enc->time_base = (AVRational){1, s->enc->sample_rate};
    if (params->bitrate_kbps) s->enc->bit_rate = (int64_t)params->bitrate_kbps * 1000;
    if (params->quality && !params->bitrate_kbps) {
        s->enc->flags |= AV_CODEC_FLAG_QSCALE;
        s->enc->global_quality = FF_QP2LAMBDA * (int)(1 + lnd_encoder_level(params, 4, 9));
    }
    if (ofmt->flags & AVFMT_GLOBALHEADER) s->enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(s->enc, codec, nullptr) < 0) {
        lnd_ffe_close(s);
        return LND_ERR_UNSUPPORTED;
    }
    s->st = avformat_new_stream(s->fmt, nullptr);
    if (!s->st || avcodec_parameters_from_context(s->st->codecpar, s->enc) < 0) {
        lnd_ffe_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    s->st->time_base = s->enc->time_base;
    uint8_t *buffer = av_malloc(LND_FFE_IO_BUFFER);
    if (!buffer) {
        lnd_ffe_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    s->avio = avio_alloc_context(buffer, LND_FFE_IO_BUFFER, 1, s, nullptr, lnd_ffe_write_cb, LND_IoCanSeek(io) ? lnd_ffe_seek_cb : nullptr);
    if (!s->avio) {
        av_free(buffer);
        lnd_ffe_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    s->fmt->pb = s->avio;
    if (avformat_write_header(s->fmt, nullptr) < 0) {
        lnd_ffe_close(s);
        return LND_ERR_IO;
    }
    s->header = true;
    AVChannelLayout in_layout;
    av_channel_layout_default(&in_layout, (int)params->channels);
    int r =
        swr_alloc_set_opts2(&s->swr, &s->enc->ch_layout, s->enc->sample_fmt, s->enc->sample_rate, &in_layout, AV_SAMPLE_FMT_FLT, (int)params->sample_rate_hz, 0, nullptr);
    av_channel_layout_uninit(&in_layout);
    if (r < 0 || swr_init(s->swr) < 0) {
        lnd_ffe_close(s);
        return LND_ERR_UNSUPPORTED;
    }
    s->frame_size = s->enc->frame_size > 0 ? (uint32_t)s->enc->frame_size : 1024;
    s->fifo = av_audio_fifo_alloc(s->enc->sample_fmt, s->enc->ch_layout.nb_channels, (int)s->frame_size * 2);
    s->frame = av_frame_alloc();
    s->pkt = av_packet_alloc();
    if (!s->fifo || !s->frame || !s->pkt) {
        lnd_ffe_close(s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    *state = s;
    return LND_OK;
}

static int32_t lnd_ffe_write(void *state, const float *pcm, uint64_t frames) {
    lnd_ff_enc *s = state;
    for (uint64_t done = 0; done < frames;) {
        int nb = (int)LND_MIN(frames - done, (uint64_t)4096);
        int out_frames = swr_get_out_samples(s->swr, nb);
        if (out_frames < 0) return LND_ERR_IO;
        if (out_frames > s->conv_frames) {
            if (s->conv) {
                av_freep(&s->conv[0]);
                av_freep(&s->conv);
            }
            if (av_samples_alloc_array_and_samples(&s->conv, nullptr, s->enc->ch_layout.nb_channels, out_frames + 64, s->enc->sample_fmt, 0) < 0)
                return LND_ERR_OUT_OF_MEMORY;
            s->conv_frames = out_frames + 64;
        }
        const uint8_t *in[1] = {(const uint8_t *)(pcm + done * s->channels)};
        int got = swr_convert(s->swr, s->conv, s->conv_frames, in, nb);
        if (got < 0) return LND_ERR_IO;
        if (got > 0 && av_audio_fifo_write(s->fifo, (void **)s->conv, got) < got) return LND_ERR_OUT_OF_MEMORY;
        while (av_audio_fifo_size(s->fifo) >= (int)s->frame_size) {
            int32_t r = lnd_ffe_encode(s, (int)s->frame_size, false);
            if (r != LND_OK) return r;
        }
        done += (uint64_t)nb;
    }
    return LND_OK;
}

static int32_t lnd_ffe_flush(void *state) {
    lnd_ff_enc *s = state;
    if (s->fmt && s->header) avio_flush(s->fmt->pb);
    return LND_OK;
}

static int32_t lnd_ffe_close(void *state) {
    lnd_ff_enc *s = state;
    int32_t r = LND_OK;
    if (s->header && s->swr && s->fifo) {
        int tail = swr_convert(s->swr, s->conv, s->conv_frames, nullptr, 0);
        if (tail > 0) av_audio_fifo_write(s->fifo, (void **)s->conv, tail);
        int rest = av_audio_fifo_size(s->fifo);
        if (rest > 0) {
            bool variable = s->enc->frame_size <= 0 || (s->enc->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE);
            r = lnd_ffe_encode(s, rest, !variable);
        }
        if (r == LND_OK && avcodec_send_frame(s->enc, nullptr) >= 0) r = lnd_ffe_drain(s);
        if (r == LND_OK && av_write_trailer(s->fmt) < 0) r = LND_ERR_IO;
    }
    if (s->conv) {
        av_freep(&s->conv[0]);
        av_freep(&s->conv);
    }
    if (s->swr) swr_free(&s->swr);
    if (s->fifo) av_audio_fifo_free(s->fifo);
    if (s->frame) av_frame_free(&s->frame);
    if (s->pkt) av_packet_free(&s->pkt);
    if (s->enc) avcodec_free_context(&s->enc);
    if (s->avio) {
        av_freep(&s->avio->buffer);
        avio_context_free(&s->avio);
    }
    if (s->fmt) {
        s->fmt->pb = nullptr;
        avformat_free_context(s->fmt);
    }
    lnd_free(s);
    return r;
}

const LND_ENCODER lnd_encoder_ffmpeg = {
    .name = "ffmpeg",
    .extensions = "wma;asf;mp2;ac3;m4a;mp4;aac;ogg;opus;flac;wav;mp3",
    .flags = LND_ENCODER_FLAG_FALLBACK,
    .open = lnd_ffe_open,
    .write = lnd_ffe_write,
    .flush = lnd_ffe_flush,
    .close = lnd_ffe_close,
};
