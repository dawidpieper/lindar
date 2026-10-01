#include "lindar.h"
#include "lindar_metadata_id3v1.h"
#include "lindar_metadata_id3v2.h"
#include "lindar_metadata_comments.h"
#include "lindar_metadata_wave.h"
#include "lindar_metadata_io.h"

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    LND_METADATA_LIMITS limits = {.bytes = 1024 * 1024, .fields = 256, .chapters = 64, .blobs = 128};
    LND_METADATA *m = LND_MetadataCreate(&limits);
    if (!m) return 0;
    for (unsigned format = 1; format <= 4; ++format) {
        int32_t r = format == 1   ? LND_MetadataId3v1Read(m, data, size)
                    : format == 2 ? LND_MetadataId3v2Read(m, data, size, nullptr)
                    : format == 3 ? LND_MetadataOpusRead(m, data, size)
                                  : LND_MetadataWaveRead(m, data, size);
        if (!r) {
            void *output = nullptr;
            size_t bytes = 0;
            if (format == 1)
                r = LND_MetadataId3v1CreateBuffer(m, LND_METADATA_DROP_UNSUPPORTED, &output, &bytes);
            else if (format == 2)
                r = LND_MetadataId3v2CreateBuffer(m, 0, LND_METADATA_DROP_UNSUPPORTED, &output, &bytes);
            else if (format == 3)
                r = LND_MetadataOpusCreateBuffer(m, LND_METADATA_DROP_UNSUPPORTED, &output, &bytes);
            else
                r = LND_MetadataWaveCreateBuffer(m, 48000, LND_METADATA_DROP_UNSUPPORTED, &output, &bytes);
            LND_MetadataBufferFree(output);
        }
        LND_MetadataClear(m);
    }
    LND_MetadataReadMemory(m, data, size, LND_METADATA_AUTO);
    LND_MetadataFree(m);
    return 0;
}
