#include "formats/decode/stream.h"
#include <limits.h>

typedef struct lnd_aac_stream {
    HANDLE_AACDECODER dec;
    INT_PCM pcm[LND_AAC_MAX_PCM];
    uint64_t decoded;
    uint64_t coded;
    uint32_t flushing;
    bool raw;
} lnd_aac_stream;

static bool lnd_aac_stream_probe(const uint8_t *data, size_t bytes) { return bytes >= 2 && data[0] == 0xff && (data[1] & 0xf6) == 0xf0; }

static void *lnd_aac_stream_create(void) {
    lnd_aac_stream *s = lnd_alloc_zero(sizeof *s);
    if (!s) return nullptr;
    s->dec = aacDecoder_Open(TT_MP4_ADTS, 1);
    if (!s->dec) {
        lnd_free(s);
        return nullptr;
    }
    aacDecoder_SetParam(s->dec, AAC_PCM_OUTPUT_CHANNEL_MAPPING, 1);
    aacDecoder_SetParam(s->dec, AAC_PCM_MAX_OUTPUT_CHANNELS, 8);
    aacDecoder_SetParam(s->dec, AAC_PCM_LIMITER_ENABLE, 0);
    aacDecoder_SetParam(s->dec, AAC_CONCEAL_METHOD, 1);
    return s;
}

static int32_t lnd_aac_stream_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    lnd_aac_stream *s = state;
    *pcm = (LND_PCM){0};
    bool flush = end && !bytes;
    if (!flush) {
        if (bytes < (s->raw ? 8u : 7u)) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        size_t length;
        if (s->raw) {
            length = (size_t)data[0] << 24 | (size_t)data[1] << 16 | (size_t)data[2] << 8 | data[3];
            if (!length || length > UINT_MAX - 8) return LND_ERR_FORMAT;
        } else {
            if (!lnd_aac_stream_probe(data, bytes)) return LND_ERR_FORMAT;
            length = (size_t)(data[3] & 3) << 11 | (size_t)data[4] << 3 | data[5] >> 5;
            if (length < ((data[1] & 1) ? 7u : 9u)) return LND_ERR_FORMAT;
        }
        if (bytes < length + (s->raw ? 8 : 0)) return end ? LND_ERR_FORMAT : LND_SOURCE_WAITING;
        UCHAR *input = (UCHAR *)data + (s->raw ? 8 : 0);
        UINT size = (UINT)length, valid = size;
        if (aacDecoder_Fill(s->dec, &input, &size, &valid) != AAC_DEC_OK || valid) return LND_ERR_FORMAT;
        *used = length + (s->raw ? 8 : 0);
    } else {
        CStreamInfo *stream = aacDecoder_GetStreamInfo(s->dec);
        if (!stream || stream->frameSize < 1 || (uint64_t)s->flushing * stream->frameSize >= stream->outputDelay) return LND_SOURCE_EOF;
        s->flushing++;
    }
    AAC_DECODER_ERROR result = aacDecoder_DecodeFrame(s->dec, s->pcm, LND_AAC_MAX_PCM, flush ? AACDEC_FLUSH : 0);
    if (result == AAC_DEC_NOT_ENOUGH_BITS && !flush) return LND_SOURCE_READY;
    if (result != AAC_DEC_OK) return LND_ERR_FORMAT;
    CStreamInfo *stream = aacDecoder_GetStreamInfo(s->dec);
    if (!stream || stream->sampleRate < 1 || stream->numChannels < 1 || stream->numChannels > 8 || stream->frameSize < 1) return LND_ERR_FORMAT;
    uint32_t frames = (uint32_t)stream->frameSize;
    if (!flush) {
        uint32_t declared = s->raw ? (uint32_t)data[4] << 24 | (uint32_t)data[5] << 16 | (uint32_t)data[6] << 8 | data[7] : 0;
        s->coded += declared && declared <= frames ? declared : frames;
    }
    int64_t first = (int64_t)s->decoded - (int64_t)stream->outputDelay;
    s->decoded += frames;
    size_t skip = first < 0 ? (size_t)LND_MIN((uint64_t)-first, frames) : 0;
    size_t take = frames - skip;
    uint64_t position = first + (int64_t)skip;
    take = (size_t)LND_MIN(take, s->coded - LND_MIN(position, s->coded));
    info->format = LND_FORMAT_S16;
    info->channels = (uint32_t)stream->numChannels;
    info->sample_rate_hz = (uint32_t)stream->sampleRate;
    if (flush) info->length_frames = s->coded;
    *pcm = (LND_PCM){
        .data = s->pcm + skip * info->channels, .frames = take, .channels = info->channels, .format = LND_FORMAT_S16, .layout = LND_LAYOUT_INTERLEAVED};
    return LND_SOURCE_READY;
}

static void lnd_aac_stream_close(void *state) {
    lnd_aac_stream *s = state;
    aacDecoder_Close(s->dec);
    lnd_free(s);
}

static int32_t lnd_aac_stream_configure(void *state, const LND_CODEC_STREAM_CONFIG *config) {
    lnd_aac_stream *s = state;
    if (!config->bytes || config->bytes > UINT_MAX) return LND_ERR_FORMAT;
    HANDLE_AACDECODER dec = aacDecoder_Open(TT_MP4_RAW, 1);
    if (!dec) return LND_ERR_OUT_OF_MEMORY;
    UCHAR *data = (UCHAR *)config->data;
    UINT bytes = (UINT)config->bytes;
    if (aacDecoder_ConfigRaw(dec, &data, &bytes) != AAC_DEC_OK) {
        aacDecoder_Close(dec);
        return LND_ERR_FORMAT;
    }
    aacDecoder_SetParam(dec, AAC_PCM_OUTPUT_CHANNEL_MAPPING, 1);
    aacDecoder_SetParam(dec, AAC_PCM_MAX_OUTPUT_CHANNELS, 8);
    aacDecoder_SetParam(dec, AAC_PCM_LIMITER_ENABLE, 0);
    aacDecoder_SetParam(dec, AAC_CONCEAL_METHOD, 1);
    aacDecoder_Close(s->dec);
    s->dec = dec;
    s->raw = true;
    return LND_OK;
}

static const LND_CODEC_STREAM lnd_aac_stream_ops = {.configure = lnd_aac_stream_configure,
                                                    .probe = lnd_aac_stream_probe,
                                                    .create = lnd_aac_stream_create,
                                                    .step = lnd_aac_stream_step,
                                                    .close = lnd_aac_stream_close};
