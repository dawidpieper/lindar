#include "playlist.h"

#include <ctype.h>
#include <string.h>

bool lnd_hls_number(const char *text, uint64_t *number) {
    uint64_t n = 0;
    if (!text || !isdigit((unsigned char)*text)) return false;
    for (; *text; text++) {
        if (*text < '0' || *text > '9' || n > (UINT64_MAX - (uint32_t)(*text - '0')) / 10) return false;
        n = n * 10 + (uint32_t)(*text - '0');
    }
    *number = n;
    return true;
}

bool lnd_hls_seconds(const char *text, int64_t *microseconds) {
    if (!text) return false;
    bool negative = *text == '-';
    if (negative) text++;
    if (!isdigit((unsigned char)*text)) return false;
    uint64_t seconds = 0, fraction = 0, scale = 100000;
    while (isdigit((unsigned char)*text)) {
        if (seconds > (uint64_t)INT64_MAX / 10000000) return false;
        seconds = seconds * 10 + (uint32_t)(*text++ - '0');
    }
    if (*text == '.') {
        text++;
        if (!isdigit((unsigned char)*text)) return false;
        while (isdigit((unsigned char)*text)) {
            if (scale) {
                fraction += (uint32_t)(*text - '0') * scale;
                scale /= 10;
            }
            text++;
        }
    }
    if (*text && *text != ',') return false;
    if (seconds > ((uint64_t)INT64_MAX - fraction) / 1000000) return false;
    int64_t value = (int64_t)(seconds * 1000000 + fraction);
    *microseconds = negative ? -value : value;
    return true;
}

bool lnd_hls_attribute(const char *list, const char *name, char *value, size_t capacity) {
    size_t length = strlen(name);
    while (*list) {
        while (*list == ',' || *list == ' ' || *list == '\t') list++;
        const char *key = list;
        while (*list && *list != '=' && *list != ',') list++;
        if (*list != '=') return false;
        size_t n = (size_t)(list++ - key);
        bool quoted = *list == '"';
        if (quoted) list++;
        const char *start = list;
        while (*list && (quoted ? *list != '"' : *list != ',')) list++;
        if (quoted && *list != '"') return false;
        size_t bytes = (size_t)(list - start);
        if (n == length && !memcmp(key, name, n)) {
            if (bytes >= capacity) return false;
            memcpy(value, start, bytes);
            value[bytes] = 0;
            return true;
        }
        if (quoted) list++;
        while (*list == ' ' || *list == '\t') list++;
        if (*list && *list != ',') return false;
    }
    return false;
}

static char *lnd_hls_resolve(const char *base, const char *relative) {
    char url[LND_HTTP_URL_MAX];
    return lnd_http_url_resolve(base, relative, url, sizeof url) == LND_OK ? lnd_http_copy(url) : nullptr;
}

static int32_t lnd_hls_attr(const char *attrs, const char *name, const char *base, bool required, char **out) {
    char value[LND_HTTP_URL_MAX], url[LND_HTTP_URL_MAX];
    *out = nullptr;
    if (!lnd_hls_attribute(attrs, name, value, sizeof value)) return required ? LND_ERR_FORMAT : LND_OK;
    if (base && lnd_http_url_resolve(base, value, url, sizeof url) != LND_OK) return LND_ERR_FORMAT;
    *out = lnd_http_copy(base ? url : value);
    return *out ? LND_OK : LND_ERR_OUT_OF_MEMORY;
}

static bool lnd_hls_range_parse(const char *text, lnd_hls_range *range) {
    char number[64];
    size_t n = strcspn(text, "@");
    if (n >= sizeof number) return false;
    memcpy(number, text, n);
    number[n] = 0;
    *range = (lnd_hls_range){.present = true, .explicit_offset = text[n] == '@'};
    if (!lnd_hls_number(number, &range->length) || !range->length) return false;
    return !range->explicit_offset || (lnd_hls_number(text + n + 1, &range->offset) && range->offset <= UINT64_MAX - range->length);
}

static void lnd_hls_segment_free(lnd_hls_segment *s) {
    lnd_free(s->url);
    lnd_free(s->map);
    lnd_free(s->key);
    lnd_free(s->map_key);
}

