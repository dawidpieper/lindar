#pragma once

#include "lindar.h"
#include "lindar_http_file.h"
#include "lindar_http_hls.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Key "http.probe_duration": Probe finite Ogg duration; session flags override it (default 1). */
LND_API extern LND_CONFIG_KEY *const LND_CFG_HTTP_PROBE_DURATION;

/** Pending HTTP opening operation; take its source or cancel, then release with HttpOpenFree. */
typedef struct LND_HTTP_OPEN LND_HTTP_OPEN;

/** Transport callback table borrowed for the session lifetime; optional session callbacks share transport
 * state. Duration probing may keep a second transfer active in the same session.
 */
typedef struct LND_HTTP_TRANSPORT LND_HTTP_TRANSPORT;

enum {
    LND_HTTP_ERR_TIMEOUT = -50, /**< The total HTTP opening deadline expired before playback became ready. */
    LND_HTTP_PENDING = 1, /**< Operation/event is not ready; call again after progress. */
    LND_HTTP_DONE = 2 /**< Transport has finished delivering its response body. */
};
enum {
    LND_HTTP_AUTO, /**< Infer finite/live behaviour from the response/protocol. */
    LND_HTTP_FINITE, /**< Treat input as finite content. */
    LND_HTTP_LIVE /**< Treat input as an ongoing live stream. */
};
enum {
    LND_HTTP_EXEC_AUTO, /**< Choose worker/manual execution from library configuration. */
    LND_HTTP_EXEC_MANUAL, /**< Drive progress through HttpUpdate or LibraryUpdate. */
    LND_HTTP_EXEC_WORKER /**< Drive transfers and decoding on a background worker shared by HTTP sessions. */
};
enum {
    LND_HTTP_CONNECTING, /**< Waiting for the transport connection/headers. */
    LND_HTTP_PROBING, /**< Detecting container and codec. */
    LND_HTTP_BUFFERING, /**< Collecting enough PCM to start/resume. */
    LND_HTTP_READY, /**< Playback PCM is available. */
    LND_HTTP_RECONNECTING, /**< Retrying an interrupted transfer. */
    LND_HTTP_SEEKING, /**< Applying a requested seek. */
    LND_HTTP_DRAINING, /**< Encoded input ended; decoded PCM is still draining. */
    LND_HTTP_ENDED, /**< Playback input and buffered PCM are exhausted. */
    LND_HTTP_CANCELLED, /**< The caller cancelled the stream. */
    LND_HTTP_FAILED /**< The stream stopped with an error. */
};
enum {
    LND_HTTP_EVENT_STATE, /**< Connection/playback state changed. */
    LND_HTTP_EVENT_METADATA, /**< Stream metadata changed. */
    LND_HTTP_EVENT_SEEK, /**< A seek request produced a result. */
    LND_HTTP_EVENT_DISCONTINUITY, /**< A stream continuity boundary was crossed. */
    LND_HTTP_EVENT_VARIANT, /**< Selected HLS variant/rendition changed. */
    LND_HTTP_EVENT_ERROR, /**< A transfer or decoding error was reported. */
    LND_HTTP_EVENT_OVERFLOW /**< Events were lost because the queue was full. */
};
enum {
    LND_HTTP_CAP_HTTP = 1u << 0, /**< Plain HTTP transport is available. */
    LND_HTTP_CAP_HTTPS = 1u << 1, /**< TLS HTTP transport is available. */
    LND_HTTP_CAP_ICY = 1u << 2, /**< ICY metadata handling is available. */
    LND_HTTP_CAP_HLS = 1u << 3, /**< HLS playback is available. */
    LND_HTTP_CAP_LL_HLS = 1u << 4, /**< Low-latency HLS support is available. */
    LND_HTTP_CAP_AES128 = 1u << 5, /**< AES-128 HLS segment decryption is available. */
    LND_HTTP_CAP_MANUAL = 1u << 6, /**< Caller-driven transport polling is supported. */
    LND_HTTP_CAP_WORKER = 1u << 7, /**< Worker-driven transport polling is supported. */
    LND_HTTP_CAP_MP4_RANGE = 1u << 8, /**< MP4 ranges need known length, byte-range support and a matching strong ETag. */
    LND_HTTP_CAP_FILE_CACHE = 1u << 9 /**< Temporary file caching is available. */
};
enum {
    LND_HTTP_HEADER_SENSITIVE = 1u << 0 /**< Restrict this header to its permitted origin across redirects. */
};
enum {
    LND_HTTP_NO_USER_AGENT = 1u << 0, /**< Suppress the default User-Agent header. */
    LND_HTTP_ALLOW_HTTP_REDIRECT = 1u << 1, /**< Permit an HTTPS request to redirect to plain HTTP. */
    LND_HTTP_RESUME_LIVE = 1u << 2, /**< Allow reconnection/resumption of live input. */
    LND_HTTP_PROBE_DURATION = 1u << 3, /**< Enable finite Ogg duration probing for this session. */
    LND_HTTP_NO_PROBE_DURATION = 1u << 4 /**< Disable duration probing; mutually exclusive with PROBE_DURATION. */
};

