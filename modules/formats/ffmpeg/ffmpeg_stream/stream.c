#include "stream.h"
#include "../ffmpeg.h"
#include "src/alloc.h"
#include "src/thread.h"
#include "src/atomic.h"
#include "formats/decode/metadata.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <string.h>
#include <stdio.h>

#define INPUT_BYTES 262144

typedef struct lnd_ff_container {
    lnd_thread thread;
    lnd_mutex mutex;
    lnd_event wake;
    lnd_atomic_u32 stop;
    uint8_t input[INPUT_BYTES];
    size_t head;
    size_t count;
    bool end;
    bool started;
    bool pending;
    int32_t status;
    float *output;
    size_t capacity;
    float *returned;
    size_t returned_capacity;
    LND_PCM pcm;
    LND_CODEC_INFO info;
    lnd_decode_tags tags;
    uint64_t total;
} lnd_ff_container;

bool lnd_ff_container_probe(const uint8_t *data, size_t bytes) {
    static const uint8_t asf[] = {0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11, 0xa6, 0xd9, 0, 0xaa, 0, 0x62, 0xce, 0x6c};
    return (bytes >= 4 && !memcmp(data, "\x1a\x45\xdf\xa3", 4)) || (bytes >= sizeof asf && !memcmp(data, asf, sizeof asf));
}

static int read_input(void *user, uint8_t *data, int bytes) {
    lnd_ff_container *s = user;
    for (;;) {
        if (lnd_load(&s->stop)) return AVERROR_EXIT;
        lnd_mutex_lock(&s->mutex);
        size_t n = LND_MIN((size_t)bytes, s->count);
        if (n) {
            size_t first = LND_MIN(n, INPUT_BYTES - s->head);
            memcpy(data, s->input + s->head, first);
            memcpy(data + first, s->input, n - first);
            s->head = (s->head + n) % INPUT_BYTES;
            s->count -= n;
        }
        bool end = s->end;
        lnd_mutex_unlock(&s->mutex);
        if (n) return (int)n;
        if (end) return AVERROR_EOF;
        lnd_event_wait(&s->wake, 20);
    }
}

static int interrupted(void *user) { return lnd_load(&((lnd_ff_container *)user)->stop) != 0; }

static void metadata(lnd_ff_container *s, AVFormatContext *format, AVStream *stream) {
#if LND_MODULE_METADATA
    LND_METADATA *m = LND_MetadataCreate(nullptr);
    int32_t r = m ? LND_OK : LND_ERR_OUT_OF_MEMORY;
    AVDictionary *dicts[] = {format->metadata, stream->metadata};
    for (unsigned i = 0; !r && i < 2; ++i) {
        const AVDictionaryEntry *entry = nullptr;
        while (!r && (entry = av_dict_get(dicts[i], "", entry, AV_DICT_IGNORE_SUFFIX)))
            r = LND_MetadataSetValue(m, entry->key, entry->value);
    }
    for (unsigned i = 0; !r && i < format->nb_chapters; ++i) {
        AVChapter *c = format->chapters[i];
        char id[32];
        snprintf(id, sizeof id, "%lld", (long long)c->id);
        const AVDictionaryEntry *title = av_dict_get(c->metadata, "title", nullptr, 0);
        int64_t start = av_rescale_q(c->start, c->time_base, AV_TIME_BASE_Q);
        int64_t end = c->end == AV_NOPTS_VALUE ? -1 : av_rescale_q(c->end, c->time_base, AV_TIME_BASE_Q);
        LND_METADATA_CHAPTER chapter = {.id = id,
                                        .title = title ? title->value : "",
                                        .start_us = (uint64_t)LND_MAX(start, 0),
                                        .end_us = end >= start && end >= 0 ? (uint64_t)end : LND_METADATA_UNKNOWN,
                                        .start_offset_bytes = LND_METADATA_UNKNOWN,
                                        .end_offset_bytes = LND_METADATA_UNKNOWN};
        r = LND_MetadataSetChapter(m, &chapter);
    }
    lnd_mutex_lock(&s->mutex);
    LND_MetadataFree(s->tags.metadata);
    s->tags.metadata = m;
    s->tags.status = r;
    s->tags.revision = lnd_tag_next_revision();
    lnd_mutex_unlock(&s->mutex);
#endif
}

