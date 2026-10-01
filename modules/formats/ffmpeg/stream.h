#include "formats/decode/stream.h"
#if LND_MODULE_FFMPEG_STREAM
#include "formats/ffmpeg/ffmpeg_stream/stream.h"
#endif
#include <libavcodec/codec_desc.h>

typedef struct lnd_ff_stream {
#if LND_MODULE_FFMPEG_STREAM
    void *container;
#endif
    AVCodecContext *decoder;
    AVCodecParserContext *parser;
    AVFrame *frame;
    AVPacket *packet;
    SwrContext *swr;
    AVChannelLayout layout;
    enum AVSampleFormat format;
    uint32_t sample_rate_hz;
    uint32_t remaining;
    uint32_t declared;
    uint64_t total;
    float *pcm;
    size_t capacity;
    bool direct;
    bool draining;
    bool parser_end;
} lnd_ff_stream;

static void *lnd_ff_stream_create(void) { return lnd_alloc_zero(sizeof(lnd_ff_stream)); }
static bool lnd_ff_stream_probe(const uint8_t *data, size_t bytes) {
#if LND_MODULE_FFMPEG_STREAM
    if (lnd_ff_container_probe(data, bytes)) return true;
#endif
    return bytes >= 7 && ((data[0] == 0x0b && data[1] == 0x77) || (data[0] == 0xff && (data[1] & 0xf6) == 0xf0));
}

static int32_t lnd_ff_stream_configure(void *state, const LND_CODEC_STREAM_CONFIG *config) {
    lnd_ff_stream *s = state;
    const AVCodecDescriptor *descriptor = avcodec_descriptor_get_by_name(config->codec);
    const AVCodec *codec = descriptor ? avcodec_find_decoder(descriptor->id) : nullptr;
    if (!codec || descriptor->type != AVMEDIA_TYPE_AUDIO) return LND_ERR_UNSUPPORTED;
    s->decoder = avcodec_alloc_context3(codec);
    s->frame = av_frame_alloc();
    s->packet = av_packet_alloc();
    if (!s->decoder || !s->frame || !s->packet) return LND_ERR_OUT_OF_MEMORY;
    s->decoder->thread_count = 1;
    s->decoder->sample_rate = (int)config->sample_rate_hz;
    if (config->channels) av_channel_layout_default(&s->decoder->ch_layout, (int)config->channels);
    const uint8_t *data = config->data;
    size_t bytes = config->bytes;
    uint8_t converted[512] = {0};
    if (descriptor->id == AV_CODEC_ID_OPUS) {
        if (bytes < 11 || data[0] || data[10] > 1 || !data[1] || data[1] > LND_MAX_CHANNELS) return LND_ERR_FORMAT;
        size_t extra = data[10] ? (size_t)data[1] + 2 : 0;
        if (bytes != 11 + extra) return LND_ERR_FORMAT;
        memcpy(converted, "OpusHead", 8);
        converted[8] = 1;
        converted[9] = data[1];
        converted[10] = data[3];
        converted[11] = data[2];
        for (unsigned i = 0; i < 4; i++)
            converted[12 + i] = data[7 - i];
        converted[16] = data[9];
        converted[17] = data[8];
        converted[18] = data[10];
        if (extra) memcpy(converted + 19, data + 11, extra);
        if (config->trim_known) converted[10] = converted[11] = 0;
        bytes = 19 + extra;
        data = converted;
    } else if (descriptor->id == AV_CODEC_ID_FLAC) {
        if (bytes < 42) return LND_ERR_FORMAT;
        data += 8;
        bytes = 34;
    } else if (descriptor->id == AV_CODEC_ID_ALAC) {
        if (bytes != 28) return LND_ERR_FORMAT;
        converted[3] = 36;
        memcpy(converted + 4, "alac", 4);
        memcpy(converted + 8, data, bytes);
        data = converted;
        bytes = 36;
    }
    if (bytes) {
        if (bytes > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE) return LND_ERR_FORMAT;
        s->decoder->extradata = av_mallocz(bytes + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!s->decoder->extradata) return LND_ERR_OUT_OF_MEMORY;
        memcpy(s->decoder->extradata, data, bytes);
        s->decoder->extradata_size = (int)bytes;
    }
    if (config->elementary) {
        s->parser = av_parser_init(descriptor->id);
        if (!s->parser) return LND_ERR_UNSUPPORTED;
    }
    int r = avcodec_open2(s->decoder, codec, nullptr);
    return r < 0 ? r == AVERROR(ENOMEM) ? LND_ERR_OUT_OF_MEMORY : LND_ERR_FORMAT : LND_OK;
}

