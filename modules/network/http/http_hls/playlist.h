#pragma once

#include "network/http/http.h"
#include "network/http/http_packets/packets.h"

typedef struct lnd_hls_range {
    uint64_t offset;
    uint64_t length;
    bool present;
    bool explicit_offset;
} lnd_hls_range;

typedef struct lnd_hls_segment {
    char *url;
    char *map;
    char *key;
    char *map_key;
    lnd_hls_range range;
    lnd_hls_range map_range;
    uint64_t sequence;
    uint64_t discontinuity;
    uint64_t key_sequence;
    uint64_t map_key_sequence;
    int64_t start_us;
    int64_t program_us;
    int64_t duration_us;
    int32_t part;
    uint8_t iv[16];
    uint8_t map_iv[16];
    bool key_set;
    bool map_set;
    bool explicit_iv;
    bool gap;
    bool independent;
} lnd_hls_segment;

typedef struct lnd_hls_variant {
    char *url;
    char *codecs;
    char *audio;
    uint64_t bitrate_bps;
} lnd_hls_variant;

typedef struct lnd_hls_audio {
    char *url;
    char *group;
    char *language;
    char *name;
    bool default_track;
    bool autoselect;
} lnd_hls_audio;

typedef struct lnd_hls_report {
    char *url;
    uint64_t sequence;
    int32_t part;
    bool has_sequence;
} lnd_hls_report;

typedef struct lnd_hls_playlist {
    lnd_hls_segment *segments;
    uint32_t count;
    uint32_t capacity;
    lnd_hls_variant *variants;
    uint32_t variant_count;
    uint32_t variant_capacity;
    lnd_hls_audio *audio;
    uint32_t audio_count;
    uint32_t audio_capacity;
    lnd_hls_report *reports;
    uint32_t report_count;
    uint32_t report_capacity;
    uint64_t sequence;
    uint64_t skipped;
    int64_t duration_us;
    int64_t target_us;
    int64_t part_target_us;
    int64_t hold_back_us;
    int64_t part_hold_back_us;
    int64_t start_offset_us;
    int64_t skip_until_us;
    char *preload;
    lnd_hls_range preload_range;
    bool end;
    bool event;
    bool vod;
    bool blocking;
    bool has_start;
} lnd_hls_playlist;

int32_t lnd_hls_parse(const uint8_t *data, size_t bytes, const char *base, uint32_t max_entries, lnd_hls_playlist *out);
void lnd_hls_playlist_free(lnd_hls_playlist *playlist);
bool lnd_hls_attribute(const char *list, const char *name, char *value, size_t capacity);
bool lnd_hls_number(const char *text, uint64_t *number);
bool lnd_hls_seconds(const char *text, int64_t *microseconds);