/** One request header; origin and flags restrict forwarding of sensitive values. */
typedef struct LND_HTTP_HEADER {
    const char *name; /**< Header name without a colon. */
    const char *value; /**< Header value without line breaks. */
    const char *origin; /**< Optional origin restricting where this header may be sent. */
    uint32_t flags; /**< LND_HTTP_HEADER_SENSITIVE or zero. */
} LND_HTTP_HEADER;

/** HTTP buffering thresholds and allocation limits; time values are milliseconds. */
typedef struct LND_HTTP_BUFFER_OPTIONS {
    uint32_t start_ms; /**< Decoded audio required before initial playback. */
    uint32_t resume_ms; /**< Decoded audio required after a stall. */
    uint32_t pcm_ms; /**< Decoded PCM queue capacity in milliseconds. */
    size_t compressed_bytes; /**< Compressed decoder input capacity in bytes. */
    size_t segment_bytes; /**< Maximum segment or Ogg cache size in bytes; also limits complete Ogg responses. */
    size_t playlist_bytes; /**< Maximum downloaded playlist size in bytes. */
    uint32_t playlist_entries; /**< Maximum parsed playlist entries. */
    uint32_t event_count; /**< Maximum queued HTTP events. */
    uint32_t dvr_ms; /**< Requested live rewind window in milliseconds. */
} LND_HTTP_BUFFER_OPTIONS;

/** Reconnect backoff and transfer timeouts, in milliseconds. */
typedef struct LND_HTTP_RETRY_OPTIONS {
    uint32_t attempts; /**< Maximum retry attempts. */
    uint32_t delay_ms; /**< Initial reconnect delay in milliseconds. */
    uint32_t max_delay_ms; /**< Maximum reconnect backoff in milliseconds. */
    uint32_t connect_timeout_ms; /**< Connection timeout in milliseconds. */
    uint32_t receive_timeout_ms; /**< Receive inactivity timeout in milliseconds. */
} LND_HTTP_RETRY_OPTIONS;

/** HTTP session settings; initialise with HttpOptionsInit. Strings are copied; transport and
 * transport_user remain borrowed until session cleanup; see LND_HttpUpdate.
 */
