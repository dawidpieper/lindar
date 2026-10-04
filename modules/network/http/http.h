#pragma once

#include "lindar_http.h"
#include "lindar_decode.h"
#include "src/alloc.h"
#include "src/atomic.h"
#include "src/spinlock.h"
#include "src/thread.h"
#include "src/context.h"
#include "src/config.h"
#include "src/error.h"
#include "formats/decode/stream.h"
#include "pcm/audio/source.h"
#if LND_MODULE_METADATA
#include "metadata/internal.h"
#endif

#define LND_HTTP_URL_MAX 4096

typedef struct lnd_http_bytes {
    uint8_t *data;
    size_t size;
    size_t capacity;
    size_t limit;
} lnd_http_bytes;

typedef struct lnd_http_metadata {
#if LND_MODULE_METADATA
    LND_METADATA *tags;
#endif
    char title[512];
    uint64_t boundary;
    int64_t time_us;
    bool encoded;
    bool estimated;
} lnd_http_metadata;

typedef struct lnd_http_session {
    struct lnd_http_session *next;
    LND_HTTP_OPTIONS options;
    LND_HTTP_HEADER *headers;
    char *url;
    char *request_url;
    char if_range[256];
    LND_SOURCE *source;
    LND_DECODER *decoder;
#if LND_MODULE_METADATA
    LND_METADATA *tags;
    uint64_t tags_revision;
    int32_t tags_status;
#endif
    LND_CODEC_INFO format;
    lnd_source decode_source;
    lnd_source *resampler;
    LND_HTTP_INFO info;
    LND_HTTP_STATS stats;
    LND_HTTP_RESPONSE response;
    const LND_HTTP_TRANSPORT *transport;
    void *transfer;
    void *transport_user;
    bool transport_owned;
    void *protocol;
    lnd_http_bytes input;
    size_t input_offset;
    float *ring;
    float *scratch;
    uint32_t scratch_channels;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
    lnd_spinlock pcm_lock;
    lnd_atomic_u32 detached;
    lnd_atomic_i32 terminal;
    lnd_atomic_u32 stalled;
    lnd_atomic_u64 played;
    uint64_t discard_frames;
    int64_t discard_us;
    int64_t seek_target_us;
    bool seek_commit;
    bool discard_round;
    uint64_t now;
    uint64_t open_deadline;
    uint64_t retry_at;
    uint64_t last_data;
    uint64_t request_id;
    uint32_t attempts;
    uint32_t icy_remaining;
    uint32_t icy_size;
    uint32_t icy_received;
    char icy[4081];
    LND_HTTP_EVENT *events;
    lnd_http_metadata *metadata;
    uint32_t metadata_count;
    uint64_t metadata_pts;
    int64_t metadata_time_us;
    bool metadata_clock;
    uint32_t event_head;
    uint32_t event_count;
    uint64_t overflow_reported;
    bool variant_changed;
    bool worker;
    bool probe_duration;
    bool cancelled;
    bool finished;
    bool input_end;
    bool decoder_end;
    bool ready;
    bool buffering;
} lnd_http_session;

struct LND_HTTP_OPEN {
    lnd_http_session *session;
    struct LND_HTTP_OPEN *next;
};

bool lnd_http_bytes_append(lnd_http_bytes *buffer, const void *data, size_t bytes);
void lnd_http_bytes_free(lnd_http_bytes *buffer);
char *lnd_http_copy(const char *text);
int32_t lnd_http_url_resolve(const char *base, const char *relative, char *out, size_t capacity);
bool lnd_http_same_origin(const char *a, const char *b);
bool lnd_http_url_valid(const char *url);
void lnd_http_event(lnd_http_session *s, int32_t type, int32_t result, const char *text);
void lnd_http_metadata_at(lnd_http_session *s, const char *title, int64_t time_us, bool estimated);
void lnd_http_metadata_icy(lnd_http_session *s, const char *title);
void lnd_http_metadata_update(lnd_http_session *s);
void lnd_http_id3(lnd_http_session *s, const uint8_t *data, size_t bytes, int64_t time_us);
void lnd_http_state(lnd_http_session *s, int32_t state);
void lnd_http_fail(lnd_http_session *s, int32_t error, const char *message);
int32_t lnd_http_request_open(lnd_http_session *s, const char *url, const char *validator, bool range, uint64_t start, uint64_t length, void **transfer);
int32_t lnd_http_request(lnd_http_session *s, const char *url, bool range, uint64_t start, uint64_t length);
int32_t lnd_http_poll(lnd_http_session *s, void *data, size_t capacity, size_t *written);
void lnd_http_request_close(lnd_http_session *s);
int32_t lnd_http_protocol_step(lnd_http_session *s, uint32_t budget);
size_t lnd_http_protocol_buffered(const lnd_http_session *s);
bool lnd_http_protocol_complete(const lnd_http_session *s);
void lnd_http_protocol_free(lnd_http_session *s);
int32_t lnd_http_protocol_seek(lnd_http_session *s, int64_t time_us, bool live);
int32_t lnd_http_decode(lnd_http_session *s);
int32_t lnd_http_feed(lnd_http_session *s, const uint8_t *data, size_t bytes, size_t *used);

void lnd_http_metadata_clear(lnd_http_session *s);
void lnd_http_decoder_metadata(lnd_http_session *s);
