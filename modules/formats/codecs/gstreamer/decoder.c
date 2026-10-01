#include "lindar_gstreamer.h"
#include "formats/codecs/system.h"

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/audio/audio.h>

typedef struct lnd_gst_state {
    LND_IO *io;
    GstElement *pipeline, *source, *convert, *sink;
    GstBus *bus;
    GstSample *sample;
    GstMapInfo map;
    GMutex io_lock;
    uint64_t input_pos;
    size_t offset;
    uint32_t channels, sample_rate_hz, frame_bytes;
    bool mapped, failed;
    gint linear_seek;
} lnd_gst_state;

static void lnd_gst_need(GstAppSrc *source, guint requested, gpointer user) {
    lnd_gst_state *s = user;
    g_mutex_lock(&s->io_lock);
    uint64_t left = LND_IoGetSizeBytes(s->io) - s->input_pos;
    size_t size = (size_t)LND_MIN(left, requested == G_MAXUINT ? 65536 : requested);
    if (!size) {
        g_mutex_unlock(&s->io_lock);
        gst_app_src_end_of_stream(source);
        return;
    }
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
    GstMapInfo map = GST_MAP_INFO_INIT;
    bool mapped = buffer && gst_buffer_map(buffer, &map, GST_MAP_WRITE);
    int64_t got = 0;
    if (mapped && LND_IoSeekBytes(s->io, s->input_pos) == LND_OK) got = LND_IoRead(s->io, map.data, size);
    if (mapped) gst_buffer_unmap(buffer, &map);
    if (buffer && got > 0) {
        gst_buffer_resize(buffer, 0, (gssize)got);
        GST_BUFFER_OFFSET(buffer) = s->input_pos;
        GST_BUFFER_OFFSET_END(buffer) = s->input_pos + got;
        s->input_pos += got;
    }
    g_mutex_unlock(&s->io_lock);
    if (got <= 0) {
        if (buffer) gst_buffer_unref(buffer);
        GST_ELEMENT_ERROR(s->source, RESOURCE, READ, ("Cannot read Lindar input"), (nullptr));
        gst_app_src_end_of_stream(source);
        return;
    }
    gst_app_src_push_buffer(source, buffer);
}

static gboolean lnd_gst_input_seek(GstAppSrc *source, guint64 offset, gpointer user) {
    lnd_gst_state *s = user;
    if (offset > LND_IoGetSizeBytes(s->io)) return FALSE;
    g_mutex_lock(&s->io_lock);
    s->input_pos = offset;
    g_mutex_unlock(&s->io_lock);
    return TRUE;
}

static void lnd_gst_pad(GstElement *decoder, GstPad *pad, gpointer user) {
    lnd_gst_state *s = user;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    bool audio = caps && gst_caps_get_size(caps) && gst_structure_has_name(gst_caps_get_structure(caps, 0), "audio/x-raw");
    if (caps) gst_caps_unref(caps);
    GstPad *target = gst_element_get_static_pad(s->convert, "sink");
    if (audio && !gst_pad_is_linked(target)) {
        if (gst_pad_link(pad, target) != GST_PAD_LINK_OK) GST_ELEMENT_ERROR(s->convert, CORE, NEGOTIATION, ("Cannot link decoded audio"), (nullptr));
        gst_object_unref(target);
        return;
    }
    gst_object_unref(target);
    GstElement *discard = gst_element_factory_make("fakesink", nullptr);
    if (!discard) {
        GST_ELEMENT_ERROR(s->convert, CORE, MISSING_PLUGIN, ("Missing fakesink"), (nullptr));
        return;
    }
    g_object_set(discard, "sync", FALSE, "async", FALSE, "enable-last-sample", FALSE, nullptr);
    if (!gst_bin_add(GST_BIN(s->pipeline), discard)) {
        gst_object_unref(discard);
        GST_ELEMENT_ERROR(s->convert, CORE, FAILED, ("Cannot add stream sink"), (nullptr));
        return;
    }
    target = gst_element_get_static_pad(discard, "sink");
    gst_pad_link(pad, target);
    gst_object_unref(target);
    gst_element_sync_state_with_parent(discard);
}

