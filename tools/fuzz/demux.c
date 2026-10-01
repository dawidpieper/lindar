#include "lindar_demux.h"
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t bytes) {
    if (!bytes || bytes > 1024 * 1024) return 0;
    LND_DEMUX_OPTIONS options = {.packet_bytes = 65536};
    LND_DEMUX *d = LND_DemuxCreate(&options);
    if (!d) return 0;
    LND_DemuxSetInit(d, data, bytes);
    uint64_t count = LND_DemuxGetSampleCount(d);
    for (uint64_t i = 0; i < count && i < 1024; i++) {
        LND_DEMUX_SAMPLE sample;
        LND_DemuxGetSample(d, i, &sample);
    }
    for (unsigned pass = 0; pass < 2; pass++) {
        if (LND_DemuxBeginAt(d, data, bytes, pass ? UINT64_MAX - bytes : 0) == LND_OK) {
            LND_DEMUX_PACKET packet;
            for (unsigned i = 0; i < 1024 && LND_DemuxRead(d, &packet) == LND_OK; i++) {
                if (!packet.data && packet.bytes) abort();
            }
            LND_DemuxEnd(d);
        }
        LND_DemuxReset(d);
    }
    LND_DemuxFree(d);
    return 0;
}
