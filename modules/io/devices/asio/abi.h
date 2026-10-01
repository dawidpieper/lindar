#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#pragma pack(push, 8)
#include "cwasio/src/cwASIOtypes.h"
#pragma pack(pop)

#if defined(__i386__) || defined(_M_IX86)
#define LND_ASIO_METHOD __attribute__((thiscall))
#else
#define LND_ASIO_METHOD
#endif

typedef struct lnd_asio_abi lnd_asio_abi;

typedef struct lnd_asio_vt {
    HRESULT(WINAPI *query_interface)(lnd_asio_abi *, const GUID *, void **);
    ULONG(WINAPI *add_ref)(lnd_asio_abi *);
    ULONG(WINAPI *release)(lnd_asio_abi *);
    cwASIOBool(LND_ASIO_METHOD *init)(lnd_asio_abi *, void *);
    void(LND_ASIO_METHOD *getDriverName)(lnd_asio_abi *, char *);
    long(LND_ASIO_METHOD *getDriverVersion)(lnd_asio_abi *);
    void(LND_ASIO_METHOD *getErrorMessage)(lnd_asio_abi *, char *);
    cwASIOError(LND_ASIO_METHOD *start)(lnd_asio_abi *);
    cwASIOError(LND_ASIO_METHOD *stop)(lnd_asio_abi *);
    cwASIOError(LND_ASIO_METHOD *getChannels)(lnd_asio_abi *, long *, long *);
    cwASIOError(LND_ASIO_METHOD *getLatencies)(lnd_asio_abi *, long *, long *);
    cwASIOError(LND_ASIO_METHOD *getBufferSize)(lnd_asio_abi *, long *, long *, long *, long *);
    cwASIOError(LND_ASIO_METHOD *canSampleRate)(lnd_asio_abi *, double);
    cwASIOError(LND_ASIO_METHOD *getSampleRate)(lnd_asio_abi *, double *);
    cwASIOError(LND_ASIO_METHOD *setSampleRate)(lnd_asio_abi *, double);
    cwASIOError(LND_ASIO_METHOD *getClockSources)(lnd_asio_abi *, struct cwASIOClockSource *, long *);
    cwASIOError(LND_ASIO_METHOD *setClockSource)(lnd_asio_abi *, long);
    cwASIOError(LND_ASIO_METHOD *getSamplePosition)(lnd_asio_abi *, cwASIOSamples *, cwASIOTimeStamp *);
    cwASIOError(LND_ASIO_METHOD *getChannelInfo)(lnd_asio_abi *, struct cwASIOChannelInfo *);
    cwASIOError(LND_ASIO_METHOD *createBuffers)(lnd_asio_abi *, struct cwASIOBufferInfo *, long, long, const struct cwASIOCallbacks *);
    cwASIOError(LND_ASIO_METHOD *disposeBuffers)(lnd_asio_abi *);
    cwASIOError(LND_ASIO_METHOD *controlPanel)(lnd_asio_abi *);
    cwASIOError(LND_ASIO_METHOD *future)(lnd_asio_abi *, long, void *);
    cwASIOError(LND_ASIO_METHOD *outputReady)(lnd_asio_abi *);
} lnd_asio_vt;

struct lnd_asio_abi {
    const lnd_asio_vt *vt;
};

static_assert(sizeof(long) == 4);
static_assert(sizeof(cwASIOSamples) == 8);
static_assert(sizeof(struct cwASIOTimeInfo) == 48);
static_assert(offsetof(struct cwASIOTime, timeInfo) == 16);
