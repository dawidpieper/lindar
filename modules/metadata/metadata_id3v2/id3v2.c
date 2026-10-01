#include "lindar_metadata_id3v2.h"
#include "metadata/internal.h"
#include "metadata/id3/id3.h"
#include <stdio.h>

static const lnd_tag_map map[] = {{"TITLE", "TIT2", "TT2"},       {"ARTIST", "TPE1", "TP1"},     {"ALBUM", "TALB", "TAL"},     {"ALBUMARTIST", "TPE2", "TP2"},
                                  {"TRACKNUMBER", "TRCK", "TRK"}, {"DISCNUMBER", "TPOS", "TPA"}, {"DATE", "TDRC", nullptr},    {"DATE", "TYER", "TYE"},
                                  {"GENRE", "TCON", "TCO"},       {"COMPOSER", "TCOM", "TCM"},   {"COPYRIGHT", "TCOP", "TCR"}, {"ENCODER", "TSSE", "TSS"},
                                  {"LYRICIST", "TEXT", "TXT"},    {"CONDUCTOR", "TPE3", "TP3"},  {"PUBLISHER", "TPUB", "TPB"}, {"BPM", "TBPM", "TBP"},
                                  {"GROUPING", "TIT1", "TT1"},    {"SUBTITLE", "TIT3", "TT3"},   {"LANGUAGE", "TLAN", "TLA"},  {"ISRC", "TSRC", "TRC"},
                                  {"COMMENT", "COMM", "COM"},     {"LYRICS", "USLT", "ULT"},     {"URL", "WXXX", "WXX"}};
#define MAP_COUNT (sizeof map / sizeof *map)

static int32_t raw_frame(LND_METADATA *m, const char *id, const char *scope, unsigned version, uint16_t flags, const uint8_t *p, size_t n) {
    LND_METADATA_BLOB blob = {.format = LND_METADATA_ID3V2, .key = id, .scope = scope, .flags = ((uint32_t)version << 16) | flags, .data = p, .size = n};
    return LND_MetadataAddBlob(m, &blob);
}

static int32_t split_text(const uint8_t **p, size_t *n, unsigned enc, char **text, bool required) {
    size_t end = lnd_tag_terminator(*p, *n, enc), width = enc == 1 || enc == 2 ? 2 : 1;
    if (end == *n && required) return LND_ERR_FORMAT;
    int32_t r = lnd_tag_decode(*p, end, enc, text);
    if (r) return r;
    size_t used = end < *n ? end + width : end;
    *p += used;
    *n -= used;
    return LND_OK;
}

static int32_t parse_frames(LND_METADATA *m, const uint8_t *p, size_t size, unsigned version, bool all_unsync, LND_METADATA_CHAPTER *chapter);

static int32_t parse_chapter(LND_METADATA *m, const uint8_t *p, size_t n, unsigned version) {
    const uint8_t *end = memchr(p, 0, n);
    if (!end || end == p || (size_t)(end - p) + 17 > n) return LND_ERR_FORMAT;
    if ((size_t)(end - p) > 255) return LND_METADATA_ERR_LIMIT;
    char id[511];
    size_t id_size = 0;
    for (const uint8_t *q = p; q < end; ++q) {
        if (*q >= 128) id[id_size++] = (char)(0xc0 | (*q >> 6));
        id[id_size++] = (char)(*q < 128 ? *q : 0x80 | (*q & 63));
    }
    id[id_size] = 0;
    for (uint32_t i = 0; i < m->chapter_count; ++i)
        if (!strcmp(m->chapters[i].id, id)) return LND_ERR_FORMAT;
    n -= (size_t)(end + 1 - p);
    p = end + 1;
    uint32_t start = lnd_tag_get32(p, true), finish = lnd_tag_get32(p + 4, true);
    uint32_t first = lnd_tag_get32(p + 8, true), last = lnd_tag_get32(p + 12, true);
    LND_METADATA_CHAPTER c = {.id = id,
                              .start_us = (uint64_t)start * 1000,
                              .end_us = finish == UINT32_MAX ? LND_METADATA_UNKNOWN : (uint64_t)finish * 1000,
                              .start_offset_bytes = first == UINT32_MAX ? LND_METADATA_UNKNOWN : first,
                              .end_offset_bytes = last == UINT32_MAX ? LND_METADATA_UNKNOWN : last};
    int32_t r = parse_frames(m, p + 16, n - 16, version, false, &c);
    if (!r) r = LND_MetadataSetChapter(m, &c);
    lnd_free((void *)c.title);
    lnd_free((void *)c.url);
    return r == LND_ERR_INVALID_ARG ? LND_ERR_FORMAT : r;
}

