#pragma once

#include "lindar.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Container parser with borrowed segments and reusable packet storage; release with DemuxFree. */
typedef struct LND_DEMUX LND_DEMUX;

enum {
    LND_DEMUX_AUTO, /**< Detect MPEG-TS or MP4 from the segment. */
    LND_DEMUX_MPEGTS, /**< Parse 188-byte MPEG transport stream packets. */
    LND_DEMUX_FMP4 /**< Parse MP4 initialisation/indexes and media fragments. */
};
enum {
    LND_DEMUX_END = 1, /**< Read result: the supplied segment/input has no packet available. */
    LND_DEMUX_CONFIG = 1u << 0, /**< Packet flag: codec configuration accompanies this packet. */
    LND_DEMUX_DISCONTINUITY = 1u << 1, /**< Packet flag: reset continuity across this boundary. */
    LND_DEMUX_EDIT = 1u << 2 /**< Packet flag: container edit/trim information applies. */
};

/** Container, track and packet limit selection; zero values select automatic/default behaviour. */
typedef struct LND_DEMUX_OPTIONS {
    int32_t container; /**< LND_DEMUX_AUTO, MPEGTS or FMP4. */
    uint32_t track_id; /**< Requested track ID; zero selects an audio track automatically. */
    size_t packet_bytes; /**< Maximum packet buffer bytes; zero uses the default. */
} LND_DEMUX_OPTIONS;

/** Borrowed encoded packet and codec configuration; copy before advancing or resetting the demuxer. */
typedef struct LND_DEMUX_PACKET {
    const void *data; /**< Borrowed encoded payload; may point into the current segment. */
    size_t bytes; /**< Encoded payload length in bytes. */
    const char *codec; /**< Borrowed format identifier. */
    const void *config; /**< Borrowed codec initialisation data. */
    size_t config_bytes; /**< Length of config in bytes. */
    uint32_t track_id; /**< Container track identifier. */
    uint32_t channels; /**< Number of PCM channels. */
    uint32_t sample_rate_hz; /**< PCM sample rate in Hz. */
    uint32_t flags; /**< LND_DEMUX_CONFIG, DISCONTINUITY and EDIT bits. */
    uint32_t duration_ticks; /**< Sample duration in container ticks. */
    uint32_t timescale; /**< Container ticks per second. */
    int64_t time_us; /**< Presentation time in microseconds. */
    int64_t duration_us; /**< Packet duration in microseconds. */
    uint32_t trim_start_frames; /**< Leading decoded frames to discard. */
    uint32_t trim_end_frames; /**< Trailing decoded frames to discard. */
} LND_DEMUX_PACKET;

/** Indexed sample location with its packet description; data is not loaded by GetSample. */
typedef struct LND_DEMUX_SAMPLE {
    uint64_t offset_bytes; /**< Absolute file byte offset of encoded payload. */
    LND_DEMUX_PACKET packet; /**< Sample timing, codec configuration and payload size. */
} LND_DEMUX_SAMPLE;

/** Get demux's indexed MP4 sample count.
 *
 * @param demux Demuxer to operate on.
 * @return Demux's indexed MP4 sample count, or zero when no index is available.
 */
LND_API uint64_t LND_DemuxGetSampleCount(const LND_DEMUX *demux);

/** Copy indexed sample index into sample, including byte offset and borrowed configuration.
 *
 * @param demux Demuxer to operate on.
 * @param index Zero-based entry index.
 * @param sample Receives the requested snapshot.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DemuxGetSample(const LND_DEMUX *demux, uint64_t index, LND_DEMUX_SAMPLE *sample);

/** Create a demuxer with optional options.
 *
 * @param options Settings to apply; NULL selects defaults.
 * @return Owned demuxer or NULL; release with LND_DemuxFree.
 */
LND_API LND_DEMUX *LND_DemuxCreate(const LND_DEMUX_OPTIONS *options);

/** Parse bytes of MP4 initialisation data into demux's track state.
 *
 * @param demux Demuxer to operate on.
 * @param data Input bytes borrowed for the operation.
 * @param bytes Buffer length in bytes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DemuxSetInit(LND_DEMUX *demux, const void *data, size_t bytes);

/** Begin demuxing bytes of borrowed data at file offset zero; retain it while reading this segment.
 *
 * @param demux Demuxer to operate on.
 * @param data Encoded segment borrowed until reading this segment ends.
 * @param bytes Buffer length in bytes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DemuxBegin(LND_DEMUX *demux, const void *data, size_t bytes);

/** Begin a borrowed segment of bytes at absolute offset_bytes; retain data while reading it.
 *
 * @param demux Demuxer to operate on.
 * @param data Encoded segment borrowed until reading this segment ends.
 * @param bytes Buffer length in bytes.
 * @param offset_bytes Absolute segment offset in the encoded file, in bytes.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DemuxBeginAt(LND_DEMUX *demux, const void *data, size_t bytes, uint64_t offset_bytes);

/** Mark demux's final segment so buffered packets can drain.
 *
 * @param demux Demuxer to operate on.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_DemuxEnd(LND_DEMUX *demux);

/** Write the next borrowed packet to packet.
 *
 * @param demux Demuxer to operate on.
 * @param packet Receives packet fields and borrowed payload/configuration views.
 * @return LND_OK, LND_DEMUX_END if exhausted, or a negative error; copy data before advancing
 * demux.
 */
LND_API int32_t LND_DemuxRead(LND_DEMUX *demux, LND_DEMUX_PACKET *packet);

/** Clear demux's segment, buffered packets and end/error state for reuse.
 *
 * @param demux Demuxer to operate on.
 */
LND_API void LND_DemuxReset(LND_DEMUX *demux);

/** Release demux's buffers and track index; borrowed segment data is not freed. NULL is accepted.
 *
 * @param demux Demuxer to operate on.
 */
LND_API void LND_DemuxFree(LND_DEMUX *demux);

#ifdef __cplusplus
}
#endif
