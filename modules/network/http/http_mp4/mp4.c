#include "network/http/http.h"
#include "mp4.h"
#include "formats/mp4/read.h"
#include "network/http/http_packets/packets.h"
#include <string.h>

struct lnd_http_mp4 {
    char *url;
    LND_DEMUX *demux;
    lnd_http_bytes cache;
    lnd_http_bytes packet;
    uint64_t length;
    uint64_t scan;
    uint64_t base;
    uint64_t requested;
    uint64_t count;
    uint64_t index;
    uint64_t retry_at;
    size_t packet_at;
    uint32_t boxes;
    uint32_t attempts;
    bool pending;
};

static int32_t lnd_mp4_http_retry(lnd_http_session *s, lnd_http_mp4 *m, int32_t result) {
    bool retryable = result == LND_ERR_IO &&
                     (!s->response.status || s->response.status == 206 || s->response.status == 408 || s->response.status == 429 || s->response.status >= 500);
    if (!retryable || m->attempts >= s->options.retry.attempts) return result;
    uint64_t delay = s->options.retry.delay_ms;
    for (uint32_t i = 0; i < m->attempts; i++) delay = LND_MIN(delay * 2, s->options.retry.max_delay_ms);
    m->retry_at = s->now + LND_MAX(delay, s->response.retry_after_ms);
    m->attempts++;
    s->stats.reconnects++;
    m->pending = false;
    m->cache.size = 0;
    lnd_http_request_close(s);
    lnd_http_state(s, LND_HTTP_RECONNECTING);
    return LND_HTTP_PENDING;
}

static int32_t lnd_mp4_http_ensure(lnd_http_session *s, lnd_http_mp4 *m, uint64_t offset, size_t bytes) {
    if (offset > m->length || bytes > m->length - offset || bytes > m->cache.limit) return LND_ERR_FORMAT;
    if (offset >= m->base && offset - m->base <= m->cache.size && bytes <= m->cache.size - (size_t)(offset - m->base)) return LND_OK;
    if (s->now < m->retry_at) return LND_HTTP_PENDING;
    if (!m->pending || offset < m->base || offset - m->base > m->requested || bytes > m->requested - (offset - m->base)) {
        m->base = offset;
        m->cache.size = 0;
        m->requested = LND_MIN(m->length - offset, LND_MAX(bytes, LND_MIN((size_t)65536, m->cache.limit)));
        int32_t r = lnd_http_request(s, m->url, true, offset, m->requested);
        if (r != LND_OK) return lnd_mp4_http_retry(s, m, r);
        m->pending = true;
    }
    uint8_t data[16384];
    size_t written = 0;
    int32_t r = lnd_http_poll(s, data, sizeof data, &written);
    if (written > sizeof data) return LND_ERR_IO;
    if (s->response.headers_complete &&
        (s->response.status != 206 || !s->response.range || s->response.range_start_bytes != m->base || s->response.total_length_bytes != m->length ||
         strcmp(s->response.etag, s->if_range) || (s->response.length_known && s->response.content_length_bytes != m->requested)))
        return lnd_mp4_http_retry(s, m, LND_ERR_IO);
    if (written) {
        if (!s->response.headers_complete || written > m->requested - m->cache.size) return LND_ERR_IO;
        if (!lnd_http_bytes_append(&m->cache, data, written)) return LND_ERR_OUT_OF_MEMORY;
        s->stats.received_bytes += written;
        s->last_data = s->now;
    }
    if (r < 0) return lnd_mp4_http_retry(s, m, r);
    if (r == LND_HTTP_DONE) {
        if (m->cache.size != m->requested) return lnd_mp4_http_retry(s, m, LND_ERR_IO);
        m->pending = false;
        m->attempts = 0;
        lnd_http_request_close(s);
    } else if (!written && s->options.retry.receive_timeout_ms && s->now - s->last_data >= s->options.retry.receive_timeout_ms)
        return lnd_mp4_http_retry(s, m, LND_ERR_IO);
    return offset >= m->base && offset - m->base <= m->cache.size && bytes <= m->cache.size - (size_t)(offset - m->base) ? LND_OK : LND_HTTP_PENDING;
}

