#include "http.h"
#if LND_MODULE_METADATA_ID3V2
#include "lindar_metadata_id3v2.h"
#endif
#include "metadata/id3/id3.h"
#include "utility/text/text.h"

#include <stdio.h>
#include <string.h>

void lnd_http_metadata_at(lnd_http_session *s, const char *title, int64_t time_us, bool estimated) {
    if (!title) return;
    if (s->metadata_count == s->options.buffer.event_count) {
#if LND_MODULE_METADATA
        LND_MetadataFree(s->metadata[0].tags);
#endif
        memmove(s->metadata, s->metadata + 1, (s->metadata_count - 1) * sizeof *s->metadata);
        s->metadata_count--;
        s->stats.events_lost++;
    }
    lnd_http_metadata *m = &s->metadata[s->metadata_count++];
    *m = (lnd_http_metadata){.time_us = LND_MAX(time_us, 0), .estimated = estimated};
    snprintf(m->title, sizeof m->title, "%s", title);
}

void lnd_http_metadata_icy(lnd_http_session *s, const char *title) {
    if (!title || !*title) return;
    lnd_http_metadata_at(s, title, 0, true);
    if (s->metadata_count) {
        lnd_http_metadata *m = &s->metadata[s->metadata_count - 1];
        m->encoded = true;
        m->boundary = lnd_decoder_fed(s->decoder);
    }
}

void lnd_http_metadata_update(lnd_http_session *s) {
    if (!s->info.sample_rate_hz) return;
    uint64_t played = lnd_load(&s->played);
    int64_t now = (int64_t)(played / s->info.sample_rate_hz * 1000000 + played % s->info.sample_rate_hz * 1000000 / s->info.sample_rate_hz);
    for (uint32_t i = 0; i < s->metadata_count;) {
        lnd_http_metadata *m = &s->metadata[i];
        if (m->encoded && lnd_decoder_consumed(s->decoder) >= m->boundary) {
            lnd_spinlock_lock(&s->pcm_lock);
            uint64_t end = lnd_load(&s->played) + s->count;
            lnd_spinlock_unlock(&s->pcm_lock);
            m->time_us = (int64_t)(end / s->info.sample_rate_hz * 1000000 + end % s->info.sample_rate_hz * 1000000 / s->info.sample_rate_hz);
            m->encoded = false;
        }
        if (!m->encoded && m->time_us <= now) {
#if LND_MODULE_METADATA
            if (m->tags) {
                LND_MetadataFree(s->tags);
                s->tags = m->tags;
                s->tags_status = LND_OK;
            } else if (*m->title) {
                if (!s->tags) s->tags = LND_MetadataCreate(nullptr);
                if (s->tags) LND_MetadataSetValue(s->tags, "TITLE", m->title);
            }
#endif
            snprintf(s->info.title, sizeof s->info.title, "%s", m->title);
            lnd_http_event(s, LND_HTTP_EVENT_METADATA, LND_OK, m->title);
            uint32_t at = (s->event_head + s->event_count - 1) % s->options.buffer.event_count;
            s->events[at].position_us = m->time_us;
            s->events[at].estimated = m->estimated;
            memmove(m, m + 1, (s->metadata_count - i - 1) * sizeof *m);
            s->metadata_count--;
        } else
            i++;
    }
}

