#include "lindar_mediacodec.h"
#include "formats/codecs/system.h"
#include "src/thread.h"

#include <android/api-level.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaDataSource.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

typedef struct lnd_mc_state {
    LND_IO *io;
    lnd_mutex io_lock;
    AMediaDataSource *input;
    AMediaExtractor *extractor;
    AMediaCodec *decoder;
    const uint8_t *pending;
    size_t pending_bytes;
    ssize_t output;
    uint64_t skip_until;
    uint32_t channels, sample_rate_hz, frame_bytes;
    int32_t format;
    bool started, input_eof, output_eof, failed, closed, trimming, modern_buffers, format_pending;
} lnd_mc_state;

static ssize_t lnd_mc_input(void *user, off64_t offset, void *dst, size_t size) {
    lnd_mc_state *s = user;
    if (!size) return 0;
    if (offset < 0 || size > PTRDIFF_MAX) return -1;
    lnd_mutex_lock(&s->io_lock);
    int64_t got = -1;
    if (!s->closed) {
        if ((uint64_t)offset >= LND_IoGetSizeBytes(s->io))
            got = 0;
        else if (LND_IoSeekBytes(s->io, (uint64_t)offset) == LND_OK)
            got = LND_IoRead(s->io, dst, size);
    }
    lnd_mutex_unlock(&s->io_lock);
    /* Android extractors can discard short tail reads when EOF returns -1. */
    return got >= 0 ? (ssize_t)got : -1;
}

static ssize_t lnd_mc_size(void *user) { return (ssize_t)LND_IoGetSizeBytes(((lnd_mc_state *)user)->io); }

static void lnd_mc_input_close(void *user) {
    lnd_mc_state *s = user;
    lnd_mutex_lock(&s->io_lock);
    s->closed = true;
    lnd_mutex_unlock(&s->io_lock);
}

static void lnd_mc_release(lnd_mc_state *s) {
    if (s->output >= 0) AMediaCodec_releaseOutputBuffer(s->decoder, (size_t)s->output, false);
    s->output = -1;
    s->pending = nullptr;
    s->pending_bytes = 0;
}

static void lnd_mc_close(void *state) {
    lnd_mc_state *s = state;
    if (!s) return;
    if (s->decoder) {
        lnd_mc_release(s);
        if (s->started) AMediaCodec_stop(s->decoder);
        AMediaCodec_delete(s->decoder);
    }
    if (s->extractor) AMediaExtractor_delete(s->extractor);
    if (s->input) AMediaDataSource_delete(s->input);
    lnd_mutex_free(&s->io_lock);
    lnd_free(s);
}

static bool lnd_mc_format(lnd_mc_state *s) {
    AMediaFormat *f = AMediaCodec_getOutputFormat(s->decoder);
    if (!f) return false;
    int32_t channels = 0, sample_rate_hz = 0, encoding = 2;
    AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
    AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sample_rate_hz);
    AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_PCM_ENCODING, &encoding);
    AMediaFormat_delete(f);
    int32_t format;
    uint32_t bytes;
    switch (encoding) {
    case 2:
        format = LND_FORMAT_S16;
        bytes = 2;
        break;
    case 3:
        format = LND_FORMAT_U8;
        bytes = 1;
        break;
    case 4:
        format = LND_FORMAT_F32;
        bytes = 4;
        break;
    case 21:
        format = LND_FORMAT_S24;
        bytes = 3;
        break;
    case 22:
        format = LND_FORMAT_S32;
        bytes = 4;
        break;
    default:
        return false;
    }
    if (channels < 1 || channels > LND_MAX_CHANNELS || sample_rate_hz < 1) return false;
    if (s->frame_bytes && (s->channels != (uint32_t)channels || s->sample_rate_hz != (uint32_t)sample_rate_hz || s->format != format)) return false;
    s->channels = (uint32_t)channels;
    s->sample_rate_hz = (uint32_t)sample_rate_hz;
    s->format = format;
    s->frame_bytes = bytes * s->channels;
    return true;
}

