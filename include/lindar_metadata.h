#pragma once

#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Owned text, chapter and binary metadata; Get views are borrowed and may be invalidated by mutation. */
typedef struct LND_METADATA LND_METADATA;

/** Encoder and its destination; OutputFinish finalises, OutputFree closes and releases it. */
typedef struct LND_OUTPUT LND_OUTPUT;

enum {
    LND_METADATA_AUTO, /**< Detect input tag format or use the writer's selection policy. */
    LND_METADATA_ID3V1, /**< ID3v1 trailing tag. */
    LND_METADATA_ID3V2, /**< ID3v2 frame-based tag. */
    LND_METADATA_OPUS, /**< OpusTags comment packet. */
    LND_METADATA_WAVE, /**< WAV/RF64 metadata chunks. */
    LND_METADATA_VORBIS, /**< Vorbis comment packet. */
    LND_METADATA_FLAC, /**< FLAC metadata/comments. */
};

enum {
    LND_METADATA_DROP_UNSUPPORTED = 1u << 0, /**< Omit fields the target format cannot preserve instead of failing. */
    LND_METADATA_ERR_LIMIT = -40, /**< Metadata exceeds a configured allocation/count limit. */
    LND_METADATA_ERR_NOT_FOUND = -41, /**< Requested metadata item/tag is absent. */
};

/** Unknown chapter time or byte offset, distinct from zero. */
#define LND_METADATA_UNKNOWN UINT64_MAX

/** Metadata allocation/count ceilings; zero fields select library defaults. */
typedef struct LND_METADATA_LIMITS {
    size_t bytes; /**< Maximum metadata storage bytes; zero uses the default. */
    uint32_t fields; /**< Maximum text fields; zero uses the default. */
    uint32_t chapters; /**< Maximum chapters; zero uses the default. */
    uint32_t blobs; /**< Maximum binary blobs; zero uses the default. */
} LND_METADATA_LIMITS;

/** Text field; setters copy strings and getters expose borrowed strings owned by metadata. */
typedef struct LND_METADATA_FIELD {
    const char *key; /**< Case-insensitive field key, such as TITLE or ARTIST. */
    const char *value; /**< UTF-8 field value. */
    const char *language; /**< Optional language identifier. */
    const char *description; /**< Optional qualifier/description. */
} LND_METADATA_FIELD;

/** Chapter interval and optional byte bounds; use METADATA_UNKNOWN for unknown numeric bounds. */
typedef struct LND_METADATA_CHAPTER {
    const char *id; /**< Chapter identifier used for replacement/removal. */
    const char *title; /**< Optional chapter title. */
    const char *url; /**< Optional chapter URL. */
    uint64_t start_us; /**< Start time in microseconds, or METADATA_UNKNOWN. */
    uint64_t end_us; /**< End time in microseconds, or METADATA_UNKNOWN. */
    uint64_t start_offset_bytes; /**< Start byte offset, or METADATA_UNKNOWN. */
    uint64_t end_offset_bytes; /**< End byte offset, or METADATA_UNKNOWN. */
} LND_METADATA_CHAPTER;

/** Preserved format-specific data; AddBlob copies it, GetBlob exposes borrowed storage. */
typedef struct LND_METADATA_BLOB {
    int32_t format; /**< Originating LND_METADATA format. */
    const char *key; /**< Format-specific item/frame key. */
    const char *scope; /**< Optional format-specific scope. */
    uint32_t flags; /**< Format-specific preservation flags. */
    const void *data; /**< Borrowed binary payload on input/output; AddBlob copies it. */
    size_t size; /**< Payload length in bytes. */
} LND_METADATA_BLOB;

/** Copy metadata onto source; NULL clears it.
 * Detaches any metadata provider, including subsequent HTTP/ICY updates.
 *
 * @param source Source to operate on.
 * @param metadata Metadata to copy; NULL clears existing metadata.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SourceSetMetadata(LND_SOURCE *source, const LND_METADATA *metadata);

/** Copy source's metadata into an existing metadata object.
 *
 * @param source Source to operate on.
 * @param metadata Existing object to receive a copy of the metadata.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_SourceCopyMetadata(const LND_SOURCE *source, LND_METADATA *metadata);

/** Get source's metadata load result: LND_OK or a negative error, including NOT_FOUND.
 *
 * @param source Source to operate on.
 * @return Source's metadata load result: LND_OK or a negative error, including NOT_FOUND.
 */
LND_API int32_t LND_SourceGetMetadataStatus(const LND_SOURCE *source);

/** Copy output's metadata into an existing metadata object.
 *
 * @param output Encoder output to operate on.
 * @param metadata Existing object to receive a copy of the metadata.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_OutputCopyMetadata(const LND_OUTPUT *output, LND_METADATA *metadata);

/** Create metadata with optional limits.
 *
 * @param limits Allocation and count limits; NULL selects defaults.
 * @return An owned object or NULL; release with LND_MetadataFree.
 */
LND_API LND_METADATA *LND_MetadataCreate(const LND_METADATA_LIMITS *limits);

/** Deep-copy metadata, including fields, chapters and blobs.
 *
 * @param metadata Metadata object to read or update.
 * @return An owned object or NULL; release with LND_MetadataFree.
 */
LND_API LND_METADATA *LND_MetadataClone(const LND_METADATA *metadata);

/** Release metadata and all owned strings/blobs; borrowed views become invalid. NULL is accepted.
 *
 * @param metadata Metadata object to read or update.
 */
LND_API void LND_MetadataFree(LND_METADATA *metadata);

/** Empty metadata while retaining its limits; previously borrowed views become invalid.
 *
 * @param metadata Metadata object to read or update.
 */
LND_API void LND_MetadataClear(LND_METADATA *metadata);