static int32_t lnd_ff_stream_pcm(lnd_ff_stream *s, LND_PCM *pcm, LND_CODEC_INFO *info) {
    AVFrame *frame = s->frame;
    uint32_t channels = (uint32_t)frame->ch_layout.nb_channels;
    if (!channels || channels > LND_MAX_CHANNELS || frame->nb_samples < 0 || frame->nb_samples > 65536 || frame->sample_rate < 1 || frame->sample_rate > 768000)
        return LND_ERR_FORMAT;
    if (!s->swr || s->format != frame->format || s->sample_rate_hz != (uint32_t)frame->sample_rate ||
        av_channel_layout_compare(&s->layout, &frame->ch_layout)) {
        swr_free(&s->swr);
        av_channel_layout_uninit(&s->layout);
        if (av_channel_layout_copy(&s->layout, &frame->ch_layout) < 0) return LND_ERR_OUT_OF_MEMORY;
        s->format = (enum AVSampleFormat)frame->format;
        s->sample_rate_hz = (uint32_t)frame->sample_rate;
        if (swr_alloc_set_opts2(&s->swr, &s->layout, AV_SAMPLE_FMT_FLT, frame->sample_rate, &s->layout, s->format, frame->sample_rate, 0, nullptr) < 0 ||
            swr_init(s->swr) < 0)
            return LND_ERR_FORMAT;
    }
    size_t samples = (size_t)frame->nb_samples * channels;
    if (samples > s->capacity) {
        float *grown = lnd_realloc(s->pcm, samples * sizeof(float));
        if (!grown) return LND_ERR_OUT_OF_MEMORY;
        s->pcm = grown;
        s->capacity = samples;
    }
    uint8_t *output = (uint8_t *)s->pcm;
    int got = swr_convert(s->swr, &output, frame->nb_samples, (const uint8_t **)frame->extended_data, frame->nb_samples);
    if (got < 0) return LND_ERR_FORMAT;
    if (!s->parser && s->declared && s->declared < (uint32_t)got) got = (int)s->declared;
    s->declared = 0;
    *info = (LND_CODEC_INFO){.sample_rate_hz = s->sample_rate_hz, .channels = channels, .format = LND_FORMAT_F32};
    *pcm = (LND_PCM){.data = s->pcm, .frames = (size_t)got, .channels = channels, .format = LND_FORMAT_F32, .layout = LND_LAYOUT_INTERLEAVED};
    s->total += (uint32_t)got;
    av_frame_unref(frame);
    return LND_SOURCE_READY;
}

static int32_t lnd_ff_stream_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    lnd_ff_stream *s = state;
    *pcm = (LND_PCM){0};
#if LND_MODULE_FFMPEG_STREAM
    if (!s->decoder && !s->container && lnd_ff_container_probe(data, bytes)) {
        s->container = lnd_ff_container_create();
        if (!s->container) return LND_ERR_OUT_OF_MEMORY;
    }
    if (s->container) return lnd_ff_container_step(s->container, data, bytes, end, used, pcm, info);