static int32_t lnd_mp4_http_index(lnd_http_session *s, lnd_http_mp4 *m) {
    int32_t r = lnd_mp4_http_ensure(s, m, m->scan, 8);
    if (r != LND_OK) return r;
    const uint8_t *p = m->cache.data + (size_t)(m->scan - m->base);
    size_t header_bytes = lnd_mp4_u32(p) == 1 ? 16 : 8;
    if (header_bytes == 16) {
        r = lnd_mp4_http_ensure(s, m, m->scan, header_bytes);
        if (r != LND_OK) return r;
        p = m->cache.data + (size_t)(m->scan - m->base);
    }
    lnd_mp4_header header;
    if (!lnd_mp4_box_bounds(p, header_bytes, m->length - m->scan, &header)) return LND_ERR_FORMAT;
    uint64_t size = header.size;
    bool moov = header.type == LND_MP4_TAG('m', 'o', 'o', 'v');
    if (!moov) {
        if (++m->boxes > 65536 || size == m->length - m->scan) return LND_ERR_UNSUPPORTED;
        m->scan += size;
        return LND_HTTP_PENDING;
    }
    if (size > m->cache.limit || size > SIZE_MAX) return LND_ERR_UNSUPPORTED;
    r = lnd_mp4_http_ensure(s, m, m->scan, (size_t)size);
    if (r != LND_OK) return r;
    r = LND_DemuxSetInit(m->demux, m->cache.data + (size_t)(m->scan - m->base), (size_t)size);
    if (r != LND_OK) return r;
    m->count = LND_DemuxGetSampleCount(m->demux);
    if (!m->count) return LND_ERR_UNSUPPORTED;
    int64_t previous = INT64_MIN, end = 0;
    for (uint64_t i = 0; i < m->count; i++) {
        LND_DEMUX_SAMPLE sample;
        r = LND_DemuxGetSample(m->demux, i, &sample);
        if (r == LND_DEMUX_END) {
            m->count = i;
            break;
        }
        if (r != LND_OK) return r;
        LND_DEMUX_PACKET *packet = &sample.packet;
        if (sample.offset_bytes > m->length || packet->bytes > m->length - sample.offset_bytes || packet->time_us < previous ||
            packet->time_us > INT64_MAX - packet->duration_us)
            return LND_ERR_FORMAT;
        previous = packet->time_us;
        end = packet->time_us + packet->duration_us - (int64_t)((uint64_t)packet->trim_end_frames * 1000000 / packet->sample_rate_hz);
    }
    if (!m->count || end <= 0) return LND_ERR_FORMAT;
    s->info.duration_us = s->info.seek_end_us = end;
    s->info.length_kind = LND_LENGTH_EXACT;
    s->info.seekable = true;
    return LND_OK;
}

int32_t lnd_http_mp4_begin(lnd_http_session *s, const uint8_t *data, size_t bytes, lnd_http_mp4 **out) {
    lnd_http_mp4 *m = lnd_alloc_zero(sizeof *m);
    if (!m) return LND_ERR_OUT_OF_MEMORY;
    m->url = lnd_http_copy(s->request_url);
    m->length = s->response.content_length_bytes;
    m->cache.limit = s->options.buffer.segment_bytes;
    m->packet.limit = s->options.buffer.compressed_bytes;
    LND_DEMUX_OPTIONS options = {.packet_bytes = m->cache.limit};
    m->demux = LND_DemuxCreate(&options);
    if (!m->url || !m->demux || !lnd_http_bytes_append(&m->cache, data, bytes)) {
        lnd_http_mp4_free(m);
        return LND_ERR_OUT_OF_MEMORY;
    }
    lnd_http_request_close(s);
    *out = m;
    return LND_HTTP_PENDING;
}