static gint lnd_gst_select(GstElement *decoder, GstPad *pad, GstCaps *caps, GstElementFactory *factory, gpointer user) {
    lnd_gst_state *s = user;
    if (!strcmp(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)), "auparse")) g_atomic_int_set(&s->linear_seek, TRUE);
    const gchar *klass = gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS);
    return klass && strstr(klass, "Decoder") && (strstr(klass, "Video") || strstr(klass, "Image")) ? 1 : 0;
}

static void lnd_gst_release(lnd_gst_state *s) {
    if (s->mapped) gst_buffer_unmap(gst_sample_get_buffer(s->sample), &s->map);
    if (s->sample) gst_sample_unref(s->sample);
    s->sample = nullptr;
    s->mapped = false;
    s->offset = 0;
}

static void lnd_gst_dispose(lnd_gst_state *s) {
    if (s->pipeline) gst_element_set_state(s->pipeline, GST_STATE_NULL);
    lnd_gst_release(s);
    if (s->bus) gst_object_unref(s->bus);
    if (s->pipeline) gst_object_unref(s->pipeline);
    s->pipeline = s->source = s->convert = s->sink = nullptr;
    s->bus = nullptr;
    s->input_pos = 0;
}

static void lnd_gst_close(void *state) {
    lnd_gst_state *s = state;
    if (!s) return;
    lnd_gst_dispose(s);
    g_mutex_clear(&s->io_lock);
    lnd_free(s);
}

static bool lnd_gst_error(lnd_gst_state *s) {
    GstMessage *message = gst_bus_pop_filtered(s->bus, GST_MESSAGE_ERROR);
    if (!message) return false;
    gst_message_unref(message);
    s->failed = true;
    return true;
}

static bool lnd_gst_pull(lnd_gst_state *s) {
    lnd_gst_release(s);
    gint64 start = g_get_monotonic_time();
    while (!s->failed) {
        if (lnd_gst_error(s)) break;
        s->sample = gst_app_sink_try_pull_sample(GST_APP_SINK(s->sink), 100 * GST_MSECOND);
        if (s->sample) {
            GstAudioInfo info;
            if (!gst_audio_info_from_caps(&info, gst_sample_get_caps(s->sample)) || GST_AUDIO_INFO_FORMAT(&info) != GST_AUDIO_FORMAT_F32 ||
                GST_AUDIO_INFO_LAYOUT(&info) != GST_AUDIO_LAYOUT_INTERLEAVED || GST_AUDIO_INFO_CHANNELS(&info) < 1 ||
                GST_AUDIO_INFO_CHANNELS(&info) > LND_MAX_CHANNELS || GST_AUDIO_INFO_RATE(&info) < 1)
                break;
            if (s->frame_bytes && (s->channels != (uint32_t)GST_AUDIO_INFO_CHANNELS(&info) || s->sample_rate_hz != (uint32_t)GST_AUDIO_INFO_RATE(&info))) break;
            s->channels = (uint32_t)GST_AUDIO_INFO_CHANNELS(&info);
            s->sample_rate_hz = (uint32_t)GST_AUDIO_INFO_RATE(&info);
            s->frame_bytes = s->channels * sizeof(float);
            GstBuffer *buffer = gst_sample_get_buffer(s->sample);
            if (!buffer || !gst_buffer_map(buffer, &s->map, GST_MAP_READ)) break;
            s->mapped = true;
            if (s->map.size % s->frame_bytes) break;
            if (s->map.size) return true;
            lnd_gst_release(s);
        } else if (gst_app_sink_is_eos(GST_APP_SINK(s->sink))) {
            lnd_gst_error(s);
            return false;
        }
        if (g_get_monotonic_time() - start >= 5000000) break;
    }
    s->failed = true;
    return false;
}