static int32_t parse_content(LND_METADATA *m, const char *id, const uint8_t *p, size_t n, unsigned version, uint16_t status, LND_METADATA_CHAPTER *chapter) {
    bool user = !strcmp(id, "TXXX") || !strcmp(id, "TXX");
    bool comment = !strcmp(id, "COMM") || !strcmp(id, "COM");
    bool lyrics = !strcmp(id, "USLT") || !strcmp(id, "ULT");
    bool url = !strcmp(id, "WXXX") || !strcmp(id, "WXX");
    const char *key = lnd_tag_key(map, MAP_COUNT, id);
    if (chapter && strcmp(id, "TIT2") && strcmp(id, "WXXX")) {
        if (!strcmp(id, "CHAP") || !strcmp(id, "CTOC")) return LND_ERR_FORMAT;
        return raw_frame(m, id, chapter->id, version, status, p, n);
    }
    if (!strcmp(id, "CHAP")) return parse_chapter(m, p, n, version);
    if (!(id[0] == 'T' || comment || lyrics || url)) {
        if (id[0] == 'W') {
            char *text = nullptr;
            size_t length = lnd_tag_terminator(p, n, 0);
            int32_t r = lnd_tag_decode(p, length, 0, &text);
            if (!r) r = lnd_tag_add_text(m, key, text, nullptr, nullptr);
            lnd_free(text);
            return r;
        }
        return raw_frame(m, id, nullptr, version, status, p, n);
    }
    if (!n || *p > 3 || (version < 4 && *p > 1)) return LND_ERR_FORMAT;
    const uint8_t *original = p;
    size_t original_size = n;
    unsigned enc = *p++;
    --n;
    char lang[4] = {0}, *description = nullptr;
    int32_t r = LND_OK;
    if (comment || lyrics) {
        if (n < 3) return LND_ERR_FORMAT;
        memcpy(lang, p, 3);
        p += 3;
        n -= 3;
        for (unsigned i = 0; i < 3; ++i)
            if (lang[i] < 'a' || lang[i] > 'z') return LND_ERR_FORMAT;
    }
    if (user || comment || lyrics || url) r = split_text(&p, &n, enc, &description, true);
    if (!r && user) {
        bool named = *description != 0;
        for (const unsigned char *q = (const unsigned char *)description; named && *q; ++q)
            if (*q < 32) named = false;
        if (!named) {
            lnd_free(description);
            return raw_frame(m, id, chapter ? chapter->id : nullptr, version, status, original, original_size);
        }
        key = description;
    }
    bool first = true;
    while (!r && (first || n)) {
        first = false;
        char *text = nullptr;
        r = split_text(&p, &n, url ? 0 : enc, &text, false);
        if (!r) {
            if (chapter) {
                char **slot = (char **)(url ? &chapter->url : &chapter->title);
                if (*slot) r = LND_ERR_FORMAT;
                else {
                    *slot = text;
                    text = nullptr;
                }
            } else r = lnd_tag_add_text(m, key, text, lang, user ? nullptr : description);
        }
        lnd_free(text);
        if (version < 4 || comment || lyrics || url) {
            for (size_t i = 0; !r && i < n; ++i)
                if (p[i]) r = LND_ERR_FORMAT;
            break;
        }
    }
    lnd_free(description);
    return r;
}

static int32_t parse_frames(LND_METADATA *m, const uint8_t *p, size_t size, unsigned version, bool all_unsync, LND_METADATA_CHAPTER *chapter) {
    lnd_id3_iterator iterator = {.data = p, .size = size, .version = version, .unsynchronized = all_unsync};
    lnd_id3_frame value;
    int32_t result;
    while ((result = lnd_id3_next(&iterator, &value)) > 0) {
        result = value.opaque ? raw_frame(m, value.id, chapter ? chapter->id : nullptr, version, value.flags, value.data, value.size)
                              : parse_content(m, value.id, value.data, value.size, version, value.flags & 0xff00u, chapter);
        if (result) break;
    }
    lnd_id3_iterator_close(&iterator);
    return result;
}

static bool latin_id_equal(const uint8_t *native, const char *utf8) {
    const uint8_t *p = (const uint8_t *)utf8;
    while (*native) {
        uint8_t c = *native++;
        if (c < 128) {
            if (*p++ != c) return false;
        } else {
            if (*p++ != (0xc0 | (c >> 6))) return false;
            if (*p++ != (0x80 | (c & 63))) return false;
        }
    }
    return !*p;
}