int32_t lnd_http_mp4_step(lnd_http_session *s, lnd_http_mp4 *m, uint32_t budget) {
    for (uint32_t i = 0; i < budget; i++) {
        if (!m->count) {
            int32_t r = lnd_mp4_http_index(s, m);
            if (r != LND_OK) return r;
        }
        if (m->packet_at < m->packet.size) {
            int64_t fed = LND_DecoderFeed(s->decoder, m->packet.data + m->packet_at, m->packet.size - m->packet_at);
            if (fed < 0) return (int32_t)fed;
            m->packet_at += (size_t)fed;
            int32_t r = lnd_http_decode(s);
            if (r < 0) return r;
            if (m->packet_at < m->packet.size) return LND_HTTP_PENDING;
            m->packet_at = m->packet.size = 0;
        }
        int32_t r = lnd_http_decode(s);
        if (r < 0) return r;
        if (s->finished) {
            lnd_http_request_close(s);
            return LND_HTTP_DONE;
        }
        lnd_spinlock_lock(&s->pcm_lock);
        bool full = s->capacity && s->count == s->capacity;
        lnd_spinlock_unlock(&s->pcm_lock);
        if (full) return LND_HTTP_PENDING;
        if (m->index == m->count) {
            if (!s->input_end) {
                LND_DecoderEnd(s->decoder);
                s->input_end = true;
            }
            continue;
        }
        LND_DEMUX_SAMPLE sample;
        r = LND_DemuxGetSample(m->demux, m->index, &sample);
        if (r != LND_OK) return r == LND_DEMUX_END ? LND_ERR_FORMAT : r;
        r = lnd_mp4_http_ensure(s, m, sample.offset_bytes, sample.packet.bytes);
        if (r != LND_OK) return r;
        sample.packet.data = m->cache.data + (size_t)(sample.offset_bytes - m->base);
        r = lnd_http_packet(s, &sample.packet, &m->packet);
        if (r != LND_OK) return r;
        m->index++;
    }
    return LND_HTTP_PENDING;
}

int32_t lnd_http_mp4_seek(lnd_http_session *s, lnd_http_mp4 *m, int64_t time_us) {
    if (!m->count || time_us > s->info.duration_us) return LND_ERR_INVALID_ARG;
    uint64_t low = 0, high = m->count;
    LND_DEMUX_SAMPLE first;
    if (LND_DemuxGetSample(m->demux, 0, &first) != LND_OK) return LND_ERR_FORMAT;
    const uint8_t *config = first.packet.config;
    int64_t preroll = !strcmp(first.packet.codec, "aac") && first.packet.config_bytes && config[0] >> 3 != 2 ? 750000 : 120000;
    int64_t target = LND_MAX(time_us - preroll, 0);
    while (low < high) {
        uint64_t middle = low + (high - low) / 2;
        LND_DEMUX_SAMPLE sample;
        if (LND_DemuxGetSample(m->demux, middle, &sample) != LND_OK) return LND_ERR_FORMAT;
        if (sample.packet.time_us <= target) low = middle + 1;
        else high = middle;
    }
    m->index = target && low ? low - 1 : 0;
    LND_DEMUX_SAMPLE sample;
    if (LND_DemuxGetSample(m->demux, m->index, &sample) != LND_OK) return LND_ERR_FORMAT;
    m->packet_at = m->packet.size = 0;
    m->attempts = 0;
    m->retry_at = 0;
    lnd_decoder_reset(s->decoder);
    if (s->resampler) lnd_resample_source_reset(s->resampler);
    lnd_store(&s->decode_source.status, LND_SOURCE_WAITING);
    s->decode_source.pos = 0;
    s->input_end = s->decoder_end = s->finished = false;
    s->discard_us = time_us - LND_MAX(sample.packet.time_us, 0);
    s->discard_frames = 0;
    s->discard_round = true;
    s->seek_target_us = time_us;
    s->seek_commit = true;
    lnd_http_state(s, LND_HTTP_SEEKING);
    return LND_OK;
}

size_t lnd_http_mp4_buffered(const lnd_http_mp4 *m) { return m ? m->cache.size + m->packet.size - m->packet_at : 0; }

void lnd_http_mp4_free(lnd_http_mp4 *m) {
    if (!m) return;
    LND_DemuxFree(m->demux);
    lnd_http_bytes_free(&m->cache);
    lnd_http_bytes_free(&m->packet);
    lnd_free(m->url);
    lnd_free(m);
}