void lnd_hls_playlist_free(lnd_hls_playlist *p) {
    for (uint32_t i = 0; i < p->count; i++) lnd_hls_segment_free(&p->segments[i]);
    for (uint32_t i = 0; i < p->variant_count; i++) {
        lnd_free(p->variants[i].url);
        lnd_free(p->variants[i].codecs);
        lnd_free(p->variants[i].audio);
    }
    for (uint32_t i = 0; i < p->audio_count; i++) {
        lnd_free(p->audio[i].url);
        lnd_free(p->audio[i].group);
        lnd_free(p->audio[i].language);
        lnd_free(p->audio[i].name);
    }
    for (uint32_t i = 0; i < p->report_count; i++) lnd_free(p->reports[i].url);
    lnd_free(p->reports);
    lnd_free(p->segments);
    lnd_free(p->variants);
    lnd_free(p->audio);
    lnd_free(p->preload);
    *p = (lnd_hls_playlist){0};
}

static bool lnd_hls_iv(const char *text, uint8_t iv[16]) {
    if (text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) return false;
    text += 2;
    size_t n = strlen(text);
    if (!n || n > 32) return false;
    memset(iv, 0, 16);
    for (size_t i = 0; i < n; i++) {
        unsigned c = (unsigned char)text[i];
        int digit = c >= '0' && c <= '9' ? (int)c - '0' : c >= 'a' && c <= 'f' ? (int)c - 'a' + 10 : c >= 'A' && c <= 'F' ? (int)c - 'A' + 10 : -1;
        if (digit < 0) return false;
        size_t at = 32 - n + i;
        iv[at / 2] |= (uint8_t)(digit << (at % 2 ? 0 : 4));
    }
    return true;
}

static void *lnd_hls_reserve(void *data, uint32_t *capacity, uint32_t count, size_t width, uint32_t limit) {
    if (count < *capacity) return data;
    if (count >= limit || count >= SIZE_MAX / width) return nullptr;
    uint32_t next = *capacity ? (uint32_t)LND_MIN((uint64_t)limit, (uint64_t)*capacity * 2) : LND_MIN(limit, 16u);
    if (next <= count || next > SIZE_MAX / width) return nullptr;
    void *grown = lnd_realloc(data, (size_t)next * width);
    if (grown) *capacity = next;
    return grown;
}