typedef struct LND_HTTP_OPTIONS {
    uint32_t size; /**< sizeof(LND_HTTP_OPTIONS), set by HttpOptionsInit. */
    uint32_t flags; /**< LND_HTTP policy bits; absent duration flags inherit CFG at HttpOpen. */
    const char *user_agent; /**< User-Agent string; NULL uses the default unless disabled by flags. */
    const LND_HTTP_HEADER *headers; /**< Array of request headers. */
    size_t header_count; /**< Number of entries in headers. */
    const char *codec_name; /**< Force one decoder without fallback; NULL selects automatically. */
    const char *proxy; /**< Optional proxy URL. */
    const char *ca_file; /**< Optional CA certificate file path for TLS verification. */
    uint32_t output_sample_rate_hz; /**< Requested output Hz; zero preserves the decoded rate. */
    uint32_t output_channels; /**< Requested output channels; zero preserves the decoded count. */
    int32_t content_mode; /**< LND_HTTP_AUTO, FINITE or LIVE. */
    int32_t execution; /**< LND_HTTP_EXEC_AUTO, MANUAL or WORKER. */
    uint32_t max_redirects; /**< Maximum followed redirects. */
    uint32_t open_timeout_ms; /**< Total opening deadline through initial buffering, including retries; 0 disables it (default). */
    LND_HTTP_BUFFER_OPTIONS buffer; /**< Buffer thresholds and capacity limits. */
    LND_HTTP_RETRY_OPTIONS retry; /**< Retry counts, backoff and timeouts. */
    LND_HTTP_HLS_OPTIONS hls; /**< HLS variant and live latency policy. */
    LND_HTTP_FILE_OPTIONS file; /**< Temporary file cache policy. */
    const LND_HTTP_TRANSPORT *transport; /**< Borrowed custom transport; NULL uses the compiled default. */
    void *transport_user; /**< Borrowed context passed to transport session/open callbacks. */
} LND_HTTP_OPTIONS;

/** Copied Lindar, HTTP and transport error details; a zero code means no recorded error. */
typedef struct LND_HTTP_ERROR {
    int32_t code; /**< Lindar result code; zero means no error. */
    int32_t http_status; /**< HTTP status code, if a response was received. */
    int32_t backend_code; /**< Transport-specific result code. */
    bool retryable; /**< Retry policy permits another attempt. */
    char message[192]; /**< NUL-terminated diagnostic text. */
} LND_HTTP_ERROR;

/** Copied HTTP playback snapshot; times are microseconds, with negative values for unavailable bounds. */
typedef struct LND_HTTP_INFO {
    int32_t state; /**< LND_HTTP connection/playback state. */
    uint32_t sample_rate_hz; /**< PCM sample rate in Hz. */
    uint32_t channels; /**< Number of PCM channels. */
    int32_t length_kind; /**< LND_LENGTH_UNKNOWN, EXACT or ESTIMATED. */
    int64_t duration_us; /**< Stream duration in microseconds; negative if unknown. */
    int64_t position_us; /**< Current playback position in microseconds. */
    int64_t seek_start_us; /**< Earliest seekable time in microseconds; negative if unavailable. */
    int64_t seek_end_us; /**< Latest seekable time in microseconds; negative if unavailable. */
    int64_t live_edge_us; /**< Current live edge in microseconds; negative if unavailable. */
    uint64_t bitrate_bps; /**< Encoded bitrate in bits per second. */
    bool live; /**< Input is treated as a live stream. */
    bool hls; /**< Input uses an HLS playlist. */
    bool seekable; /**< True if seeking is supported. */
    char codec[32]; /**< NUL-terminated selected codec name. */
    char title[512]; /**< NUL-terminated current track title. */
    char station[256]; /**< NUL-terminated station name. */
    LND_HTTP_ERROR error; /**< Latest connection/decoding error details. */
} LND_HTTP_INFO;

/** Copied HTTP transfer, decoding and queue counters. */
typedef struct LND_HTTP_STATS {
    uint64_t received_bytes; /**< Encoded bytes received across requests. */
    uint64_t decoded_frames; /**< PCM frames produced by the decoder. */
    uint64_t stalls; /**< Number of playback buffer underruns. */
    uint64_t reconnects; /**< Number of reconnect attempts. */
    uint64_t requests; /**< Number of transport requests started. */
    uint64_t events_lost; /**< Events discarded because the event queue was full. */
    uint64_t throughput_bps; /**< Measured transfer throughput in bits per second. */
    uint64_t file_bytes; /**< Bytes held in the temporary file cache. */
    uint32_t buffered_frames; /**< Frames currently available to read. */
    size_t buffered_bytes; /**< Compressed bytes waiting in decoder/protocol buffers. */
    uint64_t content_bytes; /**< Total encoded content bytes when content_size_known is true. */
    uint64_t range_start_bytes; /**< Absolute byte start of the requested/received range. */
    uint32_t buffering_percent; /**< Progress towards the current start/resume threshold, from 0 to 100. */
    bool content_size_known; /**< content_bytes is valid. */
    bool download_complete; /**< All finite input bytes have arrived; PCM may remain buffered. */
} LND_HTTP_STATS;