static bool lnd_gst_setup(lnd_gst_state *s) {
    g_atomic_int_set(&s->linear_seek, FALSE);
    s->pipeline = gst_pipeline_new(nullptr);
    if (!s->pipeline) return false;
    const char *names[] = {"appsrc", "decodebin", "audioconvert", "appsink"};
    GstElement *elements[4] = {0};
    for (unsigned i = 0; i < LND_COUNTOF(elements); i++) {
        elements[i] = gst_element_factory_make(names[i], nullptr);
        if (!elements[i]) return false;
        if (!gst_bin_add(GST_BIN(s->pipeline), elements[i])) {
            gst_object_unref(elements[i]);
            return false;
        }
    }
    s->source = elements[0];
    s->convert = elements[2];
    s->sink = elements[3];
    s->bus = gst_element_get_bus(s->pipeline);
    GstCaps *caps = gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, GST_AUDIO_NE(F32), "layout", G_TYPE_STRING, "interleaved", nullptr);
    gst_app_sink_set_caps(GST_APP_SINK(s->sink), caps);
    gst_caps_unref(caps);
    g_object_set(s->sink, "sync", FALSE, "enable-last-sample", FALSE, "max-buffers", (guint)4, "wait-on-eos", FALSE, nullptr);
    gst_app_src_set_size(GST_APP_SRC(s->source), (gint64)LND_IoGetSizeBytes(s->io));
    gst_app_src_set_stream_type(GST_APP_SRC(s->source), GST_APP_STREAM_TYPE_RANDOM_ACCESS);
    g_object_set(s->source, "format", GST_FORMAT_BYTES, "block", FALSE, nullptr);
    GstAppSrcCallbacks callbacks = {.need_data = lnd_gst_need, .seek_data = lnd_gst_input_seek};
    gst_app_src_set_callbacks(GST_APP_SRC(s->source), &callbacks, s, nullptr);
    g_signal_connect(elements[1], "pad-added", G_CALLBACK(lnd_gst_pad), s);
    g_signal_connect(elements[1], "autoplug-select", G_CALLBACK(lnd_gst_select), s);
    if (!gst_element_link(s->source, elements[1]) || !gst_element_link(s->convert, s->sink) ||
        gst_element_set_state(s->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE || !lnd_gst_pull(s))
        return false;
    return true;
}

static uint64_t lnd_gst_duration(lnd_gst_state *s) {
    gint64 duration = 0;
    if (!gst_element_query_duration(s->sink, GST_FORMAT_TIME, &duration)) duration = 0;
    if (duration <= 0 && !gst_element_query_duration(s->pipeline, GST_FORMAT_TIME, &duration)) duration = 0;
    return duration > 0 ? lnd_system_frames_ceil((uint64_t)duration, s->sample_rate_hz, GST_SECOND) : 0;
}

static int32_t lnd_gst_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    *state = nullptr;
    if (LND_IoGetSizeBytes(io) > INT64_MAX) return LND_ERR_UNSUPPORTED;
    if (!gst_init_check(nullptr, nullptr, nullptr)) return LND_ERR_EXTERNAL;
    lnd_gst_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    g_mutex_init(&s->io_lock);
    if (!lnd_gst_setup(s)) goto fail;
    uint64_t length = lnd_gst_duration(s);
    if (!length) {
        uint64_t scanned = s->map.size / s->frame_bytes;
        gint64 start = g_get_monotonic_time();
        for (unsigned i = 0; i < 128 && scanned < (uint64_t)s->sample_rate_hz * 2 && g_get_monotonic_time() - start < 1000000; i++) {
            if (!lnd_gst_pull(s)) {
                if (s->failed) goto fail;
                length = scanned;
                break;
            }
            scanned += s->map.size / s->frame_bytes;
            length = lnd_gst_duration(s);
            if (length) break;
        }
        lnd_gst_dispose(s);
        if (!lnd_gst_setup(s)) goto fail;
    }
    GstQuery *query = gst_query_new_seeking(GST_FORMAT_TIME);
    gboolean seekable = FALSE;
    if (gst_element_query(s->pipeline, query)) gst_query_parse_seeking(query, nullptr, &seekable, nullptr, nullptr);
    gst_query_unref(query);
    *info = (LND_CODEC_INFO){.format = LND_FORMAT_F32,
                             .channels = s->channels,
                             .sample_rate_hz = s->sample_rate_hz,
                             .length_frames = length,
                             .seekable = seekable && LND_IoCanSeek(io),
                             .length_estimated = true};
    *state = s;
    return LND_OK;
