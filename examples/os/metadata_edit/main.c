#include "lindar.h"
#include "lindar_metadata_id3v2.h"
#include <stdio.h>

int main(void) {
    LND_ConfigSet(LND_CFG_RUN_MODE, LND_MODE_SINGLE_THREADED);
    if (LND_LibraryInit() != LND_OK) return 1;
    LND_METADATA *tag = LND_MetadataCreate(nullptr), *copy = LND_MetadataCreate(nullptr);
    void *bytes = nullptr;
    size_t size = 0, consumed = 0;
    int result = 1;
    if (!tag || !copy || LND_MetadataSetValue(tag, "title", "Lindar demo") != LND_OK || LND_MetadataSetValue(tag, "artist", "Example") != LND_OK) goto done;
    LND_METADATA_CHAPTER chapter = {.id = "intro", .title = "Introduction", .start_us = 0, .end_us = 1000000,
                                   .start_offset_bytes = LND_METADATA_UNKNOWN, .end_offset_bytes = LND_METADATA_UNKNOWN};
    if (LND_MetadataSetChapter(tag, &chapter) != LND_OK) goto done;
    if (LND_MetadataId3v2CreateBuffer(tag, 4, 0, &bytes, &size) != LND_OK || LND_MetadataId3v2Read(copy, bytes, size, &consumed) != LND_OK) goto done;
    const char *title = LND_MetadataGetValue(copy, "title", 0);
    if (!title) goto done;
    printf("%s: %zu bytes, %u chapters\n", title, consumed, LND_MetadataGetChapterCount(copy));
    result = 0;
done:
    LND_MetadataBufferFree(bytes);
    LND_MetadataFree(copy);
    LND_MetadataFree(tag);
    LND_LibraryFree();
    return result;
}