static bool lnd_mc_feed(lnd_mc_state *s) {
    if (s->input_eof) return true;
    ssize_t index = AMediaCodec_dequeueInputBuffer(s->decoder, 0);
    if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) return true;
    if (index < 0) return false;
    size_t capacity = 0;
    uint8_t *buffer = AMediaCodec_getInputBuffer(s->decoder, (size_t)index, &capacity);
    if (!buffer) return false;
    ssize_t size = AMediaExtractor_getSampleSize(s->extractor);
    if (size < 0) {
        s->input_eof = true;
        return AMediaCodec_queueInputBuffer(s->decoder, (size_t)index, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) == AMEDIA_OK;
    }
    if ((size_t)size > capacity || (AMediaExtractor_getSampleFlags(s->extractor) & AMEDIAEXTRACTOR_SAMPLE_FLAG_ENCRYPTED)) return false;
    int64_t time = AMediaExtractor_getSampleTime(s->extractor);
    if (AMediaExtractor_readSampleData(s->extractor, buffer, capacity) != size) return false;
    if (AMediaCodec_queueInputBuffer(s->decoder, (size_t)index, 0, (size_t)size, time > 0 ? (uint64_t)time : 0, 0) != AMEDIA_OK) return false;
    AMediaExtractor_advance(s->extractor);
    return true;
}

static bool lnd_mc_pull(lnd_mc_state *s) {
    lnd_mc_release(s);
    if (s->output_eof || s->failed) return false;
    uint64_t start = lnd_time_ns();
    while (!s->output_eof && !s->failed) {
        if (!lnd_mc_feed(s)) break;
        AMediaCodecBufferInfo info = {0};
        ssize_t index = AMediaCodec_dequeueOutputBuffer(s->decoder, &info, 10000);
        if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            s->format_pending = true;
        } else if (index >= 0) {
            s->output = index;
            s->output_eof = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            if ((!s->frame_bytes || s->format_pending) && !lnd_mc_format(s)) break;
            s->format_pending = false;
            size_t capacity = 0;
            uint8_t *buffer = AMediaCodec_getOutputBuffer(s->decoder, (size_t)index, &capacity);
            size_t offset = s->modern_buffers && info.offset >= 0 ? (size_t)info.offset : 0;
            if (info.size < 0 || (s->modern_buffers && (info.offset < 0 || offset > capacity || (size_t)info.size > capacity - offset)) ||
                (info.size && !buffer))
                break;
            if (!(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) && info.size) {
                if ((uint32_t)info.size % s->frame_bytes) break;
                s->pending = buffer + offset;
                s->pending_bytes = (size_t)info.size;
                if (s->trimming) {
                    uint64_t position = info.presentationTimeUs > 0 ? lnd_system_frames_nearest((uint64_t)info.presentationTimeUs, s->sample_rate_hz, 1000000) : 0;
                    uint64_t skip = position < s->skip_until ? s->skip_until - position : 0;
                    skip = LND_MIN(skip, s->pending_bytes / s->frame_bytes);
                    s->pending += (size_t)skip * s->frame_bytes;
                    s->pending_bytes -= (size_t)skip * s->frame_bytes;
                    if (s->pending_bytes) s->trimming = false;
                }
                if (s->pending_bytes) return true;
            }
            lnd_mc_release(s);
        } else if (index != AMEDIACODEC_INFO_TRY_AGAIN_LATER && index != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            break;
        }
        if (s->output_eof) return false;
        if (lnd_time_ns() - start >= UINT64_C(5000000000)) break;
    }
    s->failed = true;
    return false;
}