/** Copied asynchronous event; request_id correlates a seek request with its result. */
typedef struct LND_HTTP_EVENT {
    int32_t type; /**< LND_HTTP_EVENT identifier. */
    int32_t state; /**< HTTP state at event creation. */
    int32_t result; /**< LND_OK or operation-specific negative error. */
    uint64_t request_id; /**< Seek request correlation ID, when applicable. */
    int64_t position_us; /**< Event position in microseconds. */
    char text[512]; /**< NUL-terminated metadata or diagnostic payload. */
    bool estimated; /**< Reported position is approximate. */
} LND_HTTP_EVENT;

/** Fill options with supported defaults and its structure size; call before overriding fields.
 *
 * @param options Receives default options and the supported structure size.
 */
LND_API void LND_HttpOptionsInit(LND_HTTP_OPTIONS *options);

/** Get LND_HTTP_CAP bits for features available in this build.
 *
 * @return LND_HTTP_CAP bits for features available in this build.
 */
LND_API uint32_t LND_HttpGetCapabilities(void);

/** Begin opening url with optional options.
 * Incremental WAV, MP3, AAC, Opus, Vorbis and FLAC require their enabled codecs. HLS uses MP4/TS
 * demuxers; ffmpeg_stream adds Matroska/WebM/ASF. File-only decoders wait for a complete finite
 * response. Unsupported MP4 range indexes, edit layouts or servers fall back to complete-file decoding.
 * Finite Ogg duration uses validated byte ranges (known size, strong ETag) or one complete response
 * within segment_bytes. CFG is captured at opening; probe failure preserves streaming.
 * Duration may arrive after playback becomes ready.
 * open_timeout_ms runs from this call until playback is first ready, including short files reaching EOF.
 * Taking the source does not stop the timer. Expiry reports LND_HTTP_ERR_TIMEOUT without retrying and
 * discards initial PCM. Workers/updates enforce the deadline; transport callbacks must return promptly.
 * Without an OS clock, call HttpUpdate with the current time before opening a timed session.
 *
 * @param url Stream URL.
 * @param options Settings to apply; NULL selects defaults.
 * @return Owned open handle or NULL; release with LND_HttpOpenFree after taking or cancelling the
 * source.
 */
LND_API LND_HTTP_OPEN *LND_HttpOpen(const char *url, const LND_HTTP_OPTIONS *options);

/** Open url and block until the source has enough PCM to start, or finite input ends.
 * Requires an OS; calls from callbacks fail with LND_ERR_BUSY. Manual transfers are advanced here;
 * after return, continue calling HttpUpdate or LibraryUpdate if the source uses manual execution.
 * Transport callbacks must return promptly. Failed sessions are cleaned up before return.
 *
 * @param url Stream URL.
 * @param timeout_ms Total opening limit in milliseconds, including buffering; 0 disables the limit.
 * @param options Optional HTTP settings; timeout_ms overrides open_timeout_ms without modifying options.
 * @return Owned source, released with LND_SourceFree, or NULL. ErrorGetLast reports the error,
 * including LND_HTTP_ERR_TIMEOUT when the opening limit expires.
 */
LND_API LND_SOURCE *LND_SourceCreateHttp(const char *url, uint32_t timeout_ms, const LND_HTTP_OPTIONS *options);

/** Copy open's current stream state into info.
 *
 * @param open Pending HTTP open operation.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_HttpOpenGetInfo(const LND_HTTP_OPEN *open, LND_HTTP_INFO *info);

/** Transfer a ready source from open to source.
 *
 * @param open Pending HTTP open operation.
 * @param source Receives the owned source on success; release it with LND_SourceFree.
 * @return LND_OK, LND_HTTP_PENDING if not ready, or a negative error.
 */