/** Release a buffer returned by a metadata CreateBuffer function; NULL is accepted.
 *
 * @param buffer Owned metadata buffer to release; NULL is accepted.
 */
LND_API void LND_MetadataBufferFree(void *buffer);

/** Get metadata's detected tag format.
 *
 * @param metadata Metadata object to read or update.
 * @return Metadata's detected tag format, or AUTO when unspecified.
 */
LND_API int32_t LND_MetadataGetFormat(const LND_METADATA *metadata);

/** Get metadata's detected format-specific version.
 *
 * @param metadata Metadata object to read or update.
 * @return Metadata's detected format-specific version, or zero if unspecified.
 */
LND_API uint32_t LND_MetadataGetVersion(const LND_METADATA *metadata);

/** Get metadata's vendor string.
 *
 * @param metadata Metadata object to read or update.
 * @return Metadata's borrowed vendor string, or NULL if absent; mutation may invalidate it.
 */
LND_API const char *LND_MetadataGetVendor(const LND_METADATA *metadata);

/** Copy vendor into metadata.
 *
 * @param metadata Metadata object to read or update.
 * @param vendor Vendor string to copy.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataSetVendor(LND_METADATA *metadata, const char *vendor);

/** Get metadata's text field count.
 *
 * @param metadata Metadata object to read or update.
 * @return Metadata's text field count, or zero for NULL.
 */
LND_API uint32_t LND_MetadataGetFieldCount(const LND_METADATA *metadata);

/** Get a field at zero-based index.
 *
 * @param metadata Metadata object to read or update.
 * @param index Zero-based entry index.
 * @return A borrowed field at zero-based index, or NULL if out of range; mutation may invalidate
 * it.
 */
LND_API const LND_METADATA_FIELD *LND_MetadataGetField(const LND_METADATA *metadata, uint32_t index);

/** Get the value for key's zero-based occurrence.
 *
 * @param metadata Metadata object to read or update.
 * @param key Case-insensitive metadata field key.
 * @param occurrence Zero-based occurrence of the matching key.
 * @return The borrowed value for key's zero-based occurrence, or NULL if absent; mutation may
 * invalidate it.
 */
LND_API const char *LND_MetadataGetValue(const LND_METADATA *metadata, const char *key, uint32_t occurrence);

/** Append a copy of field and its strings to metadata.
 *
 * @param metadata Metadata object to read or update.
 * @param field Text field to copy.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataAddField(LND_METADATA *metadata, const LND_METADATA_FIELD *field);

/** Replace metadata's values for key with a copy of value.
 *
 * @param metadata Metadata object to read or update.
 * @param key Case-insensitive field key.
 * @param value UTF-8 field value to copy.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataSetValue(LND_METADATA *metadata, const char *key, const char *value);

/** Remove all metadata fields matching key.
 *
 * @param metadata Metadata object to read or update.
 * @param key Case-insensitive metadata field key.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataRemove(LND_METADATA *metadata, const char *key);

/** Remove metadata's field at zero-based index.
 *
 * @param metadata Metadata object to read or update.
 * @param index Zero-based entry index.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataRemoveField(LND_METADATA *metadata, uint32_t index);

/** Get metadata's chapter count.
 *
 * @param metadata Metadata object to read or update.
 * @return Metadata's chapter count, or zero for NULL.
 */
LND_API uint32_t LND_MetadataGetChapterCount(const LND_METADATA *metadata);

/** Get a chapter at zero-based index.
 *
 * @param metadata Metadata object to read or update.
 * @param index Zero-based entry index.
 * @return A borrowed chapter at zero-based index, or NULL if out of range; mutation may invalidate
 * it.
 */
LND_API const LND_METADATA_CHAPTER *LND_MetadataGetChapter(const LND_METADATA *metadata, uint32_t index);

/** Copy chapter into metadata, replacing a chapter with the same ID.
 *
 * @param metadata Metadata object to read or update.
 * @param chapter Chapter to copy.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataSetChapter(LND_METADATA *metadata, const LND_METADATA_CHAPTER *chapter);

/** Get the last chapter index whose start is at or before position_us.
 *
 * @param metadata Metadata object to read or update.
 * @param position_us Absolute media position in microseconds.
 * @return The last chapter index whose start is at or before position_us, or a negative
 * error/NOT_FOUND; end bounds are not tested.
 */
LND_API int32_t LND_MetadataFindChapter(const LND_METADATA *metadata, uint64_t position_us);

/** Remove metadata's chapter with id.
 *
 * @param metadata Metadata object to read or update.
 * @param id Chapter identifier to remove.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataRemoveChapter(LND_METADATA *metadata, const char *id);

/** Get metadata's preserved binary blob count.
 *
 * @param metadata Metadata object to read or update.
 * @return Metadata's preserved binary blob count, or zero for NULL.
 */
LND_API uint32_t LND_MetadataGetBlobCount(const LND_METADATA *metadata);

/** Get a blob at zero-based index.
 *
 * @param metadata Metadata object to read or update.
 * @param index Zero-based entry index.
 * @return A borrowed blob at zero-based index, or NULL if out of range; mutation may invalidate
 * it.
 */
LND_API const LND_METADATA_BLOB *LND_MetadataGetBlob(const LND_METADATA *metadata, uint32_t index);

/** Append a copy of blob and its data to metadata.
 *
 * @param metadata Metadata object to read or update.
 * @param blob Binary metadata entry to copy.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataAddBlob(LND_METADATA *metadata, const LND_METADATA_BLOB *blob);

/** Remove metadata's blob at zero-based index.
 *
 * @param metadata Metadata object to read or update.
 * @param index Zero-based entry index.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_MetadataRemoveBlob(LND_METADATA *metadata, uint32_t index);

#ifdef __cplusplus
}
#endif
