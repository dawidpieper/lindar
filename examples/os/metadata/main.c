#include "lindar.h"
#include "lindar_metadata_io.h"
#include "lindar_file_io.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: metadata input-file\n");
        return 1;
    }
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_IO *input = LND_IoOpenFile(argv[1]);
    LND_METADATA *metadata = LND_MetadataCreate(nullptr);
    int32_t result = !input ? LND_ERR_IO : !metadata ? LND_ERR_OUT_OF_MEMORY : LND_MetadataReadIo(metadata, input, LND_METADATA_AUTO);
    if (result == LND_OK) {
        for (uint32_t i = 0; i < LND_MetadataGetFieldCount(metadata); ++i) {
            const LND_METADATA_FIELD *field = LND_MetadataGetField(metadata, i);
            printf("%s=%s\n", field->key, field->value);
        }
        for (uint32_t i = 0; i < LND_MetadataGetChapterCount(metadata); ++i) {
            const LND_METADATA_CHAPTER *chapter = LND_MetadataGetChapter(metadata, i);
            uint64_t ms = chapter->start_us / 1000;
            printf("%02llu:%02u:%02u.%03u  %s\n", (unsigned long long)(ms / 3600000), (unsigned)(ms / 60000 % 60), (unsigned)(ms / 1000 % 60),
                   (unsigned)(ms % 1000), chapter->title);
        }
    } else
        fprintf(stderr, "%s (%d)\n", LND_ErrorGetString(result), result);
    LND_MetadataFree(metadata);
    LND_IoFree(input);
    LND_LibraryFree();
    return result == LND_OK ? 0 : 1;
}
