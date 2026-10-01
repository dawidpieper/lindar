#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>

#include "src/alloc.h"
#include "src/atomic.h"
#include "utility/log/log.h"
#include "src/thread.h"
#include "src/format.h"
#include "io/devices/backend.h"

#include <string.h>
#include <wchar.h>

static const CLSID lnd_CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID lnd_IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID lnd_IID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID lnd_IID_IMMNotificationClient = {0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
static const IID lnd_IID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const IID lnd_IID_IAudioClient3 = {0x7ED4EE07, 0x8E67, 0x4CD4, {0x8C, 0x1A, 0x2B, 0x7A, 0x59, 0x87, 0xAD, 0x42}};
static const IID lnd_IID_IAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
static const IID lnd_IID_IAudioCaptureClient = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
static const GUID lnd_KSDATAFORMAT_SUBTYPE_PCM = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const GUID lnd_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const PROPERTYKEY lnd_PKEY_Device_FriendlyName = {{0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};

#define LND_HNS_PER_SEC 10000000.0

typedef struct lnd_wasapi_notify {
    const IMMNotificationClientVtbl *vt;
    LONG refs;
    lnd_backend *backend;
} lnd_wasapi_notify;

typedef struct lnd_wasapi_backend {
    IMMDeviceEnumerator *enumerator;
    lnd_wasapi_notify notify;
    lnd_atomic_u32 events;
    bool registered;
} lnd_wasapi_backend;

struct lnd_stream {
    lnd_stream_cfg cfg;
    lnd_stream_proc proc;
    void *user;
    IAudioClient *client;
    IAudioClient3 *client3;
    IAudioRenderClient *render;
    IAudioCaptureClient *capture;
    BYTE *silence;
    HANDLE event;
    HANDLE stop_event;
    lnd_thread thread;
    uint32_t buffer_frames;
    lnd_atomic_u32 failed;
    bool is_capture;
    bool running;
};

static void lnd_com_init(void) { CoInitializeEx(nullptr, COINIT_MULTITHREADED); }

static char *lnd_wide_to_utf8(const WCHAR *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return lnd_strdup("");
    char *s = lnd_alloc((size_t)n);
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, nullptr, nullptr);
    return s;
}

static WCHAR *lnd_wide_dup(const WCHAR *w) {
    size_t n = (wcslen(w) + 1) * sizeof(WCHAR);
    WCHAR *d = lnd_alloc(n);
    if (d) memcpy(d, w, n);
    return d;
}

static REFERENCE_TIME lnd_frames_to_hns(uint32_t frames, uint32_t sample_rate_hz) { return (REFERENCE_TIME)((double)frames * LND_HNS_PER_SEC / (double)sample_rate_hz + 0.5); }

static uint32_t lnd_hns_to_frames(REFERENCE_TIME hns, uint32_t sample_rate_hz) { return (uint32_t)((double)hns * (double)sample_rate_hz / LND_HNS_PER_SEC + 0.5); }

static void lnd_wasapi_build_format(WAVEFORMATEXTENSIBLE *wf, uint32_t sample_rate_hz, uint32_t channels, int32_t format) {
    uint32_t bits = lnd_format_bits(format);
    memset(wf, 0, sizeof *wf);
    wf->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wf->Format.nChannels = (WORD)channels;
    wf->Format.nSamplesPerSec = sample_rate_hz;
    wf->Format.wBitsPerSample = (WORD)bits;
    wf->Format.nBlockAlign = (WORD)(channels * bits / 8);
    wf->Format.nAvgBytesPerSec = sample_rate_hz * wf->Format.nBlockAlign;
    wf->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wf->Samples.wValidBitsPerSample = (WORD)bits;
    wf->dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER
                                      : (channels == 2 ? SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT : (channels >= 32 ? 0xFFFFFFFFu : (1u << channels) - 1));
    wf->SubFormat = lnd_format_is_float(format) ? lnd_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : lnd_KSDATAFORMAT_SUBTYPE_PCM;
}

static int32_t lnd_wasapi_format_from_wave(const WAVEFORMATEX *wf) {
    bool is_float = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    uint32_t bits = wf->wBitsPerSample;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)wf;
        is_float = IsEqualGUID(&ext->SubFormat, &lnd_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    if (is_float) return bits == 64 ? LND_FORMAT_F64 : LND_FORMAT_F32;
    switch (bits) {
    case 8:
        return LND_FORMAT_U8;
    case 16:
        return LND_FORMAT_S16;
    case 24:
        return LND_FORMAT_S24;
    case 32:
        return LND_FORMAT_S32;
    default:
        return LND_FORMAT_NONE;
    }
}

static void lnd_wasapi_signal(lnd_wasapi_backend *wb, uint32_t flags) {
    atomic_fetch_or_explicit(&wb->events, flags, memory_order_acq_rel);
    if (wb->notify.backend && wb->notify.backend->wake) wb->notify.backend->wake();
}

static HRESULT STDMETHODCALLTYPE lnd_notify_QueryInterface(IMMNotificationClient *self, REFIID riid, void **out) {
    if (IsEqualIID(riid, &lnd_IID_IUnknown) || IsEqualIID(riid, &lnd_IID_IMMNotificationClient)) {
        *out = self;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE lnd_notify_AddRef(IMMNotificationClient *self) { return (ULONG)InterlockedIncrement(&((lnd_wasapi_notify *)self)->refs); }

static ULONG STDMETHODCALLTYPE lnd_notify_Release(IMMNotificationClient *self) { return (ULONG)InterlockedDecrement(&((lnd_wasapi_notify *)self)->refs); }

static HRESULT STDMETHODCALLTYPE lnd_notify_OnDeviceStateChanged(IMMNotificationClient *self, LPCWSTR id, DWORD state) {
    LND_UNUSED(id);
    LND_UNUSED(state);
    lnd_wasapi_signal(((lnd_wasapi_notify *)self)->backend->data, LND_BACKEND_EVENT_DEVICES);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lnd_notify_OnDeviceAdded(IMMNotificationClient *self, LPCWSTR id) {
    LND_UNUSED(id);
    lnd_wasapi_signal(((lnd_wasapi_notify *)self)->backend->data, LND_BACKEND_EVENT_DEVICES);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lnd_notify_OnDeviceRemoved(IMMNotificationClient *self, LPCWSTR id) {
    LND_UNUSED(id);
    lnd_wasapi_signal(((lnd_wasapi_notify *)self)->backend->data, LND_BACKEND_EVENT_DEVICES);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lnd_notify_OnDefaultDeviceChanged(IMMNotificationClient *self, EDataFlow flow, ERole role, LPCWSTR id) {
    LND_UNUSED(id);
    if (role != eConsole) return S_OK;
    lnd_wasapi_signal(((lnd_wasapi_notify *)self)->backend->data, flow == eRender ? LND_BACKEND_EVENT_DEFAULT_OUTPUT : LND_BACKEND_EVENT_DEFAULT_INPUT);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE lnd_notify_OnPropertyValueChanged(IMMNotificationClient *self, LPCWSTR id, const PROPERTYKEY key) {
    LND_UNUSED(self);
    LND_UNUSED(id);
    LND_UNUSED(key);
    return S_OK;
}

static const IMMNotificationClientVtbl lnd_notify_vt = {
    .QueryInterface = lnd_notify_QueryInterface,
    .AddRef = lnd_notify_AddRef,
    .Release = lnd_notify_Release,
    .OnDeviceStateChanged = lnd_notify_OnDeviceStateChanged,
    .OnDeviceAdded = lnd_notify_OnDeviceAdded,
    .OnDeviceRemoved = lnd_notify_OnDeviceRemoved,
    .OnDefaultDeviceChanged = lnd_notify_OnDefaultDeviceChanged,
    .OnPropertyValueChanged = lnd_notify_OnPropertyValueChanged,
};

static int32_t lnd_wasapi_init(lnd_backend *b) {
    lnd_com_init();
    lnd_wasapi_backend *wb = lnd_alloc_zero(sizeof *wb);
    if (!wb) return LND_ERR_OUT_OF_MEMORY;
    if (FAILED(CoCreateInstance(&lnd_CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, &lnd_IID_IMMDeviceEnumerator, (void **)&wb->enumerator))) {
        lnd_free(wb);
        return LND_ERR_EXTERNAL;
    }
    wb->notify.vt = &lnd_notify_vt;
    wb->notify.refs = 1;
    wb->notify.backend = b;
    b->data = wb;
    wb->registered = SUCCEEDED(IMMDeviceEnumerator_RegisterEndpointNotificationCallback(wb->enumerator, (IMMNotificationClient *)&wb->notify));
    return LND_OK;
}

static void lnd_wasapi_free(lnd_backend *b) {
    lnd_wasapi_backend *wb = b->data;
    if (!wb) return;
    if (wb->registered) IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(wb->enumerator, (IMMNotificationClient *)&wb->notify);
    IMMDeviceEnumerator_Release(wb->enumerator);
    lnd_free(wb);
    b->data = nullptr;
}

static uint32_t lnd_wasapi_poll(lnd_backend *b) {
    lnd_wasapi_backend *wb = b->data;
    return wb ? lnd_exchange(&wb->events, 0) : 0;
}

static IMMDeviceEnumerator *lnd_wasapi_enumerator(lnd_backend *b) {
    lnd_wasapi_backend *wb = b->data;
    lnd_com_init();
    if (wb && wb->enumerator) {
        IMMDeviceEnumerator_AddRef(wb->enumerator);
        return wb->enumerator;
    }
    IMMDeviceEnumerator *e = nullptr;
    if (FAILED(CoCreateInstance(&lnd_CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, &lnd_IID_IMMDeviceEnumerator, (void **)&e))) return nullptr;
    return e;
}

static void lnd_wasapi_probe(lnd_device *d, IMMDevice *dev) {
    IAudioClient *client = nullptr;
    if (FAILED(IMMDevice_Activate(dev, &lnd_IID_IAudioClient, CLSCTX_ALL, nullptr, (void **)&client))) return;
    WAVEFORMATEX *mix = nullptr;
    if (SUCCEEDED(IAudioClient_GetMixFormat(client, &mix)) && mix) {
        d->sample_rate_hz = mix->nSamplesPerSec;
        d->channels = mix->nChannels;
        d->format = lnd_wasapi_format_from_wave(mix);
        lnd_device_add_mode(d, d->sample_rate_hz, d->channels, LND_FORMAT_F32, LND_DEVICE_FLAG_SHARED);
        REFERENCE_TIME def = 0, min = 0;
        if (SUCCEEDED(IAudioClient_GetDevicePeriod(client, &def, &min))) {
            d->default_period = lnd_hns_to_frames(def, d->sample_rate_hz);
            d->min_period = lnd_hns_to_frames(min, d->sample_rate_hz);
        }
        IAudioClient3 *client3 = nullptr;
        if (SUCCEEDED(IAudioClient_QueryInterface(client, &lnd_IID_IAudioClient3, (void **)&client3)) && client3) {
            UINT32 defp = 0, fund = 0, minp = 0, maxp = 0;
            if (SUCCEEDED(IAudioClient3_GetSharedModeEnginePeriod(client3, mix, &defp, &fund, &minp, &maxp)) && minp) d->min_period = minp;
            IAudioClient3_Release(client3);
        }
        static const uint32_t rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
        static const int32_t formats[] = {LND_FORMAT_S16, LND_FORMAT_S24, LND_FORMAT_S32, LND_FORMAT_F32};
        for (size_t r = 0; r < LND_COUNTOF(rates); r++) {
            for (size_t f = 0; f < LND_COUNTOF(formats); f++) {
                WAVEFORMATEXTENSIBLE wf;
                lnd_wasapi_build_format(&wf, rates[r], d->channels, formats[f]);
                if (IAudioClient_IsFormatSupported(client, AUDCLNT_SHAREMODE_EXCLUSIVE, &wf.Format, nullptr) == S_OK) {
                    lnd_device_add_mode(d, rates[r], d->channels, formats[f], LND_DEVICE_FLAG_EXCLUSIVE);
                    d->flags |= LND_DEVICE_FLAG_EXCLUSIVE;
                }
            }
        }
        CoTaskMemFree(mix);
    }
    IAudioClient_Release(client);
}

static int32_t lnd_wasapi_enumerate(lnd_backend *b, int32_t type, lnd_device_list *out) {
    IMMDeviceEnumerator *e = lnd_wasapi_enumerator(b);
    if (!e) return LND_ERR_EXTERNAL;
    EDataFlow flow = type == LND_DEVICE_OUTPUT ? eRender : eCapture;
    WCHAR *default_id = nullptr;
    IMMDevice *dd = nullptr;
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(e, flow, eConsole, &dd)) && dd) {
        IMMDevice_GetId(dd, &default_id);
        IMMDevice_Release(dd);
    }
    IMMDeviceCollection *col = nullptr;
    int32_t result = LND_OK;
    if (FAILED(IMMDeviceEnumerator_EnumAudioEndpoints(e, flow, DEVICE_STATE_ACTIVE, &col)) || !col) {
        result = LND_ERR_EXTERNAL;
        goto done;
    }
    UINT count = 0;
    IMMDeviceCollection_GetCount(col, &count);
    for (UINT i = 0; i < count && result == LND_OK; i++) {
        IMMDevice *dev = nullptr;
        if (FAILED(IMMDeviceCollection_Item(col, i, &dev)) || !dev) continue;
        WCHAR *wid = nullptr;
        char *name = nullptr;
        char *id = nullptr;
        if (SUCCEEDED(IMMDevice_GetId(dev, &wid)) && wid) id = lnd_wide_to_utf8(wid);
        IPropertyStore *store = nullptr;
        if (SUCCEEDED(IMMDevice_OpenPropertyStore(dev, STGM_READ, &store)) && store) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(IPropertyStore_GetValue(store, &lnd_PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) name = lnd_wide_to_utf8(pv.pwszVal);
            PropVariantClear(&pv);
            IPropertyStore_Release(store);
        }
        lnd_device *d = lnd_device_new(type, name ? name : "Unknown", id ? id : "");
        lnd_free(name);
        lnd_free(id);
        if (!d || !wid) {
            lnd_device_free(d);
            result = LND_ERR_OUT_OF_MEMORY;
        } else {
            d->backend_data = lnd_wide_dup(wid);
            d->backend_data_free = lnd_free;
            d->flags = LND_DEVICE_FLAG_SHARED | LND_DEVICE_FLAG_MULTI_INSTANCE | (type == LND_DEVICE_OUTPUT ? LND_DEVICE_FLAG_LOOPBACK : 0);
            if (default_id && wcscmp(default_id, wid) == 0) d->flags |= LND_DEVICE_FLAG_DEFAULT;
            d->max_instances = 0;
            lnd_wasapi_probe(d, dev);
            result = lnd_device_list_push(out, d);
            if (result != LND_OK) lnd_device_free(d);
        }
        if (wid) CoTaskMemFree(wid);
        IMMDevice_Release(dev);
    }
done:
    if (col) IMMDeviceCollection_Release(col);
    if (default_id) CoTaskMemFree(default_id);
    IMMDeviceEnumerator_Release(e);
    return result;
}

static HRESULT lnd_wasapi_activate(lnd_backend *b, lnd_device *d, IAudioClient **client, IAudioClient3 **client3) {
    IMMDeviceEnumerator *e = lnd_wasapi_enumerator(b);
    if (!e) return E_FAIL;
    IMMDevice *dev = nullptr;
    HRESULT hr = IMMDeviceEnumerator_GetDevice(e, (LPCWSTR)d->backend_data, &dev);
    if (SUCCEEDED(hr)) {
        hr = IMMDevice_Activate(dev, &lnd_IID_IAudioClient, CLSCTX_ALL, nullptr, (void **)client);
        IMMDevice_Release(dev);
    }
    IMMDeviceEnumerator_Release(e);
    *client3 = nullptr;
    if (SUCCEEDED(hr)) IAudioClient_QueryInterface(*client, &lnd_IID_IAudioClient3, (void **)client3);
    return hr;
}

static void lnd_wasapi_release_clients(lnd_stream *s) {
    if (s->render) IAudioRenderClient_Release(s->render);
    if (s->capture) IAudioCaptureClient_Release(s->capture);
    if (s->client3) IAudioClient3_Release(s->client3);
    if (s->client) IAudioClient_Release(s->client);
    s->render = nullptr;
    s->capture = nullptr;
    s->client3 = nullptr;
    s->client = nullptr;
}

static void lnd_wasapi_capture_thread(void *user) {
    lnd_stream *s = user;
    lnd_com_init();
    DWORD task_index = 0;
    HANDLE task = nullptr;
    if (s->cfg.priority == LND_DEVICE_THREAD_PRIORITY_REALTIME) {
        task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
        if (!task) lnd_thread_set_priority(LND_DEVICE_THREAD_PRIORITY_REALTIME);
    }
    HANDLE handles[2] = {s->stop_event, s->event};
    uint32_t period_ms = (uint32_t)((uint64_t)s->cfg.period_frames * 1000 / (s->cfg.sample_rate_hz ? s->cfg.sample_rate_hz : 48000));
    DWORD timeout = s->cfg.loopback ? LND_MAX(1u, period_ms / 2) : 2000;
    HRESULT hr = IAudioClient_Start(s->client);
    if (FAILED(hr)) {
        lnd_store(&s->failed, 1);
        LND_LOG_E("wasapi capture start failed: 0x%08lx", (unsigned long)hr);
        goto out;
    }
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, handles, FALSE, timeout);
        if (r == WAIT_OBJECT_0) break;
        if (r != WAIT_OBJECT_0 + 1 && !(r == WAIT_TIMEOUT && s->cfg.loopback)) {
            lnd_store(&s->failed, 1);
            LND_LOG_E("wasapi capture wait failed: %lu", (unsigned long)r);
            break;
        }
        UINT32 packet = 0;
        bool bad = false;
        while (SUCCEEDED(hr = IAudioCaptureClient_GetNextPacketSize(s->capture, &packet)) && packet) {
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = IAudioCaptureClient_GetBuffer(s->capture, &data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                bad = true;
                break;
            }
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                for (UINT32 done = 0; done < frames;) {
                    UINT32 n = LND_MIN(frames - done, s->buffer_frames);
                    s->proc(s->user, s->silence, n);
                    done += n;
                }
            } else if (frames) {
                s->proc(s->user, data, frames);
            }
            IAudioCaptureClient_ReleaseBuffer(s->capture, frames);
        }
        if (bad || FAILED(hr)) {
            lnd_store(&s->failed, 1);
            LND_LOG_E("wasapi capture buffer failed: 0x%08lx", (unsigned long)hr);
            break;
        }
    }
    IAudioClient_Stop(s->client);
out:
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

static void lnd_wasapi_thread(void *user) {
    lnd_stream *s = user;
    lnd_com_init();
    DWORD task_index = 0;
    HANDLE task = nullptr;
    if (s->cfg.priority == LND_DEVICE_THREAD_PRIORITY_REALTIME) {
        task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
        if (!task) lnd_thread_set_priority(LND_DEVICE_THREAD_PRIORITY_REALTIME);
    }
    HANDLE handles[2] = {s->stop_event, s->event};
    BYTE *data = nullptr;
    UINT32 padding = 0;
    uint32_t frames = s->buffer_frames;
    if (!s->cfg.exclusive && SUCCEEDED(IAudioClient_GetCurrentPadding(s->client, &padding))) frames -= padding;
    if (frames && SUCCEEDED(IAudioRenderClient_GetBuffer(s->render, frames, &data))) {
        s->proc(s->user, data, frames);
        IAudioRenderClient_ReleaseBuffer(s->render, frames, 0);
    }
    HRESULT hr = IAudioClient_Start(s->client);
    if (FAILED(hr)) {
        lnd_store(&s->failed, 1);
        LND_LOG_E("wasapi start failed: 0x%08lx", (unsigned long)hr);
        goto out;
    }
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, handles, FALSE, 2000);
        if (r == WAIT_OBJECT_0) break;
        if (r != WAIT_OBJECT_0 + 1) {
            lnd_store(&s->failed, 1);
            LND_LOG_E("wasapi event wait failed: %lu", (unsigned long)r);
            break;
        }
        frames = s->buffer_frames;
        if (!s->cfg.exclusive) {
            hr = IAudioClient_GetCurrentPadding(s->client, &padding);
            if (FAILED(hr)) {
                lnd_store(&s->failed, 1);
                LND_LOG_E("wasapi padding failed: 0x%08lx", (unsigned long)hr);
                break;
            }
            frames -= padding;
        }
        if (!frames) continue;
        hr = IAudioRenderClient_GetBuffer(s->render, frames, &data);
        if (FAILED(hr)) {
            lnd_store(&s->failed, 1);
            LND_LOG_E("wasapi get buffer failed: 0x%08lx", (unsigned long)hr);
            break;
        }
        s->proc(s->user, data, frames);
        IAudioRenderClient_ReleaseBuffer(s->render, frames, 0);
    }
    IAudioClient_Stop(s->client);
out:
    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

static int32_t lnd_wasapi_pick_exclusive(lnd_stream *s, uint32_t sample_rate_hz, uint32_t channels, int32_t *format, WAVEFORMATEXTENSIBLE *wf) {
    const int32_t candidates[] = {*format, LND_FORMAT_F32, LND_FORMAT_S32, LND_FORMAT_S24, LND_FORMAT_S16};
    for (size_t i = 0; i < LND_COUNTOF(candidates); i++) {
        if (!lnd_format_valid(candidates[i])) continue;
        lnd_wasapi_build_format(wf, sample_rate_hz, channels, candidates[i]);
        if (IAudioClient_IsFormatSupported(s->client, AUDCLNT_SHAREMODE_EXCLUSIVE, &wf->Format, nullptr) == S_OK) {
            *format = candidates[i];
            return LND_OK;
        }
    }
    return LND_ERR_FORMAT;
}

static int32_t lnd_wasapi_open_common(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out, bool capture) {
    lnd_stream *s = lnd_alloc_zero(sizeof *s);
    if (!s) return LND_ERR_OUT_OF_MEMORY;
    s->proc = proc;
    s->user = user;
    s->is_capture = capture;
    int32_t result = LND_ERR_EXTERNAL;
    WAVEFORMATEX *mix = nullptr;
    if (capture && cfg->loopback && cfg->exclusive) {
        result = LND_ERR_UNSUPPORTED;
        goto fail;
    }
    if (!capture) cfg->loopback = false;
    HRESULT hr = lnd_wasapi_activate(b, d, &s->client, &s->client3);
    if (FAILED(hr)) {
        LND_LOG_E("wasapi activate failed: 0x%08lx", (unsigned long)hr);
        goto fail;
    }
    if (FAILED(IAudioClient_GetMixFormat(s->client, &mix)) || !mix) goto fail;
    uint32_t mix_rate = mix->nSamplesPerSec;
    uint32_t mix_channels = mix->nChannels;
    int32_t mix_format = lnd_wasapi_format_from_wave(mix);
    uint32_t sample_rate_hz = cfg->sample_rate_hz ? cfg->sample_rate_hz : mix_rate;
    uint32_t channels = cfg->channels ? cfg->channels : mix_channels;
    int32_t format = cfg->format ? cfg->format : LND_FORMAT_F32;
    REFERENCE_TIME def_period = 0, min_period = 0;
    IAudioClient_GetDevicePeriod(s->client, &def_period, &min_period);
    uint32_t period_frames = 0;
    WAVEFORMATEXTENSIBLE wf;
    if (cfg->exclusive) {
        result = lnd_wasapi_pick_exclusive(s, sample_rate_hz, channels, &format, &wf);
        if (result != LND_OK) goto fail;
        result = LND_ERR_EXTERNAL;
        REFERENCE_TIME period = cfg->period_frames ? lnd_frames_to_hns(cfg->period_frames, sample_rate_hz) : def_period;
        if (period < min_period) period = min_period;
        hr = IAudioClient_Initialize(s->client, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, &wf.Format, nullptr);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 aligned = 0;
            IAudioClient_GetBufferSize(s->client, &aligned);
            period = lnd_frames_to_hns(aligned, sample_rate_hz);
            lnd_wasapi_release_clients(s);
            hr = lnd_wasapi_activate(b, d, &s->client, &s->client3);
            if (SUCCEEDED(hr))
                hr = IAudioClient_Initialize(s->client, AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, &wf.Format, nullptr);
        }
    } else {
        bool same = sample_rate_hz == mix_rate && channels == mix_channels && format == LND_FORMAT_F32 && mix_format == LND_FORMAT_F32;
        lnd_wasapi_build_format(&wf, sample_rate_hz, channels, format);
        period_frames = lnd_hns_to_frames(def_period, sample_rate_hz);
        UINT32 defp = 0, fund = 0, minp = 0, maxp = 0;
        if (!capture && same && s->client3 && cfg->period_frames &&
            SUCCEEDED(IAudioClient3_GetSharedModeEnginePeriod(s->client3, &wf.Format, &defp, &fund, &minp, &maxp)) && fund) {
            UINT32 p = LND_CLAMP(cfg->period_frames, minp, maxp);
            p = fund * ((p + fund / 2) / fund);
            p = LND_CLAMP(p, minp, maxp);
            hr = IAudioClient3_InitializeSharedAudioStream(s->client3, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, p, &wf.Format, nullptr);
            if (SUCCEEDED(hr)) period_frames = p;
        } else {
            DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | (cfg->loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0) |
                          (same ? 0 : AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY);
            REFERENCE_TIME period = cfg->period_frames ? lnd_frames_to_hns(cfg->period_frames, sample_rate_hz) : def_period;
            hr = IAudioClient_Initialize(s->client, AUDCLNT_SHAREMODE_SHARED, flags, period * cfg->periods, 0, &wf.Format, nullptr);
        }
    }
    if (FAILED(hr)) {
        LND_LOG_E("wasapi initialize failed: 0x%08lx (%u Hz, %u ch, %s%s)", (unsigned long)hr, sample_rate_hz, channels, lnd_format_name(format),
                  cfg->exclusive ? ", exclusive" : "");
        goto fail;
    }
    UINT32 buffer_frames = 0;
    if (FAILED(IAudioClient_GetBufferSize(s->client, &buffer_frames))) goto fail;
    s->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    s->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!s->event || !s->stop_event) goto fail;
    if (FAILED(IAudioClient_SetEventHandle(s->client, s->event))) goto fail;
    if (capture) {
        if (FAILED(IAudioClient_GetService(s->client, &lnd_IID_IAudioCaptureClient, (void **)&s->capture))) goto fail;
        s->silence = lnd_alloc_zero((size_t)buffer_frames * channels * lnd_format_bytes(format));
        if (!s->silence) {
            result = LND_ERR_OUT_OF_MEMORY;
            goto fail;
        }
    } else if (FAILED(IAudioClient_GetService(s->client, &lnd_IID_IAudioRenderClient, (void **)&s->render))) {
        goto fail;
    }
    REFERENCE_TIME stream_latency = 0;
    IAudioClient_GetStreamLatency(s->client, &stream_latency);
    s->buffer_frames = buffer_frames;
    cfg->sample_rate_hz = sample_rate_hz;
    cfg->channels = channels;
    cfg->format = format;
    cfg->buffer_frames = buffer_frames;
    cfg->period_frames = cfg->exclusive ? buffer_frames : period_frames;
    cfg->latency_frames = buffer_frames + lnd_hns_to_frames(stream_latency, sample_rate_hz);
    s->cfg = *cfg;
    CoTaskMemFree(mix);
    *out = s;
    return LND_OK;
fail:
    if (mix) CoTaskMemFree(mix);
    lnd_wasapi_release_clients(s);
    if (s->event) CloseHandle(s->event);
    if (s->stop_event) CloseHandle(s->stop_event);
    lnd_free(s->silence);
    lnd_free(s);
    return result;
}