static int32_t publish(lnd_ff_container *s, AVFrame *frame, SwrContext **swr, AVChannelLayout *layout, int *sample_format, int *rate) {
    uint32_t channels = (uint32_t)frame->ch_layout.nb_channels;
    if (!channels || channels > LND_MAX_CHANNELS || frame->nb_samples < 0 || frame->nb_samples > 65536 || frame->sample_rate < 1 || frame->sample_rate > 768000)
        return LND_ERR_FORMAT;
    if (!*swr || *sample_format != frame->format || *rate != frame->sample_rate || av_channel_layout_compare(layout, &frame->ch_layout)) {
        swr_free(swr);
        av_channel_layout_uninit(layout);
        if (av_channel_layout_copy(layout, &frame->ch_layout) < 0) return LND_ERR_OUT_OF_MEMORY;
        *sample_format = frame->format;
        *rate = frame->sample_rate;
        if (swr_alloc_set_opts2(swr, layout, AV_SAMPLE_FMT_FLT, *rate, layout, (enum AVSampleFormat) * sample_format, *rate, 0, nullptr) < 0 ||
            swr_init(*swr) < 0)
            return LND_ERR_FORMAT;
    }
    for (;;) {
        if (lnd_load(&s->stop)) return LND_ERR_STATE;
        lnd_mutex_lock(&s->mutex);
        if (!s->pending) break;
        lnd_mutex_unlock(&s->mutex);
        lnd_event_wait(&s->wake, 20);
    }
    size_t samples = (size_t)frame->nb_samples * channels;
    if (samples > s->capacity) {
        float *grown = lnd_realloc(s->output, samples * sizeof(float));
        if (!grown) {
            lnd_mutex_unlock(&s->mutex);
            return LND_ERR_OUT_OF_MEMORY;
        }
        s->output = grown;
        s->capacity = samples;
    }
    uint8_t *output = (uint8_t *)s->output;
    int got = swr_convert(*swr, &output, frame->nb_samples, (const uint8_t **)frame->extended_data, frame->nb_samples);
    if (got >= 0) {
        s->info = (LND_CODEC_INFO){.format = LND_FORMAT_F32, .channels = channels, .sample_rate_hz = (uint32_t)*rate};
        s->pcm = (LND_PCM){.data = s->output, .frames = (size_t)got, .channels = channels, .format = LND_FORMAT_F32};
        s->total += (uint32_t)got;
        s->pending = got > 0;
    }
    lnd_mutex_unlock(&s->mutex);
    return got < 0 ? LND_ERR_FORMAT : LND_OK;
}

static void worker(void *user) {
    lnd_ff_init();
    lnd_ff_container *s = user;
    AVFormatContext *format = avformat_alloc_context();
    AVIOContext *io = nullptr;
    AVCodecContext *decoder = nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    SwrContext *swr = nullptr;
    AVChannelLayout layout = {0};
    int sample_format = -1, rate = 0;
    int32_t result = LND_ERR_OUT_OF_MEMORY;
    uint8_t *buffer = av_malloc(32768);
    if (!format || !packet || !frame || !buffer) {
        av_free(buffer);
        goto done;
    }
    io = avio_alloc_context(buffer, 32768, 0, s, read_input, nullptr, nullptr);
    if (!io) {
        av_free(buffer);
        goto done;
    }
    io->seekable = 0;
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    format->interrupt_callback = (AVIOInterruptCB){interrupted, s};
    format->probesize = INPUT_BYTES;
    format->max_analyze_duration = AV_TIME_BASE;
    av_opt_set(format, "format_whitelist", "matroska,webm,asf", 0);
    result = LND_ERR_FORMAT;
    if (avformat_open_input(&format, nullptr, nullptr, nullptr) < 0) goto done;
    if (avformat_find_stream_info(format, nullptr) < 0) goto done;
    const AVCodec *codec = nullptr;
    int index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (index < 0 || !codec) goto done;
    decoder = avcodec_alloc_context3(codec);
    if (!decoder) {
        result = LND_ERR_OUT_OF_MEMORY;
        goto done;
    }
    decoder->thread_count = 1;
    if (avcodec_parameters_to_context(decoder, format->streams[index]->codecpar) < 0 || avcodec_open2(decoder, codec, nullptr) < 0) goto done;
    metadata(s, format, format->streams[index]);
    bool draining = false;
    while (!lnd_load(&s->stop)) {
        int r = avcodec_receive_frame(decoder, frame);
        if (!r) {
            result = publish(s, frame, &swr, &layout, &sample_format, &rate);
            av_frame_unref(frame);
            if (result) goto done;
            continue;
        }
        if (r == AVERROR_EOF) {
            result = LND_SOURCE_EOF;
            goto done;
        }
        if (r != AVERROR(EAGAIN) || draining) {
            result = LND_ERR_FORMAT;
            goto done;
        }
        do {
            av_packet_unref(packet);
            r = av_read_frame(format, packet);
        } while (r >= 0 && packet->stream_index != index && !lnd_load(&s->stop));
        if (format->event_flags & AVFMT_EVENT_FLAG_METADATA_UPDATED || format->streams[index]->event_flags & AVSTREAM_EVENT_FLAG_METADATA_UPDATED) {
            metadata(s, format, format->streams[index]);
            format->event_flags &= ~AVFMT_EVENT_FLAG_METADATA_UPDATED;
            format->streams[index]->event_flags &= ~AVSTREAM_EVENT_FLAG_METADATA_UPDATED;
        }
        if (r < 0) {
            if (r != AVERROR_EOF) {
                result = LND_ERR_IO;
                goto done;
            }
            draining = true;
            r = avcodec_send_packet(decoder, nullptr);
        } else {
            if (packet->size > 16 * 1024 * 1024) {
                result = LND_ERR_FORMAT;
                goto done;
            }
            r = avcodec_send_packet(decoder, packet);
        }
        if (r < 0) {
            result = LND_ERR_FORMAT;
            goto done;
        }
    }
    result = LND_ERR_STATE;
done:
    swr_free(&swr);
    av_channel_layout_uninit(&layout);
    avcodec_free_context(&decoder);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avformat_close_input(&format);
    if (io) {
        av_freep(&io->buffer);
        avio_context_free(&io);
    }
    lnd_mutex_lock(&s->mutex);
    s->status = result;
    lnd_mutex_unlock(&s->mutex);
}