LND_API int32_t LND_HttpOpenTakeSource(LND_HTTP_OPEN *open, LND_SOURCE **source);

/** Cancel open's pending operation.
 *
 * @param open Pending HTTP open operation.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_HttpOpenCancel(LND_HTTP_OPEN *open);

/** Release open and any source not taken from it.
 *
 * @param open Pending HTTP open operation.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_HttpOpenFree(LND_HTTP_OPEN *open);

/** Copy source's HTTP playback state and metadata into info.
 * Metadata is published at its playback boundary; ICY title changes preserve unrelated fields.
 *
 * @param source Source to operate on.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SourceGetHttpInfo(const LND_SOURCE *source, LND_HTTP_INFO *info);

/** Copy source's HTTP transfer and buffer counters into stats.
 *
 * @param source Source to operate on.
 * @param stats Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SourceGetHttpStats(const LND_SOURCE *source, LND_HTTP_STATS *stats);

/** Pop source's next HTTP event into event.
 *
 * @param source Source to operate on.
 * @param event Receives the next HTTP event.
 * @return LND_OK if read, LND_HTTP_PENDING if empty, or a negative error.
 */
LND_API int32_t LND_SourcePollHttpEvent(LND_SOURCE *source, LND_HTTP_EVENT *event);

/** Request source seek to absolute position_us and optionally write request_id.
 * HLS seeks are limited to the advertised DVR window. Opus and MP4 range seeks are asynchronous.
 * Native Opus ranges require known size, range support and a matching strong ETag; other resources decode from the start.
 * Duration probing flags do not affect range seeking.
 *
 * @param source Source to operate on.
 * @param position_us Absolute media position in microseconds.
 * @param request_id Optional output for the queued request identifier; may be NULL.
 * @return LND_OK or a negative error; completion arrives as an event.
 */
LND_API int32_t LND_SourceSeekHttpMicroseconds(LND_SOURCE *source, int64_t position_us, uint64_t *request_id);

/** Request source seek to the live edge and optionally write request_id.
 *
 * @param source Source to operate on.
 * @param request_id Optional output for the queued request identifier; may be NULL.
 * @return LND_OK or a negative error; completion arrives as an event.
 */
LND_API int32_t LND_SourceSeekHttpLive(LND_SOURCE *source, uint64_t *request_id);

/** Set source's HLS bitrate ceiling and preferred language.
 * Variant switches retain a stable output format.
 *
 * @param source Source to operate on.
 * @param max_bitrate_bps Maximum preferred bitrate in bits per second; 0 removes the limit.
 * @param language Preferred HLS language; NULL clears the preference.
 * @return LND_OK or a negative error; zero bitrate removes the ceiling.
 */
LND_API int32_t LND_SourceSetHttpVariant(LND_SOURCE *source, uint64_t max_bitrate_bps, const char *language);

/** Request cancellation of source's network producer; queued PCM remains available to drain.
 * Ending the source alone does not cancel network production.
 *
 * @param source Source to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SourceCancelHttp(LND_SOURCE *source);

/** Advance manual HTTP work at monotonic_ms for up to work_units.
 * Also completes cleanup of detached sessions in either execution mode. Release the open handle
 * and any source taken from it, then obtain LND_OK here before releasing their transport/context.
 *
 * @param monotonic_ms Current monotonic time in milliseconds.
 * @param work_units Work budget in [1, 65536].
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_HttpUpdate(uint64_t monotonic_ms, uint32_t work_units);

/** Transport request borrowed during open; retain copies needed by an asynchronous transfer. */
typedef struct LND_HTTP_REQUEST {
    const char *url; /**< Absolute request URL. */
    const char *user_agent; /**< User-Agent string; NULL uses the default unless disabled by flags. */
    const LND_HTTP_HEADER *headers; /**< Array of request headers. */
    size_t header_count; /**< Number of entries in headers. */
    const char *proxy; /**< Optional proxy URL. */
    const char *ca_file; /**< Optional CA certificate file path for TLS verification. */
    const char *if_range; /**< Optional validator used for a byte-range request. */
    uint64_t range_start_bytes; /**< Absolute byte start of the requested/received range. */
    uint64_t range_length_bytes; /**< Requested range length; zero requests the remaining bytes. */
    bool range; /**< Send a byte-range request. */
    bool manual; /**< Transport must support caller-driven polling. */
    uint32_t flags; /**< LND_HTTP request policy bits inherited from options. */
    uint32_t connect_timeout_ms; /**< Connection timeout in milliseconds. */
    uint32_t receive_timeout_ms; /**< Receive inactivity timeout in milliseconds. */
    uint32_t max_redirects; /**< Maximum followed redirects. */
} LND_HTTP_REQUEST;