static int32_t lnd_wasapi_open(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    return lnd_wasapi_open_common(b, d, cfg, proc, user, out, false);
}

static int32_t lnd_wasapi_open_capture(lnd_backend *b, lnd_device *d, lnd_stream_cfg *cfg, lnd_stream_proc proc, void *user, lnd_stream **out) {
    return lnd_wasapi_open_common(b, d, cfg, proc, user, out, true);
}

static int32_t lnd_wasapi_start(lnd_stream *s) {
    if (s->running) return LND_OK;
    ResetEvent(s->stop_event);
    lnd_store(&s->failed, 0);
    int32_t r = lnd_thread_create(&s->thread, s->is_capture ? lnd_wasapi_capture_thread : lnd_wasapi_thread, s);
    if (r == LND_OK) s->running = true;
    return r;
}

static int32_t lnd_wasapi_stop(lnd_stream *s) {
    if (!s->running) return LND_OK;
    SetEvent(s->stop_event);
    lnd_thread_join(&s->thread);
    IAudioClient_Reset(s->client);
    s->running = false;
    return LND_OK;
}

static int32_t lnd_wasapi_status(lnd_stream *s) { return lnd_load(&s->failed) ? LND_ERR_EXTERNAL : LND_OK; }

static void lnd_wasapi_close(lnd_stream *s) {
    lnd_wasapi_stop(s);
    lnd_wasapi_release_clients(s);
    CloseHandle(s->event);
    CloseHandle(s->stop_event);
    lnd_free(s->silence);
    lnd_free(s);
}

const lnd_backend_vt lnd_backend_wasapi_vt = {
    .name = "wasapi",
    .init = lnd_wasapi_init,
    .free = lnd_wasapi_free,
    .enumerate = lnd_wasapi_enumerate,
    .poll = lnd_wasapi_poll,
    .open = lnd_wasapi_open,
    .open_capture = lnd_wasapi_open_capture,
    .start = lnd_wasapi_start,
    .stop = lnd_wasapi_stop,
    .status = lnd_wasapi_status,
    .close = lnd_wasapi_close,
};