static int32_t lnd_hls_append(lnd_hls_playlist *p, lnd_hls_segment *current, const char *url, const char *base, uint32_t limit) {
    if (p->count >= limit || current->duration_us <= 0) return LND_ERR_FORMAT;
    lnd_hls_segment s = *current;
    s.url = lnd_hls_resolve(base, url);
    s.map = lnd_http_copy(current->map);
    s.key = lnd_http_copy(current->key);
    s.map_key = lnd_http_copy(current->map_key);
    if (!s.url || (current->map && !s.map) || (current->key && !s.key) || (current->map_key && !s.map_key)) {
        lnd_hls_segment_free(&s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    if (s.range.present && !s.range.explicit_offset) {
        lnd_hls_segment *last = p->count ? &p->segments[p->count - 1] : nullptr;
        if (!last || !last->range.present || strcmp(last->url, s.url)) {
            lnd_hls_segment_free(&s);
            return LND_ERR_FORMAT;
        }
        s.range.offset = last->range.offset + last->range.length;
        if (s.range.offset > UINT64_MAX - s.range.length) {
            lnd_hls_segment_free(&s);
            return LND_ERR_FORMAT;
        }
    }
    if (s.key && !s.explicit_iv) {
        memset(s.iv, 0, 16);
        for (unsigned i = 0; i < 8; i++) s.iv[15 - i] = (uint8_t)(s.sequence >> (8 * i));
    }
    void *grown = lnd_hls_reserve(p->segments, &p->capacity, p->count, sizeof *p->segments, limit);
    if (!grown) {
        lnd_hls_segment_free(&s);
        return LND_ERR_OUT_OF_MEMORY;
    }
    p->segments = grown;

    p->segments[p->count++] = s;
    return LND_OK;
}

static bool lnd_hls_date(const char *s, int64_t *value) {
    if (strlen(s) < 20) return false;
    const unsigned positions[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
    for (size_t i = 0; i < sizeof positions / sizeof *positions; i++)
        if (s[positions[i]] < '0' || s[positions[i]] > '9') return false;
    if (s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':') return false;
    int year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + s[3] - '0';
    int month = (s[5] - '0') * 10 + s[6] - '0', day = (s[8] - '0') * 10 + s[9] - '0';
    int hour = (s[11] - '0') * 10 + s[12] - '0', minute = (s[14] - '0') * 10 + s[15] - '0';
    int second = (s[17] - '0') * 10 + s[18] - '0';
    const int months[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = !(year % 4) && (year % 100 || !(year % 400));
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > months[month - 1] + (month == 2 && leap) || hour > 23 || minute > 59 || second > 59)
        return false;
    int64_t days = (int64_t)(year - 1970) * 365 + (year - 1) / 4 - 1969 / 4 - ((year - 1) / 100 - 1969 / 100) + (year - 1) / 400 - 1969 / 400 + day - 1;
    for (int m = 1; m < month; m++) days += months[m - 1] + (m == 2 && leap);
    int64_t us = ((days * 24 + hour) * 60 + minute) * 60000000 + second * 1000000;
    const char *p = s + 19;
    if (*p == '.') {
        p++;
        if (*p < '0' || *p > '9') return false;
        int scale = 100000;
        while (*p >= '0' && *p <= '9') {
            us += (*p++ - '0') * scale;
            scale /= 10;
        }
    }
    if (*p == 'Z' && !p[1]) {
        *value = us;
        return true;
    }
    if ((*p != '+' && *p != '-') || strlen(p) != 6 || p[3] != ':' || p[1] < '0' || p[1] > '9' || p[2] < '0' || p[2] > '9' || p[4] < '0' || p[4] > '9' ||
        p[5] < '0' || p[5] > '9')
        return false;
    int zone_h = (p[1] - '0') * 10 + p[2] - '0', zone_m = (p[4] - '0') * 10 + p[5] - '0';
    if (zone_h > 23 || zone_m > 59) return false;
    *value = us + (*p == '+' ? -1 : 1) * ((int64_t)zone_h * 60 + zone_m) * 60000000;
    return true;
}

typedef struct lnd_hls_parser {
    lnd_hls_playlist playlist;
    lnd_hls_segment current;
    lnd_hls_variant variant;
    const char *base;
    uint32_t limit, part;
    int64_t part_time;
    bool pending_variant;
} lnd_hls_parser;

static int32_t lnd_hls_parse_key(lnd_hls_parser *s, const char *attrs) {
    char value[LND_HTTP_URL_MAX], *key = nullptr;
    uint8_t iv[16] = {0};
    bool explicit_iv = false;
    if (!lnd_hls_attribute(attrs, "METHOD", value, sizeof value)) return LND_ERR_FORMAT;
    if (!strcmp(value, "AES-128")) {
        if (lnd_hls_attribute(attrs, "KEYFORMAT", value, sizeof value) && strcmp(value, "identity")) return LND_ERR_UNSUPPORTED;
        explicit_iv = lnd_hls_attribute(attrs, "IV", value, sizeof value);
        if (explicit_iv && !lnd_hls_iv(value, iv)) return LND_ERR_FORMAT;
        int32_t result = lnd_hls_attr(attrs, "URI", s->base, true, &key);
        if (result != LND_OK) return result;
    } else if (strcmp(value, "NONE")) return LND_ERR_UNSUPPORTED;
    lnd_free(s->current.key);
    s->current.key = key;
    s->current.explicit_iv = explicit_iv;
    memcpy(s->current.iv, iv, sizeof iv);
    s->current.key_set = true;
    s->current.key_sequence = s->current.sequence;
    return LND_OK;
}

static int32_t lnd_hls_parse_map(lnd_hls_parser *s, const char *attrs) {
    char value[128], *map = nullptr;
    lnd_hls_range range = {0};
    if (s->current.key && !s->current.explicit_iv) return LND_ERR_FORMAT;
    if (lnd_hls_attribute(attrs, "BYTERANGE", value, sizeof value) && !lnd_hls_range_parse(value, &range)) return LND_ERR_FORMAT;
    int32_t result = lnd_hls_attr(attrs, "URI", s->base, true, &map);
    if (result != LND_OK) return result;
    char *key = lnd_http_copy(s->current.key);
    if (s->current.key && !key) {
        lnd_free(map);
        return LND_ERR_OUT_OF_MEMORY;
    }
    lnd_free(s->current.map);
    lnd_free(s->current.map_key);
    s->current.map = map;
    s->current.map_key = key;
    s->current.map_range = range;
    s->current.map_set = true;
    s->current.map_key_sequence = s->current.key_sequence;
    memcpy(s->current.map_iv, s->current.iv, sizeof s->current.map_iv);
    return LND_OK;
}

static int32_t lnd_hls_parse_variant(lnd_hls_parser *s, const char *attrs) {
    char value[128];
    if (s->pending_variant || !lnd_hls_attribute(attrs, "BANDWIDTH", value, sizeof value) || !lnd_hls_number(value, &s->variant.bitrate_bps))
        return LND_ERR_FORMAT;
    int32_t result = lnd_hls_attr(attrs, "CODECS", nullptr, false, &s->variant.codecs);
    if (result == LND_OK) result = lnd_hls_attr(attrs, "AUDIO", nullptr, false, &s->variant.audio);
    if (result == LND_OK) s->pending_variant = true;
    return result;
}

static int32_t lnd_hls_parse_audio(lnd_hls_parser *s, const char *attrs) {
    char value[128];
    if (!lnd_hls_attribute(attrs, "TYPE", value, sizeof value) || strcmp(value, "AUDIO")) return LND_OK;
    if (s->playlist.audio_count >= s->limit) return LND_ERR_FORMAT;
    lnd_hls_audio audio = {0};
    int32_t result = lnd_hls_attr(attrs, "URI", s->base, false, &audio.url);
    if (result == LND_OK) result = lnd_hls_attr(attrs, "GROUP-ID", nullptr, true, &audio.group);
    if (result == LND_OK) result = lnd_hls_attr(attrs, "LANGUAGE", nullptr, false, &audio.language);
    if (result == LND_OK) result = lnd_hls_attr(attrs, "NAME", nullptr, true, &audio.name);
    audio.default_track = lnd_hls_attribute(attrs, "DEFAULT", value, sizeof value) && !strcmp(value, "YES");
    audio.autoselect = lnd_hls_attribute(attrs, "AUTOSELECT", value, sizeof value) && !strcmp(value, "YES");
    void *grown = result ? nullptr : lnd_hls_reserve(s->playlist.audio, &s->playlist.audio_capacity, s->playlist.audio_count, sizeof audio, s->limit);
    if (!grown) {
        lnd_free(audio.url);
        lnd_free(audio.group);
        lnd_free(audio.language);
        lnd_free(audio.name);
        return result ? result : LND_ERR_OUT_OF_MEMORY;
    }
    s->playlist.audio = grown;
    s->playlist.audio[s->playlist.audio_count++] = audio;
    return LND_OK;
}

static int32_t lnd_hls_parse_server(lnd_hls_parser *s, const char *attrs) {
    char value[128];
    s->playlist.blocking = lnd_hls_attribute(attrs, "CAN-BLOCK-RELOAD", value, sizeof value) && !strcmp(value, "YES");
    if (lnd_hls_attribute(attrs, "HOLD-BACK", value, sizeof value) && !lnd_hls_seconds(value, &s->playlist.hold_back_us)) {
        return LND_ERR_FORMAT;
    }
    if (lnd_hls_attribute(attrs, "PART-HOLD-BACK", value, sizeof value) && !lnd_hls_seconds(value, &s->playlist.part_hold_back_us)) {
        return LND_ERR_FORMAT;
    }
    if (lnd_hls_attribute(attrs, "CAN-SKIP-UNTIL", value, sizeof value) && !lnd_hls_seconds(value, &s->playlist.skip_until_us)) {
        return LND_ERR_FORMAT;
    }

    return LND_OK;
}

static int32_t lnd_hls_parse_part(lnd_hls_parser *s, const char *attrs) {
    int32_t result = LND_OK;

    char value[LND_HTTP_URL_MAX], uri[LND_HTTP_URL_MAX];
    lnd_hls_segment piece = s->current;
    piece.range = (lnd_hls_range){0};
    if (!lnd_hls_attribute(attrs, "URI", uri, sizeof uri) || !lnd_hls_attribute(attrs, "DURATION", value, sizeof value) ||
        !lnd_hls_seconds(value, &piece.duration_us) || piece.duration_us <= 0) {
        return LND_ERR_FORMAT;
    }
    if (lnd_hls_attribute(attrs, "BYTERANGE", value, sizeof value) && !lnd_hls_range_parse(value, &piece.range)) {
        return LND_ERR_FORMAT;
    }
    piece.independent = lnd_hls_attribute(attrs, "INDEPENDENT", value, sizeof value) && !strcmp(value, "YES");
    piece.gap = lnd_hls_attribute(attrs, "GAP", value, sizeof value) && !strcmp(value, "YES");
    if (s->part > INT32_MAX) return LND_ERR_FORMAT;
    piece.part = (int32_t)s->part++;
    if (s->part_time > INT64_MAX - s->playlist.duration_us || piece.duration_us > INT64_MAX - s->part_time) {
        return LND_ERR_FORMAT;
    }
    if (piece.program_us != INT64_MIN) {
        if (piece.program_us > INT64_MAX - s->part_time) {
            return LND_ERR_FORMAT;
        }
        piece.program_us += s->part_time;
    }
    piece.start_us = s->playlist.duration_us + s->part_time;
    s->part_time += piece.duration_us;
    result = lnd_hls_append(&s->playlist, &piece, uri, s->base, s->limit);
    if (result != LND_OK) return result;

    return LND_OK;
}

static int32_t lnd_hls_parse_report(lnd_hls_parser *s, const char *attrs) {
    char value[128];
    if (s->playlist.report_count >= s->limit) return LND_ERR_FORMAT;
    lnd_hls_report report = {.part = -1};
    report.has_sequence = lnd_hls_attribute(attrs, "LAST-MSN", value, sizeof value);
    if (report.has_sequence && !lnd_hls_number(value, &report.sequence)) return LND_ERR_FORMAT;
    if (lnd_hls_attribute(attrs, "LAST-PART", value, sizeof value)) {
        uint64_t part;
        if (!lnd_hls_number(value, &part) || part > INT32_MAX) return LND_ERR_FORMAT;
        report.part = (int32_t)part;
    }
    int32_t result = lnd_hls_attr(attrs, "URI", s->base, true, &report.url);
    if (result != LND_OK) return result;
    void *grown = lnd_hls_reserve(s->playlist.reports, &s->playlist.report_capacity, s->playlist.report_count, sizeof report, s->limit);
    if (!grown) {
        lnd_free(report.url);
        return LND_ERR_OUT_OF_MEMORY;
    }
    s->playlist.reports = grown;
    s->playlist.reports[s->playlist.report_count++] = report;
    return LND_OK;
}

static int32_t lnd_hls_parse_preload(lnd_hls_parser *s, const char *attrs) {
    char value[128], *url = nullptr;
    if (!lnd_hls_attribute(attrs, "TYPE", value, sizeof value) || strcmp(value, "PART")) return LND_OK;
    lnd_hls_range range = {0};
    if (lnd_hls_attribute(attrs, "BYTERANGE-START", value, sizeof value)) {
        if (!lnd_hls_number(value, &range.offset)) return LND_ERR_FORMAT;
        range.present = range.explicit_offset = true;
    }
    if (lnd_hls_attribute(attrs, "BYTERANGE-LENGTH", value, sizeof value) &&
        (!lnd_hls_number(value, &range.length) || range.length > UINT64_MAX - range.offset))
        return LND_ERR_FORMAT;
    int32_t result = lnd_hls_attr(attrs, "URI", s->base, true, &url);
    if (result != LND_OK) return result;
    lnd_free(s->playlist.preload);
    s->playlist.preload = url;
    s->playlist.preload_range = range;
    return LND_OK;
}

static int32_t lnd_hls_parse_uri(lnd_hls_parser *s, const char *line) {
    int32_t result;

    if (s->pending_variant) {
        if (s->playlist.variant_count >= s->limit) {
            return LND_ERR_FORMAT;
        }
        s->variant.url = lnd_hls_resolve(s->base, line);
        void *grown = lnd_hls_reserve(s->playlist.variants, &s->playlist.variant_capacity, s->playlist.variant_count, sizeof *s->playlist.variants, s->limit);
        if (!s->variant.url || !grown) {
            if (grown) s->playlist.variants = grown;
            return LND_ERR_OUT_OF_MEMORY;
        }
        s->playlist.variants = grown;
        s->playlist.variants[s->playlist.variant_count++] = s->variant;
        s->variant = (lnd_hls_variant){0};
        s->pending_variant = false;
    } else {
        s->current.start_us = s->playlist.duration_us;
        s->current.part = -1;
        result = lnd_hls_append(&s->playlist, &s->current, line, s->base, s->limit);
        if (result != LND_OK) return result;
        if (s->current.duration_us > INT64_MAX - s->playlist.duration_us || s->current.sequence == UINT64_MAX) {
            return LND_ERR_FORMAT;
        }
        if (s->current.program_us != INT64_MIN) {
            if (s->current.program_us > INT64_MAX - s->current.duration_us) {
                return LND_ERR_FORMAT;
            }
            s->current.program_us += s->current.duration_us;
        }
        s->playlist.duration_us += s->current.duration_us;
        s->current.sequence++;
        s->current.duration_us = 0;
        s->current.gap = false;
        s->current.range = (lnd_hls_range){0};
        s->part = 0;
        s->part_time = 0;
    }

    return LND_OK;
}

static int32_t lnd_hls_parse_line(lnd_hls_parser *s, const char *line) {
    if (*line && *line != '#') {
        return lnd_hls_parse_uri(s, line);
    } else if (!strncmp(line, "#EXTINF:", 8)) {
        if (!lnd_hls_seconds(line + 8, &s->current.duration_us) || s->current.duration_us <= 0) {
            return LND_ERR_FORMAT;
        }
    } else if (!strcmp(line, "#EXT-X-ENDLIST")) s->playlist.end = true;
    else if (!strcmp(line, "#EXT-X-GAP")) s->current.gap = true;
    else if (!strcmp(line, "#EXT-X-DISCONTINUITY")) {
        if (s->current.discontinuity == UINT64_MAX) {
            return LND_ERR_FORMAT;
        }
        s->current.discontinuity++;
    } else if (!strncmp(line, "#EXT-X-PROGRAM-DATE-TIME:", 25)) {
        if (!lnd_hls_date(line + 25, &s->current.program_us)) {
            return LND_ERR_FORMAT;
        }
    } else if (!strncmp(line, "#EXT-X-MEDIA-SEQUENCE:", 22)) {
        if (!lnd_hls_number(line + 22, &s->playlist.sequence) || s->playlist.count) {
            return LND_ERR_FORMAT;
        }
        s->current.sequence = s->playlist.sequence;
    } else if (!strncmp(line, "#EXT-X-DISCONTINUITY-SEQUENCE:", 30)) {
        if (!lnd_hls_number(line + 30, &s->current.discontinuity) || s->playlist.count) {
            return LND_ERR_FORMAT;
        }
    } else if (!strncmp(line, "#EXT-X-TARGETDURATION:", 22)) {
        if (!lnd_hls_seconds(line + 22, &s->playlist.target_us) || s->playlist.target_us <= 0 || s->playlist.target_us > INT64_MAX / 4) {
            return LND_ERR_FORMAT;
        }
    } else if (!strncmp(line, "#EXT-X-PLAYLIST-TYPE:", 21)) {
        s->playlist.vod = !strcmp(line + 21, "VOD");
        s->playlist.event = !strcmp(line + 21, "EVENT");
        if (!s->playlist.vod && !s->playlist.event) {
            return LND_ERR_FORMAT;
        }
    } else if (!strncmp(line, "#EXT-X-BYTERANGE:", 17)) {
        if (!lnd_hls_range_parse(line + 17, &s->current.range)) {
            return LND_ERR_FORMAT;
        }
    } else if (!strncmp(line, "#EXT-X-KEY:", 11)) {
        return lnd_hls_parse_key(s, line + 11);
    } else if (!strncmp(line, "#EXT-X-MAP:", 11)) {
        return lnd_hls_parse_map(s, line + 11);
    } else if (!strncmp(line, "#EXT-X-STREAM-INF:", 18)) {
        return lnd_hls_parse_variant(s, line + 18);
    } else if (!strncmp(line, "#EXT-X-MEDIA:", 13)) {
        return lnd_hls_parse_audio(s, line + 13);
    } else if (!strncmp(line, "#EXT-X-START:", 13)) {
        char value[128];
        if (!lnd_hls_attribute(line + 13, "TIME-OFFSET", value, sizeof value) || !lnd_hls_seconds(value, &s->playlist.start_offset_us)) {
            return LND_ERR_FORMAT;
        }
        s->playlist.has_start = true;
    } else if (!strncmp(line, "#EXT-X-PART-INF:", 16)) {
        char value[128];
        if (!lnd_hls_attribute(line + 16, "PART-TARGET", value, sizeof value) || !lnd_hls_seconds(value, &s->playlist.part_target_us)) {
            return LND_ERR_FORMAT;
        }
    } else if (!strncmp(line, "#EXT-X-SERVER-CONTROL:", 22)) {
        return lnd_hls_parse_server(s, line + 22);
    } else if (!strncmp(line, "#EXT-X-SKIP:", 12)) {
        char value[128];
        if (!lnd_hls_attribute(line + 12, "SKIPPED-SEGMENTS", value, sizeof value) || !lnd_hls_number(value, &s->playlist.skipped) ||
            s->playlist.sequence > UINT64_MAX - s->playlist.skipped) {
            return LND_ERR_FORMAT;
        }
        if (s->playlist.count) {
            return LND_ERR_FORMAT;
        }
        s->current.sequence = s->playlist.sequence + s->playlist.skipped;
    } else if (!strncmp(line, "#EXT-X-PART:", 12)) {
        return lnd_hls_parse_part(s, line + 12);
    } else if (!strncmp(line, "#EXT-X-RENDITION-REPORT:", 24)) {
        return lnd_hls_parse_report(s, line + 24);
    } else if (!strncmp(line, "#EXT-X-PRELOAD-HINT:", 20)) {
        return lnd_hls_parse_preload(s, line + 20);
    }

    return LND_OK;
}

static int32_t lnd_hls_parse_finish(lnd_hls_parser *s) {
    if (s->playlist.part_target_us < 0 || s->playlist.hold_back_us < 0 || s->playlist.part_hold_back_us < 0 || s->playlist.skip_until_us < 0)
        return LND_ERR_FORMAT;
    if (s->part_time > INT64_MAX - s->playlist.duration_us) return LND_ERR_FORMAT;
    else s->playlist.duration_us += s->part_time;
    if ((s->pending_variant || s->current.duration_us || (!s->playlist.variant_count && !s->playlist.target_us))) return LND_ERR_FORMAT;
    if (s->playlist.count) {
        const lnd_hls_segment *last = &s->playlist.segments[s->playlist.count - 1];
        for (uint32_t i = 0; i < s->playlist.report_count; i++) {
            if (!s->playlist.reports[i].has_sequence) s->playlist.reports[i].sequence = last->sequence;
            if (s->playlist.reports[i].part < 0) s->playlist.reports[i].part = last->part;
        }
    }
    return LND_OK;
}

int32_t lnd_hls_parse(const uint8_t *data, size_t bytes, const char *base, uint32_t limit, lnd_hls_playlist *out) {
    if (!data || !bytes || !limit || !base || !out || memchr(data, 0, bytes) || bytes == SIZE_MAX) return LND_ERR_INVALID_ARG;
    char *text = lnd_alloc(bytes + 1);
    if (!text) return LND_ERR_OUT_OF_MEMORY;
    memcpy(text, data, bytes);
    text[bytes] = 0;
    lnd_hls_parser parser = {.base = base, .limit = limit, .current = {.part = -1, .program_us = INT64_MIN}};
    lnd_hls_parser *s = &parser;
    char *line = text;
    if (bytes >= 3 && !memcmp(line, "\xef\xbb\xbf", 3)) line += 3;
    bool first = true;
    int32_t result = LND_OK;
    for (; *line;) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        size_t n = strlen(line);
        if (n && line[n - 1] == '\r') line[--n] = 0;
        if (first) {
            if (strcmp(line, "#EXTM3U")) {
                result = LND_ERR_FORMAT;
                break;
            }
            first = false;
        } else {
            result = lnd_hls_parse_line(s, line);
            if (result != LND_OK) break;
        }
        if (!end) break;
        line = end + 1;
    }
    if (result == LND_OK) result = lnd_hls_parse_finish(s);
    lnd_free(s->variant.url);
    lnd_free(s->variant.codecs);
    lnd_free(s->variant.audio);
    lnd_free(s->current.key);
    lnd_free(s->current.map);
    lnd_free(s->current.map_key);
    lnd_free(text);
    if (result != LND_OK) {
        lnd_hls_playlist_free(&s->playlist);
        return result;
    }
    *out = s->playlist;
    return LND_OK;
}