/** Transport response snapshot filled by poll; strings are bounded, NUL-terminated copies. */
typedef struct LND_HTTP_RESPONSE {
    int32_t status; /**< HTTP response status code. */
    int32_t backend_code; /**< Transport-specific result code. */
    uint64_t content_length_bytes; /**< Current response body length when length_known is true. */
    uint64_t range_start_bytes; /**< Absolute byte start of the requested/received range. */
    uint64_t total_length_bytes; /**< Full resource size from Content-Range, when available. */
    uint32_t icy_interval_bytes; /**< Audio byte interval between ICY metadata blocks; zero disables ICY parsing. */
    uint32_t retry_after_ms; /**< Server-requested retry delay in milliseconds. */
    bool headers_complete; /**< Response headers are available for validation. */
    bool length_known; /**< Response byte length is known. */
    bool range; /**< Response contains a validated byte range. */
    bool accepts_ranges; /**< Server advertises byte-range support. */
    char content_type[128]; /**< NUL-terminated response MIME type. */
    char location[4096]; /**< NUL-terminated redirect target. */
    char etag[256]; /**< NUL-terminated entity validator. */
    char station[256]; /**< NUL-terminated ICY station name. */
} LND_HTTP_RESPONSE;

/** Transport callback table borrowed for the session lifetime; optional session callbacks share transport
 * state. Duration probing may keep a second transfer active in the same session.
 */
struct LND_HTTP_TRANSPORT {
    uint32_t size; /**< sizeof(LND_HTTP_TRANSPORT). */
    uint32_t capabilities; /**< Supported LND_HTTP_CAP transport features. */
    /** Open request with user and write owned transfer.
     *
     * @param user Borrowed callback context.
     * @param request Borrowed request descriptor.
     * @param transfer Receives the owned transfer on success.
     * @return LND_OK or a negative error.
     */
    int32_t (*open)(void *user, const LND_HTTP_REQUEST *request, void **transfer);
    /** Update response and copy up to capacity body bytes to data, reporting written.
     *
     * @param transfer Transport transfer created by open.
     * @param response Receives status, lengths and copied response strings.
     * @param data Writable response-body buffer with capacity bytes.
     * @param capacity Writable buffer capacity in bytes.
     * @param written Receives the number of bytes written.
     * @return LND_OK, LND_HTTP_PENDING, LND_HTTP_DONE or a negative error.
     */
    int32_t (*poll)(void *transfer, LND_HTTP_RESPONSE *response, void *data, size_t capacity, size_t *written);
    /** Cancel and release transfer.
     *
     * @param transfer Transport transfer created by open.
     */
    void (*close)(void *transfer);
    /** Optionally create session from user.
     *
     * @param user Borrowed callback context.
     * @param session Receives the owned session on success.
     * @return LND_OK or a negative error. Session replaces user for open.
     */
    int32_t (*session_open)(void *user, void **session);
    /** Release session created by session_open.
     *
     * @param session Transport session created by session_open.
     */
    void (*session_close)(void *session);
};

#ifdef __cplusplus
}
#endif
