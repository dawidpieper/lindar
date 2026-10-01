#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef intptr_t ssize_t;
typedef int64_t off64_t;
typedef int32_t media_status_t;
typedef struct AMediaFormat AMediaFormat;
typedef struct AMediaCodec AMediaCodec;
typedef struct AMediaExtractor AMediaExtractor;
typedef struct AMediaDataSource AMediaDataSource;
typedef ssize_t (*AMediaDataSourceReadAt)(void *, off64_t, void *, size_t);
typedef ssize_t (*AMediaDataSourceGetSize)(void *);
typedef void (*AMediaDataSourceClose)(void *);
typedef struct AMediaCodecBufferInfo {
    int32_t offset, size;
    int64_t presentationTimeUs;
    uint32_t flags;
} AMediaCodecBufferInfo;
typedef enum { AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC, AMEDIAEXTRACTOR_SEEK_NEXT_SYNC, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC } SeekMode;
enum {
    AMEDIA_OK = 0,
    AMEDIACODEC_INFO_TRY_AGAIN_LATER = -1,
    AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED = -2,
    AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED = -3,
    AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG = 2,
    AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM = 4,
    AMEDIAEXTRACTOR_SAMPLE_FLAG_ENCRYPTED = 2
};
extern const char *AMEDIAFORMAT_KEY_CHANNEL_COUNT, *AMEDIAFORMAT_KEY_SAMPLE_RATE, *AMEDIAFORMAT_KEY_PCM_ENCODING, *AMEDIAFORMAT_KEY_MIME, *AMEDIAFORMAT_KEY_DURATION;
AMediaDataSource *AMediaDataSource_new(void);
void AMediaDataSource_delete(AMediaDataSource *);
void AMediaDataSource_setUserdata(AMediaDataSource *, void *);
void AMediaDataSource_setReadAt(AMediaDataSource *, AMediaDataSourceReadAt);
void AMediaDataSource_setGetSize(AMediaDataSource *, AMediaDataSourceGetSize);
void AMediaDataSource_setClose(AMediaDataSource *, AMediaDataSourceClose);
AMediaExtractor *AMediaExtractor_new(void);
media_status_t AMediaExtractor_delete(AMediaExtractor *);
media_status_t AMediaExtractor_setDataSourceCustom(AMediaExtractor *, AMediaDataSource *);
size_t AMediaExtractor_getTrackCount(AMediaExtractor *);
AMediaFormat *AMediaExtractor_getTrackFormat(AMediaExtractor *, size_t);
media_status_t AMediaExtractor_selectTrack(AMediaExtractor *, size_t);
media_status_t AMediaExtractor_unselectTrack(AMediaExtractor *, size_t);
ssize_t AMediaExtractor_getSampleSize(AMediaExtractor *);
ssize_t AMediaExtractor_readSampleData(AMediaExtractor *, uint8_t *, size_t);
uint32_t AMediaExtractor_getSampleFlags(AMediaExtractor *);
int64_t AMediaExtractor_getSampleTime(AMediaExtractor *);
bool AMediaExtractor_advance(AMediaExtractor *);
media_status_t AMediaExtractor_seekTo(AMediaExtractor *, int64_t, SeekMode);
AMediaCodec *AMediaCodec_createDecoderByType(const char *);
media_status_t AMediaCodec_configure(AMediaCodec *, const AMediaFormat *, void *, void *, uint32_t);
media_status_t AMediaCodec_start(AMediaCodec *);
media_status_t AMediaCodec_stop(AMediaCodec *);
media_status_t AMediaCodec_delete(AMediaCodec *);
media_status_t AMediaCodec_flush(AMediaCodec *);
uint8_t *AMediaCodec_getInputBuffer(AMediaCodec *, size_t, size_t *);
uint8_t *AMediaCodec_getOutputBuffer(AMediaCodec *, size_t, size_t *);
ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec *, int64_t);
ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec *, AMediaCodecBufferInfo *, int64_t);
media_status_t AMediaCodec_queueInputBuffer(AMediaCodec *, size_t, off64_t, size_t, uint64_t, uint32_t);
media_status_t AMediaCodec_releaseOutputBuffer(AMediaCodec *, size_t, bool);
AMediaFormat *AMediaCodec_getOutputFormat(AMediaCodec *);
media_status_t AMediaFormat_delete(AMediaFormat *);
bool AMediaFormat_getInt64(AMediaFormat *, const char *, int64_t *);
bool AMediaFormat_getInt32(AMediaFormat *, const char *, int32_t *);
bool AMediaFormat_getString(AMediaFormat *, const char *, const char **);
