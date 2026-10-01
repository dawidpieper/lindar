#include "network/http/http_hls/playlist.h"
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t bytes) {
    if (bytes > 65536) return 0;
    lnd_hls_playlist playlist = {0};
    lnd_hls_parse(data, bytes, "https://example.invalid/media/list.m3u8", 64, &playlist);
    lnd_hls_playlist_free(&playlist);
    return 0;
}