#endif
    if (!s->decoder) {
        if (bytes < 7) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        if (!lnd_ff_stream_probe(data, bytes)) return LND_ERR_FORMAT;
        const char *codec = data[0] == 0xff ? "aac" : data[5] >> 3 > 10 ? "eac3" : "ac3";
        LND_CODEC_STREAM_CONFIG config = {.codec = codec, .elementary = true};
        int32_t configured = lnd_ff_stream_configure(s, &config);
        if (configured != LND_OK) return configured;
        s->direct = true;
    }
    int r = avcodec_receive_frame(s->decoder, s->frame);
    if (r == 0) return lnd_ff_stream_pcm(s, pcm, info);
    if (r == AVERROR_EOF) {
        info->length_frames = s->total;
        return LND_SOURCE_EOF;
    }
    if (r != AVERROR(EAGAIN)) return LND_ERR_FORMAT;
    size_t prefix = 0;
    if (s->direct)
        s->remaining = (uint32_t)LND_MIN(bytes, (size_t)INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE);
    else if (!s->remaining && bytes) {
        if (bytes < 8) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        s->remaining = (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 | data[3];
        s->declared = (uint32_t)data[4] << 24 | (uint32_t)data[5] << 16 | (uint32_t)data[6] << 8 | data[7];
        if (!s->remaining || s->remaining > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE) return LND_ERR_FORMAT;
        prefix = 8;
        data += 8;
        bytes -= 8;
    }
    if (s->remaining > bytes) {
        if (prefix) s->remaining = 0;
        return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
    }
    uint32_t take = s->remaining;
    if (!take && !end) return LND_SOURCE_WAITING;
    av_packet_unref(s->packet);
    if (take) {
        if (av_new_packet(s->packet, (int)take) < 0) return LND_ERR_OUT_OF_MEMORY;
        memcpy(s->packet->data, data, take);
    }
    uint8_t *packet = s->packet->data;
    int packet_bytes = (int)take;
    if (s->parser && !s->parser_end) {
        int parsed = av_parser_parse2(s->parser, s->decoder, &packet, &packet_bytes, s->packet->data, (int)take, AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
        if (parsed < 0 || (uint32_t)parsed > take) return LND_ERR_FORMAT;
        s->remaining -= (uint32_t)parsed;
        *used = prefix + (size_t)parsed;
        if (!take) s->parser_end = true;
        if (!packet_bytes && take) return LND_SOURCE_READY;
        AVPacket *copy = av_packet_alloc();
        if (!copy) return LND_ERR_OUT_OF_MEMORY;
        if (packet_bytes && av_new_packet(copy, packet_bytes) < 0) {
            av_packet_free(&copy);
            return LND_ERR_OUT_OF_MEMORY;
        }
        if (packet_bytes) memcpy(copy->data, packet, (size_t)packet_bytes);
        av_packet_free(&s->packet);
        s->packet = copy;
    } else {
        *used = prefix + take;
        s->remaining = 0;
    }
    if (packet_bytes)
        r = avcodec_send_packet(s->decoder, s->packet);
    else if (!s->draining) {
        r = avcodec_send_packet(s->decoder, nullptr);
        s->draining = true;
    } else
        return LND_ERR_FORMAT;
    return r < 0 ? LND_ERR_FORMAT : LND_SOURCE_READY;
}

static void lnd_ff_stream_close(void *state) {
    lnd_ff_stream *s = state;
#if LND_MODULE_FFMPEG_STREAM
    lnd_ff_container_close(s->container);
#endif
    if (s->parser) av_parser_close(s->parser);
    avcodec_free_context(&s->decoder);
    av_frame_free(&s->frame);
    av_packet_free(&s->packet);
    swr_free(&s->swr);
    av_channel_layout_uninit(&s->layout);
    lnd_free(s->pcm);
    lnd_free(s);
}

#if LND_MODULE_FFMPEG_STREAM
static int32_t lnd_ff_stream_metadata(void *state, LND_METADATA *m, uint64_t *revision) {
    lnd_ff_stream *s = state;
    return s->container ? lnd_ff_container_metadata(s->container, m, revision) : LND_ERR_UNSUPPORTED;
}
#endif

static const LND_CODEC_STREAM lnd_ff_stream_ops = {
#if LND_MODULE_FFMPEG_STREAM
    .flags = LND_CODEC_STREAM_ASYNC,
    .metadata = lnd_ff_stream_metadata,
#endif
    .probe = lnd_ff_stream_probe,
    .create = lnd_ff_stream_create,
    .configure = lnd_ff_stream_configure,
    .step = lnd_ff_stream_step,
    .close = lnd_ff_stream_close};