typedef struct toc_entry {
    const uint8_t *id, *children;
    uint8_t count, mark;
} toc_entry;

static int32_t visit_toc(toc_entry *entries, uint32_t count, uint32_t index, unsigned depth, const LND_METADATA *m) {
    if (entries[index].mark == 1) return LND_ERR_CYCLE;
    if (entries[index].mark == 2) return LND_OK;
    if (depth > 32) return LND_METADATA_ERR_LIMIT;
    entries[index].mark = 1;
    const uint8_t *id = entries[index].children;
    for (unsigned i = 0; i < entries[index].count; ++i) {
        bool found = false;
        for (uint32_t j = 0; j < count; ++j)
            if (!strcmp((const char *)id, (const char *)entries[j].id)) {
                int32_t r = visit_toc(entries, count, j, depth + 1, m);
                if (r) return r;
                found = true;
                break;
            }
        for (uint32_t j = 0; !found && j < m->chapter_count; ++j) found = latin_id_equal(id, m->chapters[j].id);
        if (!found) return LND_ERR_FORMAT;
        id += strlen((const char *)id) + 1;
    }
    entries[index].mark = 2;
    return LND_OK;
}

static int32_t validate_toc(const LND_METADATA *m) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < m->blob_count; ++i)
        if (m->blobs[i].format == LND_METADATA_ID3V2 && !strcmp(m->blobs[i].key, "CTOC") && !*m->blobs[i].scope) ++count;
    if (!count) return LND_OK;
#if SIZE_MAX < UINT64_MAX
    if (count > SIZE_MAX / sizeof(toc_entry)) return LND_METADATA_ERR_LIMIT;
#endif
    toc_entry *entries = lnd_alloc_zero((size_t)count * sizeof *entries);
    if (!entries) return LND_ERR_OUT_OF_MEMORY;
    uint32_t next = 0;
    int32_t r = LND_OK;
    for (uint32_t i = 0; !r && i < m->blob_count; ++i) {
        const LND_METADATA_BLOB *b = m->blobs + i;
        if (b->format != LND_METADATA_ID3V2 || strcmp(b->key, "CTOC") || *b->scope) continue;
        const uint8_t *p = b->data, *end = p + b->size;
        const uint8_t *zero = memchr(p, 0, b->size);
        if (!zero || zero == p || end - zero < 3 || (zero[1] & ~3u)) {
            r = LND_ERR_FORMAT;
            break;
        }
        for (uint32_t j = 0; j < next; ++j)
            if (!strcmp((const char *)entries[j].id, (const char *)p)) r = LND_ERR_FORMAT;
        for (uint32_t j = 0; j < m->chapter_count; ++j)
            if (latin_id_equal(p, m->chapters[j].id)) r = LND_ERR_FORMAT;
        toc_entry *entry = entries + next++;
        entry->id = p;
        entry->count = zero[2];
        entry->children = zero + 3;
        p = entry->children;
        for (unsigned j = 0; !r && j < entry->count; ++j) {
            zero = memchr(p, 0, (size_t)(end - p));
            if (!zero || zero == p) {
                r = LND_ERR_FORMAT;
                break;
            }
            p = zero + 1;
        }
        if (!r && p < end) {
            LND_METADATA *nested = LND_MetadataCreate(&m->limits);
            if (!nested) r = LND_ERR_OUT_OF_MEMORY;
            else {
                LND_METADATA_CHAPTER scope = {.id = "toc"};
                r = parse_frames(nested, p, (size_t)(end - p), b->flags >> 16, false, &scope);
                lnd_free((void *)scope.title);
                lnd_free((void *)scope.url);
                LND_MetadataFree(nested);
            }
        }
    }
    for (uint32_t i = 0; !r && i < count; ++i) r = visit_toc(entries, count, i, 0, m);
    lnd_free(entries);
    return r;
}

int32_t LND_MetadataId3v2Read(LND_METADATA *m, const void *data, size_t size, size_t *consumed) {
    if (consumed) *consumed = 0;
    if (!m || !data) return LND_ERR_INVALID_ARG;
    lnd_id3_tag tag;
    int32_t r = lnd_id3_header(data, size, &tag);
    if (r) return r;
    if (tag.total > m->limits.bytes) return LND_METADATA_ERR_LIMIT;
    r = lnd_id3_body(&tag);
    LND_METADATA *tmp = r ? nullptr : LND_MetadataCreate(&m->limits);
    if (!tmp) {
        lnd_id3_close(&tag);
        return r ? r : LND_ERR_OUT_OF_MEMORY;
    }
    tmp->format = LND_METADATA_ID3V2;
    tmp->version = tag.version;
    r = parse_frames(tmp, tag.body, tag.size, tag.version, tag.version == 4 && (tag.flags & 0x80), nullptr);
    lnd_id3_close(&tag);
    if (!r) r = validate_toc(tmp);
    r = lnd_tag_commit(m, tmp, r);
    if (!r && consumed) *consumed = tag.total;
    return r;
}

