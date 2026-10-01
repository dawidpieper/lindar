#pragma once
#include <stdint.h>
typedef struct AAudioStream AAudioStream;
typedef struct AAudioStreamBuilder AAudioStreamBuilder;
typedef int32_t aaudio_result_t;
typedef int32_t aaudio_format_t;
typedef int32_t aaudio_stream_state_t;
typedef int32_t aaudio_data_callback_result_t;
enum {
    AAUDIO_INPUT_PRESET_GENERIC = 1, AAUDIO_INPUT_PRESET_CAMCORDER = 5, AAUDIO_INPUT_PRESET_VOICE_RECOGNITION = 6,
    AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION = 7, AAUDIO_INPUT_PRESET_UNPROCESSED = 9, AAUDIO_INPUT_PRESET_VOICE_PERFORMANCE = 10,
    AAUDIO_OK = 0, AAUDIO_ERROR_NO_MEMORY = -1, AAUDIO_ERROR_DISCONNECTED = -2, AAUDIO_ERROR_UNAVAILABLE = -3,
    AAUDIO_ERROR_INVALID_FORMAT = -4, AAUDIO_ERROR_INVALID_RATE = -5, AAUDIO_ERROR_INTERNAL = -6,
    AAUDIO_DIRECTION_INPUT = 1, AAUDIO_DIRECTION_OUTPUT = 2,
    AAUDIO_SHARING_MODE_EXCLUSIVE = 1, AAUDIO_SHARING_MODE_SHARED = 2,
    AAUDIO_PERFORMANCE_MODE_LOW_LATENCY = 1, AAUDIO_FORMAT_PCM_I16 = 1, AAUDIO_FORMAT_PCM_FLOAT = 2,
    AAUDIO_CALLBACK_RESULT_STOP = 1, AAUDIO_CALLBACK_RESULT_CONTINUE = 0,
    AAUDIO_STREAM_STATE_STOPPING = 1, AAUDIO_STREAM_STATE_STARTING = 2, AAUDIO_STREAM_STATE_STARTED = 3,
    AAUDIO_STREAM_STATE_STOPPED = 4
};
typedef aaudio_data_callback_result_t (*AAudioStream_dataCallback)(AAudioStream *, void *, void *, int32_t);
typedef void (*AAudioStream_errorCallback)(AAudioStream *, void *, aaudio_result_t);
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **);
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *);
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setDeviceId(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *, aaudio_format_t);
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setFramesPerDataCallback(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *, AAudioStream_dataCallback, void *);
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *, AAudioStream_errorCallback, void *);
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *, AAudioStream **);
aaudio_result_t AAudioStream_close(AAudioStream *);
int32_t AAudioStream_getSharingMode(AAudioStream *);
int32_t AAudioStream_getSampleRate(AAudioStream *);
int32_t AAudioStream_getChannelCount(AAudioStream *);
int32_t AAudioStream_getFramesPerBurst(AAudioStream *);
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream *);
aaudio_format_t AAudioStream_getFormat(AAudioStream *);
int32_t AAudioStream_setBufferSizeInFrames(AAudioStream *, int32_t);
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *);
aaudio_result_t AAudioStream_requestStart(AAudioStream *);
aaudio_result_t AAudioStream_requestStop(AAudioStream *);
aaudio_stream_state_t AAudioStream_getState(AAudioStream *);
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *, aaudio_stream_state_t, aaudio_stream_state_t *, int64_t);
