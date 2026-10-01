#include "lindar_audiotoolbox.h"
#include "formats/codecs/system.h"

#include <AudioToolbox/AudioToolbox.h>

typedef struct lnd_at_state {
    LND_IO *io;
    AudioFileID file;
    ExtAudioFileRef decoder;
    AudioStreamBasicDescription client;
    bool compressed;
    uint32_t frame_bytes;
    uint64_t length;
    bool failed;
} lnd_at_state;

static OSStatus lnd_at_input(void *user, SInt64 offset, UInt32 size, void *dst, UInt32 *actual) {
    lnd_at_state *s = user;
    *actual = 0;
    if (offset < 0 || (uint64_t)offset > LND_IoGetSizeBytes(s->io)) return kAudioFilePositionError;
    if (LND_IoSeekBytes(s->io, (uint64_t)offset) != LND_OK) return kAudioFilePositionError;
    int64_t got = LND_IoRead(s->io, dst, size);
    if (got < 0) return kAudioFileUnspecifiedError;
    *actual = (UInt32)got;
    return noErr;
}

static SInt64 lnd_at_size(void *user) { return (SInt64)LND_IoGetSizeBytes(((lnd_at_state *)user)->io); }

static void lnd_at_close(void *state) {
    lnd_at_state *s = state;
    if (!s) return;
    if (s->decoder) ExtAudioFileDispose(s->decoder);
    if (s->file) AudioFileClose(s->file);
    lnd_free(s);
}

static int32_t lnd_at_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    *state = nullptr;
    if (LND_IoGetSizeBytes(io) > INT64_MAX) return LND_ERR_UNSUPPORTED;
    lnd_at_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->io = io;
    if (AudioFileOpenWithCallbacks(s, lnd_at_input, nullptr, lnd_at_size, nullptr, 0, &s->file) != noErr ||
        ExtAudioFileWrapAudioFileID(s->file, false, &s->decoder) != noErr)
        goto fail;
    AudioStreamBasicDescription format = {0};
    UInt32 size = sizeof format;
    if (ExtAudioFileGetProperty(s->decoder, kExtAudioFileProperty_FileDataFormat, &size, &format) != noErr ||
        !(format.mSampleRate >= 1 && format.mSampleRate <= UINT32_MAX) || !format.mChannelsPerFrame || format.mChannelsPerFrame > LND_MAX_CHANNELS)
        goto fail;
    uint32_t sample_rate_hz = (uint32_t)format.mSampleRate;
    if (format.mSampleRate != sample_rate_hz) goto fail;
    s->compressed = format.mFormatID != kAudioFormatLinearPCM;
    s->frame_bytes = format.mChannelsPerFrame * sizeof(float);
    AudioStreamBasicDescription client = {.mSampleRate = sample_rate_hz,
                                          .mFormatID = kAudioFormatLinearPCM,
                                          .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian,
                                          .mBytesPerPacket = s->frame_bytes,
                                          .mFramesPerPacket = 1,
                                          .mBytesPerFrame = s->frame_bytes,
                                          .mChannelsPerFrame = format.mChannelsPerFrame,
                                          .mBitsPerChannel = 32};
    if (ExtAudioFileSetProperty(s->decoder, kExtAudioFileProperty_ClientDataFormat, sizeof client, &client) != noErr) goto fail;
    s->client = client;
    SInt64 length = 0;
    size = sizeof length;
    if (ExtAudioFileGetProperty(s->decoder, kExtAudioFileProperty_FileLengthFrames, &size, &length) == noErr && length > 0) s->length = (uint64_t)length;
    *info = (LND_CODEC_INFO){.format = LND_FORMAT_F32, .channels = client.mChannelsPerFrame, .sample_rate_hz = sample_rate_hz, .length_frames = s->length, .seekable = LND_IoCanSeek(io)};
    *state = s;
    return LND_OK;
fail:
    lnd_at_close(s);
    return LND_ERR_FORMAT;
}

static uint64_t lnd_at_read(void *state, void *dst, uint64_t frames) {
    lnd_at_state *s = state;
    if (!frames) return 0;
    if (s->failed || frames > SIZE_MAX / s->frame_bytes) return LND_CODEC_READ_ERROR;
    uint64_t done = 0;
    while (done < frames) {
        UInt32 count = (UInt32)LND_MIN(frames - done, UINT32_MAX / s->frame_bytes);
        UInt32 wanted = count;
        AudioBufferList buffers = {.mNumberBuffers = 1,
                                   .mBuffers = {{s->frame_bytes / sizeof(float), count * s->frame_bytes, (uint8_t *)dst + (size_t)done * s->frame_bytes}}};
        if (ExtAudioFileRead(s->decoder, &count, &buffers) != noErr || count > wanted) {
            s->failed = true;
            break;
        }
        done += count;
        if (!count) break;
    }
    return s->failed ? LND_CODEC_READ_ERROR : done;
}

static int32_t lnd_at_seek(void *state, uint64_t frame) {
    lnd_at_state *s = state;
    if (frame > INT64_MAX || (s->length && frame > s->length)) return LND_ERR_INVALID_ARG;
    if (!frame && s->compressed) {
        ExtAudioFileRef decoder = nullptr;
        if (ExtAudioFileWrapAudioFileID(s->file, false, &decoder) != noErr) return LND_ERR_IO;
        if (ExtAudioFileSetProperty(decoder, kExtAudioFileProperty_ClientDataFormat, sizeof s->client, &s->client) != noErr) {
            ExtAudioFileDispose(decoder);
            return LND_ERR_IO;
        }
        ExtAudioFileDispose(s->decoder);
        s->decoder = decoder;
    } else if (ExtAudioFileSeek(s->decoder, (SInt64)frame) != noErr) {
        return LND_ERR_IO;
    }
    s->failed = false;
    return LND_OK;
}

const LND_CODEC lnd_codec_audiotoolbox = {
    .name = "audiotoolbox",
    .extensions = "au;snd;aac;adts;m4a;m4b;mp4;mp3;wav;aif;aiff;aifc;caf;flac;alac;ac3;amr;3gp;3g2",
    .flags = LND_CODEC_FLAG_SYSTEM,
    .probe = lnd_system_probe,
    .open = lnd_at_open,
    .read = lnd_at_read,
    .seek = lnd_at_seek,
    .close = lnd_at_close
};

const LND_CODEC *LND_AudioToolboxGetCodec(void) { return &lnd_codec_audiotoolbox; }
