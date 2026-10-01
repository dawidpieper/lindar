#pragma once
#include "lindar_output.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Worker-backed encoded stream upload; finish outputs using it before CastFree. */
typedef struct LND_CAST LND_CAST;
enum {
    LND_CAST_SHOUTCAST, /**< SHOUTcast v1 source upload and metadata. */
    LND_CAST_ICECAST /**< Icecast SOURCE upload and metadata. */
};
enum {
    LND_CAST_CONNECTING, /**< The upload connection is being established. */
    LND_CAST_READY, /**< Encoded data can be uploaded. */
    LND_CAST_DRAINING, /**< End was requested; queued bytes are being sent. */
    LND_CAST_ENDED, /**< All queued bytes have been sent and the upload ended. */
    LND_CAST_FAILED /**< Upload stopped with an error. */
};

/** Streaming server options copied by CastCreate, including strings; zero limits select defaults. */
typedef struct LND_CAST_OPTIONS {
    const char *url; /**< Streaming server URL. */
    const char *admin_url; /**< Optional metadata administration URL. */
    const char *username; /**< Optional server login name. */
    const char *password; /**< Server login password. */
    const char *mount; /**< Icecast mount path or protocol-specific stream identifier. */
    const char *content_type; /**< MIME type of the encoded stream. */
    const char *name; /**< Advertised station name. */
    const char *description; /**< Advertised station description. */
    const char *genre; /**< Advertised genre. */
    const char *website; /**< Advertised station website URL. */
    const char *ca_file; /**< Optional CA certificate file path for TLS verification. */
    uint32_t bitrate_kbps; /**< Advertised stream bitrate in kilobits per second. */
    uint32_t timeout_ms; /**< Transfer timeout in milliseconds; zero uses the default. */
    size_t queue_bytes; /**< Encoded output queue capacity; zero uses the default. */
    int32_t protocol; /**< LND_CAST_SHOUTCAST or LND_CAST_ICECAST. */
    bool public_stream; /**< Request a publicly listed station. */
} LND_CAST_OPTIONS;

/** Copied upload state; metadata errors are separate from audio transfer errors. */
typedef struct LND_CAST_INFO {
    int32_t state; /**< LND_CAST connection/drain state. */
    int32_t error; /**< LND_OK or the recorded negative processing error. */
    int32_t metadata_error; /**< Last title update error, independent of audio transfer. */
    size_t queued_bytes; /**< Encoded bytes awaiting upload. */
    size_t capacity_bytes; /**< Encoded queue capacity. */
    uint64_t sent_bytes; /**< Encoded bytes sent to the server. */
} LND_CAST_INFO;

/** Start an encoded streaming connection using copied options.
 * Reconnection is the responsibility of the application.
 *
 * @param options Required settings, borrowed during the call.
 * @return Owned cast or NULL; release with LND_CastFree.
 */
LND_API LND_CAST *LND_CastCreate(const LND_CAST_OPTIONS *options);

/** Queue up to bytes of encoded data on cast.
 *
 * @param cast Streaming connection.
 * @param data Input bytes borrowed for the operation.
 * @param bytes Buffer length in bytes.
 * @return Accepted bytes or a negative error; retry any remainder.
 */
LND_API int64_t LND_CastWrite(LND_CAST *cast, const void *data, size_t bytes);

/** Create an encoder using params and cast as its destination.
 * The bridge cannot retry partial encoded chunks. Size queue_bytes for the largest encoder write
 * and expected network stalls.
 *
 * @param cast Streaming connection.
 * @param params Encoder settings; referenced metadata is copied on creation.
 * @return Owned output or NULL; release it with LND_OutputFree before LND_CastFree.
 */
LND_API LND_OUTPUT *LND_OutputCreateCast(LND_CAST *cast, const LND_ENCODER_PARAMS *params);

/** Queue a copied title update for cast's server metadata.
 *
 * @param cast Streaming connection.
 * @param title UTF-8 title to copy.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_CastSetTitle(LND_CAST *cast, const char *title);

/** Copy cast's connection, queue and error state into info.
 *
 * @param cast Streaming connection.
 * @param info Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_CastGetInfo(const LND_CAST *cast, LND_CAST_INFO *info);

/** Start draining cast's queued data; does not wait for ENDED or FAILED.
 * Finish/free the encoder output first, then retain cast until ENDED or FAILED before freeing it.
 *
 * @param cast Streaming connection.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_CastEnd(LND_CAST *cast);

/** Stop cast's worker and release the connection and queue; NULL is accepted.
 * Does not drain queued bytes; use LND_CastEnd and wait for ENDED or FAILED to finish an upload.
 *
 * @param cast Streaming connection.
 */
LND_API void LND_CastFree(LND_CAST *cast);
#ifdef __cplusplus
}
#endif