static int32_t lnd_mc_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    *state = nullptr;
    if (LND_IoGetSizeBytes(io) > PTRDIFF_MAX) return LND_ERR_UNSUPPORTED;
    lnd_mc_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    s->output = -1;
    s->modern_buffers = android_get_device_api_level() >= 36;
    lnd_mutex_init(&s->io_lock);
    int32_t result = LND_ERR_FORMAT;
    int64_t duration = 0;
    s->input = AMediaDataSource_new();
    s->extractor = AMediaExtractor_new();
    if (!s->input || !s->extractor) {
        result = LND_ERR_OUT_OF_MEMORY;
        goto fail;
    }
    AMediaDataSource_setUserdata(s->input, s);
    AMediaDataSource_setReadAt(s->input, lnd_mc_input);
    AMediaDataSource_setGetSize(s->input, lnd_mc_size);
    AMediaDataSource_setClose(s->input, lnd_mc_input_close);
    if (AMediaExtractor_setDataSourceCustom(s->extractor, s->input) != AMEDIA_OK) goto fail;
    for (size_t i = 0, count = AMediaExtractor_getTrackCount(s->extractor); i < count; i++) {
        AMediaFormat *format = AMediaExtractor_getTrackFormat(s->extractor, i);
        if (!format) continue;
        const char *mime = nullptr;
        bool audio = AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime) && mime && !strncmp(mime, "audio/", 6);
        if (audio) {
            s->decoder = AMediaCodec_createDecoderByType(mime);
            if (s->decoder && AMediaCodec_configure(s->decoder, format, nullptr, nullptr, 0) == AMEDIA_OK &&
                AMediaExtractor_selectTrack(s->extractor, i) == AMEDIA_OK && AMediaCodec_start(s->decoder) == AMEDIA_OK) {
                s->started = true;
                AMediaFormat_getInt64(format, AMEDIAFORMAT_KEY_DURATION, &duration);
            } else if (s->decoder) {
                AMediaCodec_delete(s->decoder);
                s->decoder = nullptr;
                AMediaExtractor_unselectTrack(s->extractor, i);
            }
        }
        AMediaFormat_delete(format);
        if (s->started) break;
    }
    if (!s->started) goto fail;
    if (!lnd_mc_pull(s) && (s->failed || !s->frame_bytes)) goto fail;
    *info = (LND_CODEC_INFO){.format = s->format,
                             .channels = s->channels,
                             .sample_rate_hz = s->sample_rate_hz,
                             .length_frames = duration > 0 ? lnd_system_frames_ceil((uint64_t)duration, s->sample_rate_hz, 1000000) : 0,
                             .seekable = LND_IoCanSeek(io),
                             .length_estimated = true};
    *state = s;
    return LND_OK;
fail:
    lnd_mc_close(s);
    return result;
}

static uint64_t lnd_mc_read(void *state, void *dst, uint64_t frames) {
    lnd_mc_state *s = state;
    if (!frames) return 0;
    if (s->failed || frames > SIZE_MAX / s->frame_bytes) return LND_CODEC_READ_ERROR;
    uint64_t done = 0;
    while (done < frames) {
        if (!s->pending_bytes && !lnd_mc_pull(s)) break;
        size_t count = (size_t)LND_MIN(frames - done, s->pending_bytes / s->frame_bytes);
        size_t bytes = count * s->frame_bytes;
        memcpy((uint8_t *)dst + (size_t)done * s->frame_bytes, s->pending, bytes);
        s->pending += bytes;
        s->pending_bytes -= bytes;
        done += count;
    }
    return s->failed ? LND_CODEC_READ_ERROR : done;
}

static int32_t lnd_mc_seek(void *state, uint64_t frame) {
    lnd_mc_state *s = state;
    uint64_t time = lnd_system_frames(frame, 1000000, s->sample_rate_hz);
    if (time > INT64_MAX) return LND_ERR_INVALID_ARG;
    lnd_mc_release(s);
    if (AMediaCodec_flush(s->decoder) != AMEDIA_OK || AMediaExtractor_seekTo(s->extractor, (int64_t)time, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) != AMEDIA_OK) {
        s->failed = true;
        return LND_ERR_IO;
    }
    s->input_eof = s->output_eof = s->failed = false;
    s->format_pending = true;
    s->trimming = frame != 0;
    s->skip_until = frame;
    return LND_OK;
}

const LND_CODEC lnd_codec_mediacodec = {.name = "mediacodec",
                                        .extensions = "aac;adts;m4a;m4b;mp4;mp3;wav;ogg;opus;flac;amr;3gp;3g2;mkv;webm;ts",
                                        .flags = LND_CODEC_FLAG_SYSTEM,
                                        .probe = lnd_system_probe,
                                        .open = lnd_mc_open,
                                        .read = lnd_mc_read,
                                        .seek = lnd_mc_seek,
                                        .close = lnd_mc_close};

const LND_CODEC *LND_MediaCodecGetCodec(void) { return &lnd_codec_mediacodec; }