void *lnd_ff_container_create(void) {
    lnd_ff_container *s = lnd_alloc_zero(sizeof *s);
    if (!s) return nullptr;
    lnd_mutex_init(&s->mutex);
    if (lnd_event_init(&s->wake)) {
        lnd_mutex_free(&s->mutex);
        lnd_free(s);
        return nullptr;
    }
    return s;
}

void lnd_ff_container_close(void *state) {
    lnd_ff_container *s = state;
    if (!s) return;
    lnd_store(&s->stop, 1);
    lnd_event_signal(&s->wake);
    if (s->started) lnd_thread_join(&s->thread);
    lnd_event_free(&s->wake);
    lnd_mutex_free(&s->mutex);
    lnd_decode_tags_clear(&s->tags);
    lnd_free(s->output);
    lnd_free(s->returned);
    lnd_free(s);
}

int32_t lnd_ff_container_step(void *state, const uint8_t *data, size_t bytes, bool end, size_t *used, LND_PCM *pcm, LND_CODEC_INFO *info) {
    lnd_ff_container *s = state;
    if (!s->started) {
        int32_t r = lnd_thread_create(&s->thread, worker, s);
        if (r) return r;
        s->started = true;
    }
    lnd_mutex_lock(&s->mutex);
    size_t take = LND_MIN(bytes, INPUT_BYTES - s->count);
    size_t tail = (s->head + s->count) % INPUT_BYTES;
    size_t first = LND_MIN(take, INPUT_BYTES - tail);
    if (take) {
        memcpy(s->input + tail, data, first);
        memcpy(s->input, data + first, take - first);
    }
    s->count += take;
    *used = take;
    if (end && take == bytes) s->end = true;
    int32_t result = s->status ? s->status : take ? LND_SOURCE_READY : LND_SOURCE_WAITING;
    if (s->pending) {
        float *previous = s->returned;
        size_t capacity = s->returned_capacity;
        s->returned = s->output;
        s->returned_capacity = s->capacity;
        s->output = previous;
        s->capacity = capacity;
        *pcm = s->pcm;
        *info = s->info;
        s->pending = false;
        result = LND_SOURCE_READY;
    } else if (result == LND_SOURCE_EOF) {
        *info = s->info;
        info->length_frames = s->total;
        info->length_known = true;
    }
    lnd_mutex_unlock(&s->mutex);
    lnd_event_signal(&s->wake);
    return result;
}

int32_t lnd_ff_container_metadata(void *state, LND_METADATA *m, uint64_t *revision) {
    lnd_ff_container *s = state;
    lnd_mutex_lock(&s->mutex);
    int32_t r = lnd_decode_tags_get(&s->tags, m, revision);
    lnd_mutex_unlock(&s->mutex);
    return r;
}
