#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <objidl.h>

#include "src/alloc.h"
#include "src/atomic.h"
#include "utility/log/log.h"
#include "src/platform.h"
#include "io/io.h"
#include "lindar_codecs.h"
#include "src/format.h"

#include <string.h>

static const IID lnd_IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID lnd_IID_ISequentialStream = {0x0C733A30, 0x2A1C, 0x11CE, {0xAD, 0xE5, 0x00, 0xAA, 0x00, 0x44, 0x77, 0x3D}};
static const IID lnd_IID_IStream = {0x0000000C, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID lnd_MF_MT_MAJOR_TYPE = {0x48EBA18E, 0xF8C9, 0x4687, {0xBF, 0x11, 0x0A, 0x74, 0xC9, 0xF9, 0x6A, 0x8F}};
static const GUID lnd_MF_MT_SUBTYPE = {0xF7E34C9A, 0x42E8, 0x4714, {0xB7, 0x4B, 0xCB, 0x29, 0xD7, 0x2C, 0x35, 0xE5}};
static const GUID lnd_MF_MT_AUDIO_NUM_CHANNELS = {0x37E48BF5, 0x645E, 0x4C5B, {0x89, 0xDE, 0xAD, 0xA9, 0xE2, 0x9B, 0x69, 0x6A}};
static const GUID lnd_MF_MT_AUDIO_SAMPLES_PER_SECOND = {0x5FAEEAE7, 0x0290, 0x4C31, {0x9E, 0x8A, 0xC5, 0x34, 0xF6, 0x8D, 0x9D, 0xBA}};
static const GUID lnd_MF_MT_AUDIO_BITS_PER_SAMPLE = {0xF2DEB57F, 0x40FA, 0x4764, {0xAA, 0x33, 0xED, 0x4F, 0x2D, 0x1F, 0xF6, 0x69}};
static const GUID lnd_MF_PD_DURATION = {0x6C990D33, 0xBB8E, 0x477A, {0x85, 0x98, 0x0D, 0x5D, 0x96, 0xFC, 0xD8, 0x8A}};
static const GUID lnd_MFMediaType_Audio = {0x73647561, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const GUID lnd_MFAudioFormat_Float = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const GUID lnd_MFAudioFormat_PCM = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const GUID lnd_GUID_NULL = {0};

typedef HRESULT(WINAPI *lnd_create_bytestream_proc)(IStream *, IMFByteStream **);

typedef struct lnd_mf_stream {
    const IStreamVtbl *vt;
    LONG refs;
    lnd_io *io;
} lnd_mf_stream;

static HRESULT STDMETHODCALLTYPE lnd_mfs_QueryInterface(IStream *self, REFIID riid, void **out) {
    if (IsEqualIID(riid, &lnd_IID_IUnknown) || IsEqualIID(riid, &lnd_IID_ISequentialStream) || IsEqualIID(riid, &lnd_IID_IStream)) {
        *out = self;
        InterlockedIncrement(&((lnd_mf_stream *)self)->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE lnd_mfs_AddRef(IStream *self) { return (ULONG)InterlockedIncrement(&((lnd_mf_stream *)self)->refs); }

static ULONG STDMETHODCALLTYPE lnd_mfs_Release(IStream *self) {
    lnd_mf_stream *s = (lnd_mf_stream *)self;
    LONG refs = InterlockedDecrement(&s->refs);
    if (refs == 0) lnd_free(s);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_Read(IStream *self, void *pv, ULONG cb, ULONG *read) {
    lnd_mf_stream *s = (lnd_mf_stream *)self;
    int64_t n = LND_IoRead(s->io, pv, cb);
    if (read) *read = n > 0 ? (ULONG)n : 0;
    return n < 0 ? STG_E_READFAULT : n == cb ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_Write(IStream *self, const void *pv, ULONG cb, ULONG *written) {
    LND_UNUSED(self);
    LND_UNUSED(pv);
    LND_UNUSED(cb);
    if (written) *written = 0;
    return STG_E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_Seek(IStream *self, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *out) {
    lnd_mf_stream *s = (lnd_mf_stream *)self;
    int64_t base = origin == STREAM_SEEK_SET ? 0 : (origin == STREAM_SEEK_CUR ? (int64_t)LND_IoGetPositionBytes(s->io) : (int64_t)LND_IoGetSizeBytes(s->io));
    int64_t target = base + move.QuadPart;
    if (target < 0) return STG_E_INVALIDFUNCTION;
    if ((uint64_t)target > LND_IoGetSizeBytes(s->io)) target = (int64_t)LND_IoGetSizeBytes(s->io);
    LND_IoSeekBytes(s->io, (uint64_t)target);
    if (out) out->QuadPart = (ULONGLONG)target;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_SetSize(IStream *self, ULARGE_INTEGER size) {
    LND_UNUSED(self);
    LND_UNUSED(size);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_CopyTo(IStream *self, IStream *dst, ULARGE_INTEGER cb, ULARGE_INTEGER *read, ULARGE_INTEGER *written) {
    LND_UNUSED(self);
    LND_UNUSED(dst);
    LND_UNUSED(cb);
    LND_UNUSED(read);
    LND_UNUSED(written);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_Commit(IStream *self, DWORD flags) {
    LND_UNUSED(self);
    LND_UNUSED(flags);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_Revert(IStream *self) {
    LND_UNUSED(self);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_LockRegion(IStream *self, ULARGE_INTEGER offset, ULARGE_INTEGER cb, DWORD type) {
    LND_UNUSED(self);
    LND_UNUSED(offset);
    LND_UNUSED(cb);
    LND_UNUSED(type);
    return STG_E_INVALIDFUNCTION;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_UnlockRegion(IStream *self, ULARGE_INTEGER offset, ULARGE_INTEGER cb, DWORD type) {
    LND_UNUSED(self);
    LND_UNUSED(offset);
    LND_UNUSED(cb);
    LND_UNUSED(type);
    return STG_E_INVALIDFUNCTION;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_Stat(IStream *self, STATSTG *stat, DWORD flag) {
    lnd_mf_stream *s = (lnd_mf_stream *)self;
    LND_UNUSED(flag);
    if (!stat) return STG_E_INVALIDPOINTER;
    memset(stat, 0, sizeof *stat);
    stat->type = STGTY_STREAM;
    stat->cbSize.QuadPart = LND_IoGetSizeBytes(s->io);
    stat->grfMode = STGM_READ;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lnd_mfs_Clone(IStream *self, IStream **out) {
    LND_UNUSED(self);
    if (out) *out = nullptr;
    return E_NOTIMPL;
}

static const IStreamVtbl lnd_mf_stream_vt = {
    .QueryInterface = lnd_mfs_QueryInterface,
    .AddRef = lnd_mfs_AddRef,
    .Release = lnd_mfs_Release,
    .Read = lnd_mfs_Read,
    .Write = lnd_mfs_Write,
    .Seek = lnd_mfs_Seek,
    .SetSize = lnd_mfs_SetSize,
    .CopyTo = lnd_mfs_CopyTo,
    .Commit = lnd_mfs_Commit,
    .Revert = lnd_mfs_Revert,
    .LockRegion = lnd_mfs_LockRegion,
    .UnlockRegion = lnd_mfs_UnlockRegion,
    .Stat = lnd_mfs_Stat,
    .Clone = lnd_mfs_Clone,
};

typedef struct lnd_mf_state {
    IMFSourceReader *reader;
    IMFByteStream *bytestream;
    lnd_mf_stream *stream;
    uint32_t channels;
    uint32_t sample_rate_hz;
    uint32_t frame_bytes;
    int32_t format;
    uint64_t length;
    uint64_t pos;
    uint64_t skip_until;
    uint8_t *pending;
    size_t pending_cap;
    size_t pending_frames;
    size_t pending_offset;
    bool eof;
    bool started;
} lnd_mf_state;

static lnd_create_bytestream_proc lnd_mf_create_bytestream;

static bool lnd_mf_startup(void) {
    static lnd_atomic_u32 ready;
    if (lnd_load(&ready) == 2) return true;
    if (lnd_load(&ready) == 3) return false;
    uint32_t expected = 0;
    if (!lnd_cas(&ready, &expected, 1)) {
        while ((expected = lnd_load(&ready)) == 1) Sleep(0);
        return expected == 2;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HMODULE mfplat = LoadLibraryW(L"mfplat.dll");
    lnd_mf_create_bytestream = mfplat ? (lnd_create_bytestream_proc)(void *)GetProcAddress(mfplat, "MFCreateMFByteStreamOnStream") : nullptr;
    if (!lnd_mf_create_bytestream || FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        if (mfplat) FreeLibrary(mfplat);
        lnd_store(&ready, 3);
        return false;
    }
    lnd_store(&ready, 2);
    return true;
}

static HRESULT lnd_mf_open_reader(lnd_io *io, lnd_mf_stream **stream_out, IMFByteStream **bs_out, IMFSourceReader **reader_out) {
    lnd_mf_stream *stream = lnd_alloc_zero(sizeof *stream);
    if (!stream) return E_OUTOFMEMORY;
    stream->vt = &lnd_mf_stream_vt;
    stream->refs = 1;
    stream->io = io;
    LND_IoSeekBytes(io, 0);
    IMFByteStream *bs = nullptr;
    HRESULT hr = lnd_mf_create_bytestream((IStream *)stream, &bs);
    if (FAILED(hr)) {
        lnd_mfs_Release((IStream *)stream);
        return hr;
    }
    IMFAttributes *attrs = nullptr;
    MFCreateAttributes(&attrs, 1);
    IMFSourceReader *reader = nullptr;
    hr = MFCreateSourceReaderFromByteStream(bs, attrs, &reader);
    if (attrs) IMFAttributes_Release(attrs);
    if (FAILED(hr)) {
        IMFByteStream_Release(bs);
        lnd_mfs_Release((IStream *)stream);
        return hr;
    }
    *stream_out = stream;
    *bs_out = bs;
    *reader_out = reader;
    return S_OK;
}

static HRESULT lnd_mf_configure(IMFSourceReader *reader, const GUID *subtype, uint32_t *channels, uint32_t *sample_rate_hz, uint32_t *bits) {
    IMFMediaType *type = nullptr;
    HRESULT hr = MFCreateMediaType(&type);
    if (FAILED(hr)) return hr;
    IMFMediaType_SetGUID(type, &lnd_MF_MT_MAJOR_TYPE, &lnd_MFMediaType_Audio);
    IMFMediaType_SetGUID(type, &lnd_MF_MT_SUBTYPE, subtype);
    hr = IMFSourceReader_SetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type);
    IMFMediaType_Release(type);
    if (FAILED(hr)) return hr;
    IMFMediaType *current = nullptr;
    hr = IMFSourceReader_GetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &current);
    if (FAILED(hr)) return hr;
    UINT32 ch = 0, sr = 0, b = 0;
    IMFMediaType_GetUINT32(current, &lnd_MF_MT_AUDIO_NUM_CHANNELS, &ch);
    IMFMediaType_GetUINT32(current, &lnd_MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
    IMFMediaType_GetUINT32(current, &lnd_MF_MT_AUDIO_BITS_PER_SAMPLE, &b);
    IMFMediaType_Release(current);
    *channels = ch;
    *sample_rate_hz = sr;
    *bits = b;
    return S_OK;
}

static int32_t lnd_mf_probe(LND_IO *io) {
    uint8_t h[16];
    int64_t n = LND_IoRead(io, h, sizeof h);
    if (n < 12) return 0;
    static const uint8_t asf[16] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11, 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
    if (n == 16 && memcmp(h, asf, 16) == 0) return 50;
    if (memcmp(h, "ID3", 3) == 0 || memcmp(h, "fLaC", 4) == 0 || memcmp(h, "OggS", 4) == 0 || memcmp(h, "#!AMR", 5) == 0) return 50;
    if (memcmp(h + 4, "ftyp", 4) == 0) return 50;
    if (h[0] == 0x1A && h[1] == 0x45 && h[2] == 0xDF && h[3] == 0xA3) return 50;
    if (h[0] == 0xFF && (h[1] & 0xE0) == 0xE0 && (h[1] & 0x06) != 0) return 50;
    if (h[0] == 0xFF && (h[1] & 0xF6) == 0xF0) return 50;
    if (h[0] == 0x0B && h[1] == 0x77) return 30;
    if (memcmp(h, "RIFF", 4) == 0 && (memcmp(h + 8, "WAVE", 4) == 0 || memcmp(h + 8, "AVI ", 4) == 0)) return 40;
    if (memcmp(h, "FORM", 4) == 0 && (memcmp(h + 8, "AIFF", 4) == 0 || memcmp(h + 8, "AIFC", 4) == 0)) return 40;
    return 0;
}

static void lnd_mf_close(void *state);

static int32_t lnd_mf_open(LND_IO *io, LND_CODEC_INFO *info, void **state) {
    if (!lnd_mf_startup()) return LND_ERR_UNSUPPORTED;
    lnd_mf_state *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    HRESULT hr = lnd_mf_open_reader(io, &s->stream, &s->bytestream, &s->reader);
    if (FAILED(hr)) {
        lnd_free(s);
        return LND_ERR_FORMAT;
    }
    IMFSourceReader_SetStreamSelection(s->reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    IMFSourceReader_SetStreamSelection(s->reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    uint32_t channels = 0, sample_rate_hz = 0, bits = 0;
    hr = lnd_mf_configure(s->reader, &lnd_MFAudioFormat_Float, &channels, &sample_rate_hz, &bits);
    if (SUCCEEDED(hr) && bits == 32) {
        s->format = LND_FORMAT_F32;
    } else {
        hr = lnd_mf_configure(s->reader, &lnd_MFAudioFormat_PCM, &channels, &sample_rate_hz, &bits);
        if (FAILED(hr) || bits != 16) {
            lnd_mf_close(s);
            return LND_ERR_FORMAT;
        }
        s->format = LND_FORMAT_S16;
    }
    if (channels < 1 || channels > LND_MAX_CHANNELS || sample_rate_hz < 1) {
        lnd_mf_close(s);
        return LND_ERR_FORMAT;
    }
    s->channels = channels;
    s->sample_rate_hz = sample_rate_hz;
    s->frame_bytes = channels * lnd_format_bytes(s->format);
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(IMFSourceReader_GetPresentationAttribute(s->reader, (DWORD)MF_SOURCE_READER_MEDIASOURCE, &lnd_MF_PD_DURATION, &var)) && var.vt == VT_UI8) {
        s->length = (uint64_t)((double)var.uhVal.QuadPart * (double)sample_rate_hz / 10000000.0 + 0.5);
    }
    PropVariantClear(&var);
    info->format = s->format;
    info->channels = channels;
    info->sample_rate_hz = sample_rate_hz;
    info->length_frames = s->length;
    info->seekable = true;
    *state = s;
    return LND_OK;
}

static bool lnd_mf_fill(lnd_mf_state *s) {
    while (!s->eof) {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample *sample = nullptr;
        HRESULT hr = IMFSourceReader_ReadSample(s->reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) || (flags & MF_SOURCE_READERF_ERROR)) {
            if (sample) IMFSample_Release(sample);
            s->eof = true;
            return false;
        }
        if (!sample) continue;
        IMFMediaBuffer *buffer = nullptr;
        if (FAILED(IMFSample_ConvertToContiguousBuffer(sample, &buffer))) {
            IMFSample_Release(sample);
            continue;
        }
        BYTE *data = nullptr;
        DWORD max_len = 0, len = 0;
        if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, &max_len, &len))) {
            size_t frames = len / s->frame_bytes;
            size_t skip = 0;
            if (s->skip_until) {
                uint64_t first = (uint64_t)((double)timestamp * (double)s->sample_rate_hz / 10000000.0 + 0.5);
                if (first < s->skip_until) skip = (size_t)LND_MIN((uint64_t)frames, s->skip_until - first);
            }
            size_t keep = frames - skip;
            if (keep) {
                if (keep * s->frame_bytes > s->pending_cap) {
                    uint8_t *grown = lnd_realloc(s->pending, keep * s->frame_bytes);
                    if (grown) {
                        s->pending = grown;
                        s->pending_cap = keep * s->frame_bytes;
                    } else {
                        keep = 0;
                    }
                }
                if (keep) memcpy(s->pending, data + skip * s->frame_bytes, keep * s->frame_bytes);
                s->pending_frames = keep;
                s->pending_offset = 0;
                s->skip_until = 0;
            }
            IMFMediaBuffer_Unlock(buffer);
            IMFMediaBuffer_Release(buffer);
            IMFSample_Release(sample);
            if (keep) return true;
            continue;
        }
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
    }
    return false;
}

static uint64_t lnd_mf_read(void *state, void *dst, uint64_t frames) {
    lnd_mf_state *s = state;
    uint8_t *out = dst;
    uint64_t total = 0;
    while (total < frames) {
        if (s->pending_offset >= s->pending_frames && !lnd_mf_fill(s)) break;
        size_t avail = s->pending_frames - s->pending_offset;
        size_t n = (size_t)LND_MIN((uint64_t)avail, frames - total);
        memcpy(out + total * s->frame_bytes, s->pending + s->pending_offset * s->frame_bytes, n * s->frame_bytes);
        s->pending_offset += n;
        total += n;
    }
    s->pos += total;
    return total;
}

static int32_t lnd_mf_seek(void *state, uint64_t frame) {
    lnd_mf_state *s = state;
    if (s->length && frame > s->length) frame = s->length;
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = (LONGLONG)((double)frame * 10000000.0 / (double)s->sample_rate_hz + 0.5);
    HRESULT hr = IMFSourceReader_SetCurrentPosition(s->reader, &lnd_GUID_NULL, &var);
    PropVariantClear(&var);
    if (FAILED(hr)) return LND_ERR_UNSUPPORTED;
    IMFSourceReader_Flush(s->reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    s->pending_frames = s->pending_offset = 0;
    s->eof = false;
    s->skip_until = frame;
    s->pos = frame;
    return LND_OK;
}

static void lnd_mf_close(void *state) {
    lnd_mf_state *s = state;
    if (!s) return;
    if (s->reader) IMFSourceReader_Release(s->reader);
    if (s->bytestream) IMFByteStream_Release(s->bytestream);
    if (s->stream) lnd_mfs_Release((IStream *)s->stream);
    lnd_free(s->pending);
    lnd_free(s);
}

const LND_CODEC lnd_codec_mediafoundation = {
    .name = "mediafoundation",
    .extensions = "mp3;wma;m4a;aac;adts;mp4;m4b;asf;wmv;avi;mov;3gp;3g2;flac;alac;amr;mka;mkv;webm;ac3;ec3",
    .flags = LND_CODEC_FLAG_SYSTEM,
    .probe = lnd_mf_probe,
    .open = lnd_mf_open,
    .read = lnd_mf_read,
    .seek = lnd_mf_seek,
    .close = lnd_mf_close,
};