void lnd_http_id3(lnd_http_session *s, const uint8_t *data, size_t bytes, int64_t time_us) {
    lnd_id3_tag tag;
    if (lnd_id3_header(data, bytes, &tag) != LND_OK) return;
    if (lnd_id3_body(&tag) != LND_OK) {
        lnd_id3_close(&tag);
        return;
    }
    lnd_id3_iterator iterator = {.data = tag.body, .size = tag.size, .version = tag.version, .unsynchronized = tag.version == 4 && (tag.flags & 0x80)};
    char title[512] = {0};
    bool timestamp = false;
    uint64_t pts = 0;
    lnd_id3_frame frame;
    int32_t result;
    while ((result = lnd_id3_next(&iterator, &frame)) > 0) {
        if (frame.opaque) continue;
        const uint8_t *value = frame.data;
        size_t size = frame.size;
        if (!strcmp(frame.id, "TIT2") || !strcmp(frame.id, "TT2")) {
            if (!size || value[0] > (tag.version < 4 ? 1u : 3u)) {
                result = LND_ERR_FORMAT;
                break;
            }
            unsigned encoding = *value++;
            size = lnd_text_terminator(value, size - 1, encoding);
            result = lnd_text_decode(value, size, encoding, title, sizeof title);
            if (result) break;
        } else if (!strcmp(frame.id, "PRIV")) {
            static const char owner[] = "com.apple.streaming.transportStreamTimestamp";
            if (size == sizeof owner + 8 && !memcmp(value, owner, sizeof owner)) {
                pts = 0;
                for (unsigned i = 0; i < 8; i++)
                    pts = pts << 8 | value[sizeof owner + i];
                pts &= (1ull << 33) - 1;
                timestamp = true;
            }
        }
    }
    lnd_id3_iterator_close(&iterator);
    lnd_id3_close(&tag);
    if (result < 0) return;
    if (timestamp) {
        if (!s->metadata_clock) {
            s->metadata_pts = pts;
            s->metadata_time_us = time_us;
            s->metadata_clock = true;
        }
        int64_t delta = (int64_t)pts - (int64_t)s->metadata_pts;
        if (delta > (1ll << 32)) delta -= 1ll << 33;
        if (delta < -(1ll << 32)) delta += 1ll << 33;
        time_us = s->metadata_time_us + delta / 90000 * 1000000 + delta % 90000 * 1000000 / 90000;
    }
    lnd_http_metadata_at(s, title, time_us, !timestamp);
#if LND_MODULE_METADATA_ID3V2
    if (s->metadata_count) {
        LND_METADATA *metadata = LND_MetadataCreate(nullptr);
        if (metadata && !LND_MetadataId3v2Read(metadata, data, bytes, nullptr))
            s->metadata[s->metadata_count - 1].tags = metadata;
        else
            LND_MetadataFree(metadata);
    }
#endif
}

void lnd_http_metadata_clear(lnd_http_session *s) {
#if LND_MODULE_METADATA
    for (uint32_t i = 0; i < s->metadata_count; ++i)
        LND_MetadataFree(s->metadata[i].tags);
#endif
    s->metadata_count = 0;
}

void lnd_http_decoder_metadata(lnd_http_session *s) {
#if LND_MODULE_METADATA
    LND_DecoderLoadMetadata(s->decoder);
    uint64_t revision = LND_DecoderGetMetadataRevision(s->decoder);
    if (!revision || revision == s->tags_revision) return;
    s->tags_revision = revision;
    LND_METADATA *metadata = LND_MetadataCreate(nullptr);
    s->tags_status = metadata ? LND_DecoderCopyMetadata(s->decoder, metadata) : LND_ERR_OUT_OF_MEMORY;
    if (s->tags_status) {
        LND_MetadataFree(metadata);
        return;
    }
    if (!s->tags) {
        s->tags = metadata;
        return;
    }
    const char *title = LND_MetadataGetValue(metadata, "TITLE", 0);
    if (!title) title = LND_MetadataGetValue(metadata, "TIT2", 0);
    lnd_spinlock_lock(&s->pcm_lock);
    uint64_t frames = lnd_load(&s->played) + s->count;
    lnd_spinlock_unlock(&s->pcm_lock);
    int64_t time = s->info.sample_rate_hz ? (int64_t)(frames * 1000000 / s->info.sample_rate_hz) : 0;
    lnd_http_metadata_at(s, title ? title : "", time, false);
    s->metadata[s->metadata_count - 1].tags = metadata;
#endif
}