static void frame(lnd_tag_buffer *out, const char *id, unsigned version, uint16_t flags, lnd_tag_buffer *body) {
    if (body->error) out->error = body->error;
    if (body->size > 0x0fffffff) out->error = LND_METADATA_ERR_LIMIT;
    if (!out->error) {
        uint8_t h[10] = {0};
        memcpy(h, id, 4);
        if (version == 4) lnd_tag_put_syncsafe(h + 4, (uint32_t)body->size);
        else lnd_tag_put32(h + 4, (uint32_t)body->size, true);
        h[8] = (uint8_t)(flags >> 8);
        h[9] = (uint8_t)flags;
        lnd_tag_append(out, h, sizeof h);
        lnd_tag_append(out, body->data, body->size);
    }
    lnd_free(body->data);
}

static void text_frame(lnd_tag_buffer *out, const char *id, const char *value, unsigned version) {
    lnd_tag_buffer b = {.limit = out->limit};
    unsigned enc = version == 4 ? 3 : 1;
    lnd_tag_byte(&b, (uint8_t)enc);
    lnd_tag_text(&b, value, enc, false);
    frame(out, id, version, 0, &b);
}

static void write_blobs(lnd_tag_buffer *out, const LND_METADATA *m, const char *scope, unsigned version, bool drop) {
    for (uint32_t i = 0; !out->error && i < m->blob_count; ++i) {
        const LND_METADATA_BLOB *b = m->blobs + i;
        if (strcmp(b->scope, scope)) continue;
        if (b->format != LND_METADATA_ID3V2 || (b->flags >> 16) != version || strlen(b->key) != 4 || !lnd_id3_valid_id(b->key, 4)) {
            if (!drop) out->error = LND_ERR_UNSUPPORTED;
            continue;
        }
        lnd_tag_buffer body = {.limit = out->limit};
        lnd_tag_append(&body, b->data, b->size);
        frame(out, b->key, version, (uint16_t)b->flags, &body);
    }
}