fail:
    lnd_gst_close(s);
    return LND_ERR_FORMAT;
}

static uint64_t lnd_gst_read(void *state, void *dst, uint64_t frames) {
    lnd_gst_state *s = state;
    if (!frames) return 0;
    if (s->failed || frames > SIZE_MAX / s->frame_bytes) return LND_CODEC_READ_ERROR;
    uint64_t done = 0;
    while (done < frames) {
        if ((!s->mapped || s->offset == s->map.size) && !lnd_gst_pull(s)) break;
        size_t count = (size_t)LND_MIN(frames - done, (s->map.size - s->offset) / s->frame_bytes);
        size_t bytes = count * s->frame_bytes;
        memcpy((uint8_t *)dst + (size_t)done * s->frame_bytes, s->map.data + s->offset, bytes);
        s->offset += bytes;
        done += count;
    }
    return s->failed ? LND_CODEC_READ_ERROR : done;
}

static int32_t lnd_gst_seek(void *state, uint64_t frame) {
    lnd_gst_state *s = state;
    uint64_t time = lnd_system_frames(frame, GST_SECOND, s->sample_rate_hz);
    if (time > INT64_MAX) return LND_ERR_INVALID_ARG;
    if (frame && frame % s->sample_rate_hz * GST_SECOND % s->sample_rate_hz) time++;
    if (time > INT64_MAX) return LND_ERR_INVALID_ARG;
    if (!frame || g_atomic_int_get(&s->linear_seek)) {
        lnd_gst_dispose(s);
        s->failed = false;
        if (!lnd_gst_setup(s)) {
            s->failed = true;
            return LND_ERR_IO;
        }
        while (frame) {
            if ((!s->mapped || s->offset == s->map.size) && !lnd_gst_pull(s)) {
                int32_t result = s->failed ? LND_ERR_IO : LND_ERR_INVALID_ARG;
                s->failed = true;
                return result;
            }
            size_t count = (size_t)LND_MIN(frame, (s->map.size - s->offset) / s->frame_bytes);
            s->offset += count * s->frame_bytes;
            frame -= count;
        }
    } else {
        if (s->failed || !s->pipeline) return LND_ERR_IO;
        if (!gst_element_seek_simple(s->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, (gint64)time)) return LND_ERR_IO;
        if (gst_element_get_state(s->pipeline, nullptr, nullptr, 5 * GST_SECOND) != GST_STATE_CHANGE_SUCCESS) {
            s->failed = true;
            return LND_ERR_IO;
        }
        lnd_gst_release(s);
        s->failed = false;
    }
    return LND_OK;
}

const LND_CODEC lnd_codec_gstreamer = {
    .name = "gstreamer",
    .extensions = "au;snd;aac;adts;m4a;m4b;mp4;mov;avi;mp3;wav;aif;aiff;aifc;caf;ogg;opus;flac;alac;wma;asf;amr;3gp;3g2;mkv;mka;webm;ac3;ec3;ts",
    .flags = LND_CODEC_FLAG_SYSTEM,
    .probe = lnd_system_probe,
    .open = lnd_gst_open,
    .read = lnd_gst_read,
    .seek = lnd_gst_seek,
    .close = lnd_gst_close};

const LND_CODEC *LND_GStreamerGetCodec(void) { return &lnd_codec_gstreamer; }
