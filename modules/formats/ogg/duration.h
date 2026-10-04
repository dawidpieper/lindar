#pragma once

#include "io/io.h"

#if LND_MODULE_OPUS_DECODER || LND_MODULE_VORBIS_DECODER
#include <ogg/ogg.h>

static inline bool lnd_ogg_duration_end(lnd_io *io, uint64_t end, int32_t serial) {
    uint64_t remaining = LND_MIN(end, UINT64_C(65536));
    if (end > io->size || LND_IoSeekBytes(io, end - remaining) != LND_OK) return false;
    ogg_sync_state sync;
    if (ogg_sync_init(&sync)) return false;
    bool valid = false;
    while (remaining) {
        size_t take = (size_t)LND_MIN(remaining, UINT64_C(4096));
        char *buffer = ogg_sync_buffer(&sync, (long)take);
        if (!buffer || LND_IoRead(io, buffer, take) != (int64_t)take) break;
        remaining -= take;
        ogg_sync_wrote(&sync, (long)take);
        ogg_page page;
        int result;
        while ((result = ogg_sync_pageout(&sync, &page))) {
            if (result < 0) continue;
            valid = !remaining && ogg_page_serialno(&page) == serial && ogg_page_eos(&page) && ogg_page_granulepos(&page) >= 0 && sync.returned == sync.fill;
        }
    }
    ogg_sync_clear(&sync);
    return valid;
}
#endif

int32_t lnd_opus_duration(lnd_io *io, int64_t *duration_us);
int32_t lnd_vorbis_duration(lnd_io *io, int64_t *duration_us);