int32_t LND_MetadataId3v2CreateBuffer(const LND_METADATA *m, uint32_t version, uint32_t flags, void **data, size_t *size) {
    if (!m || !data || !size || (version && version != 3 && version != 4) || (flags & ~LND_METADATA_DROP_UNSUPPORTED)) return LND_ERR_INVALID_ARG;
    *data = nullptr;
    *size = 0;
    if (!version) version = m->version == 3 && m->format == LND_METADATA_ID3V2 ? 3 : 4;
    bool drop = (flags & LND_METADATA_DROP_UNSUPPORTED) != 0;
    int32_t validation = validate_toc(m);
    if (validation) return validation;
    lnd_tag_buffer out = {.limit = m->limits.bytes};
    uint8_t header[10] = {'I', 'D', '3', (uint8_t)version};
    lnd_tag_append(&out, header, sizeof header);
    for (uint32_t i = 0; !out.error && i < m->field_count; ++i) {
        const LND_METADATA_FIELD *f = m->fields + i;
        const char *id = lnd_tag_id(map, MAP_COUNT, f->key);
        if (!strcmp(f->key, "DATE") && version == 3) id = "TYER";
        if (!id && strlen(f->key) == 4 && lnd_id3_valid_id(f->key, 4) && (f->key[0] == 'T' || f->key[0] == 'W')) id = f->key;
        bool user = !id || !strcmp(id, "TXXX");
        if (user) id = "TXXX";
        bool comm = !strcmp(id, "COMM") || !strcmp(id, "USLT"), url = !strcmp(id, "WXXX");
        bool prior = false;
        for (uint32_t j = 0; j < i; ++j) {
            const LND_METADATA_FIELD *f2 = m->fields + j;
            if (!strcmp(f->key, f2->key) && !strcmp(f->language, f2->language) && !strcmp(f->description, f2->description)) {
                prior = true;
                break;
            }
        }
        if (prior) continue;
        unsigned enc = version == 4 ? 3 : 1;
        lnd_tag_buffer b = {.limit = out.limit};
        if (id[0] != 'W' || url) lnd_tag_byte(&b, (uint8_t)enc);
        if (comm) {
            const char *lang = *f->language ? f->language : "und";
            if (strlen(lang) != 3) b.error = LND_ERR_UNSUPPORTED;
            else {
                for (unsigned k = 0; k < 3; ++k)
                    if (lang[k] < 'a' || lang[k] > 'z') b.error = LND_ERR_UNSUPPORTED;
                lnd_tag_append(&b, lang, 3);
            }
        } else if (*f->language && !drop) b.error = LND_ERR_UNSUPPORTED;
        if (user || comm || url) lnd_tag_text(&b, user ? f->key : f->description, enc, true);
        else if (*f->description && !drop) b.error = LND_ERR_UNSUPPORTED;
        lnd_tag_text(&b, f->value, id[0] == 'W' ? 0 : enc, false);
        for (uint32_t j = i + 1; j < m->field_count; ++j) {
            const LND_METADATA_FIELD *f2 = m->fields + j;
            if (strcmp(f->key, f2->key) || strcmp(f->language, f2->language) || strcmp(f->description, f2->description)) continue;
            if (version != 4 || id[0] != 'T') {
                if (!drop) b.error = LND_ERR_UNSUPPORTED;
                continue;
            }
            lnd_tag_byte(&b, 0);
            lnd_tag_text(&b, f2->value, 3, false);
        }
        frame(&out, id, version, 0, &b);
    }
    bool toc = false;
    for (uint32_t i = 0; i < m->blob_count; ++i)
        if (m->blobs[i].format == LND_METADATA_ID3V2 && !strcmp(m->blobs[i].key, "CTOC")) toc = true;
    for (uint32_t i = 0; !out.error && i < m->chapter_count; ++i) {
        const LND_METADATA_CHAPTER *c = m->chapters + i;
        if (strlen(c->id) > 510 || c->start_us / 1000 >= UINT32_MAX || (c->end_us != LND_METADATA_UNKNOWN && c->end_us / 1000 >= UINT32_MAX) ||
            (c->start_offset_bytes != LND_METADATA_UNKNOWN && c->start_offset_bytes >= UINT32_MAX) ||
            (c->end_offset_bytes != LND_METADATA_UNKNOWN && c->end_offset_bytes >= UINT32_MAX)) {
            out.error = LND_ERR_UNSUPPORTED;
            break;
        }
        lnd_tag_buffer b = {.limit = out.limit};
        lnd_tag_text(&b, c->id, 0, true);
        if (b.size > 256) b.error = LND_METADATA_ERR_LIMIT;
        lnd_tag_u32(&b, (uint32_t)(c->start_us / 1000), true);
        lnd_tag_u32(&b, c->end_us == LND_METADATA_UNKNOWN ? UINT32_MAX : (uint32_t)(c->end_us / 1000), true);
        lnd_tag_u32(&b, (uint32_t)c->start_offset_bytes, true);
        lnd_tag_u32(&b, (uint32_t)c->end_offset_bytes, true);
        if (*c->title) text_frame(&b, "TIT2", c->title, version);
        if (*c->url) {
            lnd_tag_buffer url = {.limit = out.limit};
            lnd_tag_byte(&url, version == 4 ? 3 : 1);
            lnd_tag_text(&url, "", version == 4 ? 3 : 1, true);
            lnd_tag_text(&url, c->url, 0, false);
            frame(&b, "WXXX", version, 0, &url);
        }
        write_blobs(&b, m, c->id, version, drop);
        frame(&out, "CHAP", version, 0, &b);
    }
    if (!toc && m->chapter_count && m->chapter_count <= 255) {
        lnd_tag_buffer b = {.limit = out.limit};
        const char *id = "lindar-toc";
        for (uint32_t i = 0; i < m->chapter_count; ++i)
            if (!strcmp(m->chapters[i].id, id)) {
                id = nullptr;
                break;
            }
        if (id) {
            lnd_tag_append(&b, id, strlen(id) + 1);
            lnd_tag_byte(&b, 3);
            lnd_tag_byte(&b, (uint8_t)m->chapter_count);
            for (uint32_t i = 0; i < m->chapter_count; ++i) lnd_tag_text(&b, m->chapters[i].id, 0, true);
            frame(&out, "CTOC", version, 0, &b);
        }
    }
    write_blobs(&out, m, "", version, drop);
    if (!out.error) {
        if (out.size - 10 > 0x0fffffff) out.error = LND_METADATA_ERR_LIMIT;
        else lnd_tag_put_syncsafe(out.data + 6, (uint32_t)(out.size - 10));
    }
    return lnd_tag_finish(&out, data, size);
}
