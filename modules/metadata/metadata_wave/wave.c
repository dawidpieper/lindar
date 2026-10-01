#include "lindar_metadata_wave.h"
#include "metadata/internal.h"
#include "formats/riff/read.h"
#include "lnd_modules.h"
#if LND_MODULE_METADATA_ID3V2
#include "lindar_metadata_id3v2.h"
#endif
#include <stdio.h>

static const lnd_tag_map map[] = {{"TITLE", "INAM", nullptr},       {"ARTIST", "IART", nullptr},   {"ALBUM", "IPRD", nullptr},    {"COMMENT", "ICMT", nullptr},
                                  {"COPYRIGHT", "ICOP", nullptr},   {"DATE", "ICRD", nullptr},     {"GENRE", "IGNR", nullptr},    {"ENCODER", "ISFT", nullptr},
                                  {"TRACKNUMBER", "ITRK", nullptr}, {"COMPOSER", "IMUS", nullptr}, {"ENGINEER", "IENG", nullptr}, {"SUBJECT", "ISBJ", nullptr},
                                  {"KEYWORDS", "IKEY", nullptr},    {"SOURCE", "ISRC", nullptr}};
#define MAP_COUNT (sizeof map / sizeof *map)

static void chunk(lnd_tag_buffer *b, const char *id, const void *data, size_t size) {
    if (size > UINT32_MAX) {
        b->error = LND_METADATA_ERR_LIMIT;
        return;
    }
    lnd_tag_append(b, id, 4);
    lnd_tag_u32(b, (uint32_t)size, false);
    lnd_tag_append(b, data, size);
    if (size & 1) lnd_tag_byte(b, 0);
}

static int32_t wave_text(const uint8_t *p, size_t n, unsigned encoding, char **text) {
    size_t size = lnd_tag_terminator(p, n, 0);
    return lnd_tag_decode(p, size, encoding, text);
}

static int32_t read_info(LND_METADATA *m, const uint8_t *p, size_t n, unsigned encoding) {
    lnd_riff riff = {.end = n};
    uint64_t pos = 4;
    while (pos < n) {
        uint64_t at = pos;
        lnd_riff_chunk chunk;
        if (lnd_riff_next(&riff, p + at, &pos, &chunk) != LND_OK) return LND_ERR_FORMAT;
        size_t size = (size_t)chunk.size;
        char id[5] = {0};
        memcpy(id, p + at, 4);
        if (strlen(id) != 4) return LND_ERR_FORMAT;
        char *text = nullptr;
        int32_t r = wave_text(p + at + 8, size, encoding, &text);
        if (!r) r = lnd_tag_add_text(m, lnd_tag_key(map, MAP_COUNT, id), text, nullptr, nullptr);
        lnd_free(text);
        if (r) return r;
    }
    return LND_OK;
}

static int32_t read_cue(LND_METADATA *m, const uint8_t *p, size_t n, uint32_t sample_rate_hz) {
    if (!sample_rate_hz || n < 4) return LND_ERR_FORMAT;
    uint32_t count = lnd_tag_get32(p, false);
    if (count > (n - 4) / 24 || (size_t)count * 24 != n - 4) return LND_ERR_FORMAT;
    if (count > m->limits.chapters) return LND_METADATA_ERR_LIMIT;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *cue = p + 4 + (size_t)i * 24;
        if (memcmp(cue + 8, "data", 4) || lnd_tag_get32(cue + 12, false) || lnd_tag_get32(cue + 16, false)) return LND_ERR_UNSUPPORTED;
        char id[16];
        snprintf(id, sizeof id, "%u", lnd_tag_get32(cue, false));
        for (uint32_t j = 0; j < m->chapter_count; ++j)
            if (!strcmp(m->chapters[j].id, id)) return LND_ERR_FORMAT;
        LND_METADATA_CHAPTER c = {.id = id,
                                  .start_us = (uint64_t)lnd_tag_get32(cue + 20, false) * 1000000 / sample_rate_hz,
                                  .end_us = LND_METADATA_UNKNOWN,
                                  .start_offset_bytes = LND_METADATA_UNKNOWN,
                                  .end_offset_bytes = LND_METADATA_UNKNOWN};
        int32_t r = LND_MetadataSetChapter(m, &c);
        if (r) return r;
    }
    return LND_OK;
}

static int32_t read_adtl(LND_METADATA *m, const uint8_t *p, size_t n, uint32_t sample_rate_hz, unsigned encoding) {
    lnd_riff riff = {.end = n};
    for (uint64_t pos = 4; pos < n;) {
        uint64_t at = pos;
        lnd_riff_chunk chunk;
        if (lnd_riff_next(&riff, p + at, &pos, &chunk) != LND_OK) return LND_ERR_FORMAT;
        size_t size = (size_t)chunk.size;
        if (size < 4) return LND_ERR_FORMAT;
        const uint8_t *body = p + at + 8;
        char id[16];
        snprintf(id, sizeof id, "%u", lnd_tag_get32(body, false));
        uint32_t index = 0;
        while (index < m->chapter_count && strcmp(m->chapters[index].id, id)) ++index;
        if (index == m->chapter_count) return LND_ERR_FORMAT;
        LND_METADATA_CHAPTER c = m->chapters[index];
        int32_t r = LND_OK;
        if (!memcmp(p + at, "labl", 4)) {
            char *text = nullptr;
            r = wave_text(body + 4, size - 4, encoding, &text);
            if (!r) {
                c.title = text;
                r = LND_MetadataSetChapter(m, &c);
            }
            lnd_free(text);
        } else {
            char key[5] = {0};
            memcpy(key, p + at, 4);
            if (!memcmp(key, "ltxt", 4)) {
                if (size < 20 || !sample_rate_hz) return LND_ERR_FORMAT;
                c.end_us = c.start_us + (uint64_t)lnd_tag_get32(body + 4, false) * 1000000 / sample_rate_hz;
                r = LND_MetadataSetChapter(m, &c);
            }
            if (!r) {
                const void *payload = body + 4;
                size_t payload_size = size - 4;
                char *text = nullptr;
                uint8_t *region = nullptr;
                if (!strcmp(key, "note")) {
                    r = wave_text(body + 4, size - 4, encoding, &text);
                    if (!r) {
                        payload = text;
                        payload_size = strlen(text) + 1;
                    }
                } else if (!strcmp(key, "ltxt") && !lnd_tag_get16(body + 18, false)) {
                    region = lnd_alloc(payload_size);
                    if (!region) r = LND_ERR_OUT_OF_MEMORY;
                    else {
                        memcpy(region, payload, payload_size);
                        uint16_t cp = encoding == 3 ? 65001 : encoding == 4 ? 1252 : 28591;
                        region[14] = (uint8_t)cp;
                        region[15] = (uint8_t)(cp >> 8);
                        payload = region;
                    }
                }
                LND_METADATA_BLOB blob = {.format = LND_METADATA_WAVE, .key = key, .scope = id, .data = payload, .size = payload_size};
                if (!r) r = LND_MetadataAddBlob(m, &blob);
                lnd_free(text);
                lnd_free(region);
            }
        }
        if (r) return r;
    }
    return LND_OK;
}

static bool opaque_chunk(const uint8_t *id) { return !memcmp(id, "bext", 4) || !memcmp(id, "iXML", 4) || !memcmp(id, "axml", 4) || !memcmp(id, "cart", 4); }

int32_t LND_MetadataWaveRead(LND_METADATA *m, const void *wave, size_t size) {
    if (!m || !wave) return LND_ERR_INVALID_ARG;
    const uint8_t *p = wave;
    if (size < 12) return LND_ERR_FORMAT;
    lnd_riff riff;
    int32_t r = lnd_riff_open(p, size, 0, &riff);
    if (r != LND_OK) return r;
    if (riff.rf64) {
        uint64_t pos = 12;
        lnd_riff_chunk chunk;
        if (size < 48 || lnd_riff_next(&riff, p + 12, &pos, &chunk) != LND_OK) return LND_ERR_FORMAT;
        r = lnd_riff_ds64(&riff, p + 20, size - 20, chunk.size);
        if (r != LND_OK) return r;
    }
    LND_METADATA *tmp = LND_MetadataCreate(&m->limits);
    if (!tmp) return LND_ERR_OUT_OF_MEMORY;
    tmp->format = LND_METADATA_WAVE;
    tmp->version = riff.rf64 ? 64 : 1;
    uint32_t sample_rate_hz = 0;
    unsigned encoding = 4;
    size_t metadata_bytes = 0;
    for (unsigned pass = 0; !r && pass < 3; ++pass) {
        for (uint64_t pos = 12; !r && pos < riff.end;) {
            lnd_riff_chunk chunk;
            const uint8_t *id = p + pos;
            r = lnd_riff_next(&riff, id, &pos, &chunk);
            if (r != LND_OK) break;
            const uint8_t *body = p + chunk.body;
            size_t n = (size_t)chunk.size;
            bool list = !memcmp(id, "LIST", 4) && n >= 4;
            bool info = list && !memcmp(body, "INFO", 4), adtl = list && !memcmp(body, "adtl", 4);
            bool cue = !memcmp(id, "cue ", 4), opaque = opaque_chunk(id), embedded = !memcmp(id, "id3 ", 4) || !memcmp(id, "ID3 ", 4);
            if (!pass) {
                if (info || adtl || cue || opaque || embedded) {
                    if (n > m->limits.bytes - metadata_bytes) {
                        r = LND_METADATA_ERR_LIMIT;
                        break;
                    }
                    metadata_bytes += n;
                }
                if (!memcmp(id, "fmt ", 4)) {
                    if (n < 16) r = LND_ERR_FORMAT;
                    else sample_rate_hz = lnd_tag_get32(body + 4, false);
                } else if (!memcmp(id, "CSET", 4)) {
                    if (n < 8) r = LND_ERR_FORMAT;
                    else {
                        uint16_t cp = lnd_tag_get16(body, false);
                        if (cp == 65001) encoding = 3;
                        else if (cp == 28591) encoding = 0;
                        else if (cp == 0 || cp == 1252) encoding = 4;
                        else r = LND_ERR_UNSUPPORTED;
                    }
                }
            } else if (pass == 1) {
                if (info) r = read_info(tmp, body, n, encoding);
                else if (cue) r = read_cue(tmp, body, n, sample_rate_hz);
                else if (opaque || embedded) {
                    char key[5] = {0};
                    memcpy(key, id, 4);
                    LND_METADATA_BLOB blob = {.format = LND_METADATA_WAVE, .key = key, .data = body, .size = n};
                    r = LND_MetadataAddBlob(tmp, &blob);
                }
            } else if (adtl) r = read_adtl(tmp, body, n, sample_rate_hz, encoding);
        }
    }
#if LND_MODULE_METADATA_ID3V2
    for (uint64_t pos = 12; !r && pos < riff.end;) {
        const uint8_t *id = p + pos;
        lnd_riff_chunk chunk;
        r = lnd_riff_next(&riff, id, &pos, &chunk);
        if (r != LND_OK) break;
        size_t n = (size_t)chunk.size;
        if (!memcmp(id, "id3 ", 4) || !memcmp(id, "ID3 ", 4)) {
            LND_METADATA *tags = LND_MetadataCreate(&m->limits);
            if (!tags) {
                r = LND_ERR_OUT_OF_MEMORY;
                break;
            }
            r = LND_MetadataId3v2Read(tags, id + 8, n, nullptr);
            if (!r) {
                for (uint32_t i = 0; !r && i < tags->field_count; ++i) r = LND_MetadataRemove(tmp, tags->fields[i].key);
                for (uint32_t i = 0; !r && i < tags->field_count; ++i) r = LND_MetadataAddField(tmp, tags->fields + i);
                if (tags->chapter_count) {
                    if (tmp->chapter_count && tmp->chapter_count != tags->chapter_count) r = LND_ERR_UNSUPPORTED;
                    for (uint32_t i = tmp->blob_count; !r && i-- > 0;) {
                        LND_METADATA_BLOB old = tmp->blobs[i];
                        if (!*old.scope) continue;
                        uint32_t index = 0;
                        while (index < tmp->chapter_count && strcmp(tmp->chapters[index].id, old.scope)) ++index;
                        if (index == tmp->chapter_count) {
                            r = LND_ERR_FORMAT;
                            break;
                        }
                        old.scope = tags->chapters[index].id;
                        r = LND_MetadataAddBlob(tmp, &old);
                        if (!r) LND_MetadataRemoveBlob(tmp, i);
                    }
                    if (!r) {
                        for (uint32_t i = 0; i < tmp->chapter_count; ++i) {
                            tmp->bytes -= strlen(tmp->chapters[i].id) + strlen(tmp->chapters[i].title) + strlen(tmp->chapters[i].url) + 4;
                            lnd_free((void *)tmp->chapters[i].id);
                        }
                        tmp->chapter_count = 0;
                        for (uint32_t i = 0; !r && i < tags->chapter_count; ++i) r = LND_MetadataSetChapter(tmp, tags->chapters + i);
                    }
                }
            }
            LND_MetadataFree(tags);
        }
    }
#endif
    return lnd_tag_commit(m, tmp, r);
}

static bool embedded_id3(const LND_METADATA_BLOB *b) {
    return b->format == LND_METADATA_WAVE && !*b->scope && (!strcmp(b->key, "id3 ") || !strcmp(b->key, "ID3 "));
}

#if LND_MODULE_METADATA_ID3V2
static int32_t build_embedded(const LND_METADATA *m, uint32_t flags, void **data, size_t *size) {
    LND_METADATA *tags = LND_MetadataCreate(&m->limits);
    if (!tags) return LND_ERR_OUT_OF_MEMORY;
    unsigned version = 4;
    int32_t r = LND_OK;
    for (uint32_t i = 0; !r && i < m->blob_count; ++i) {
        const LND_METADATA_BLOB *b = m->blobs + i;
        if (embedded_id3(b)) {
            LND_METADATA *original = LND_MetadataCreate(&m->limits);
            if (!original) {
                r = LND_ERR_OUT_OF_MEMORY;
                break;
            }
            r = LND_MetadataId3v2Read(original, b->data, b->size, nullptr);
            version = original->version == 3 ? 3 : 4;
            for (uint32_t j = 0; !r && j < original->blob_count; ++j) {
                const LND_METADATA_BLOB *raw = original->blobs + j;
                if (!strcmp(raw->key, "CTOC")) continue;
                bool exists = !*raw->scope;
                for (uint32_t k = 0; !exists && k < m->chapter_count; ++k) exists = !strcmp(raw->scope, m->chapters[k].id);
                if (exists) r = LND_MetadataAddBlob(tags, raw);
                else if (!(flags & LND_METADATA_DROP_UNSUPPORTED)) r = LND_ERR_UNSUPPORTED;
            }
            LND_MetadataFree(original);
        } else if (b->format == LND_METADATA_ID3V2) {
            version = b->flags >> 16;
            r = LND_MetadataAddBlob(tags, b);
        }
    }
    for (uint32_t i = 0; !r && i < m->field_count; ++i) r = LND_MetadataAddField(tags, m->fields + i);
    for (uint32_t i = 0; !r && i < m->chapter_count; ++i) r = LND_MetadataSetChapter(tags, m->chapters + i);
    if (!r) r = LND_MetadataId3v2CreateBuffer(tags, version, flags, data, size);
    LND_MetadataFree(tags);
    return r;
}
#endif

static bool sample_position(uint64_t us, uint32_t sample_rate_hz, uint32_t *samples) {
    if (us / 1000000 > UINT32_MAX / sample_rate_hz) return false;
    uint64_t n = (us / 1000000) * sample_rate_hz + ((us % 1000000) * sample_rate_hz + 500000) / 1000000;
    if (n > UINT32_MAX) return false;
    *samples = (uint32_t)n;
    return true;
}

int32_t LND_MetadataWaveCreateBuffer(const LND_METADATA *m, uint32_t sample_rate_hz, uint32_t flags, void **chunks, size_t *size) {
    if (!m || !chunks || !size || !sample_rate_hz || (flags & ~LND_METADATA_DROP_UNSUPPORTED)) return LND_ERR_INVALID_ARG;
    *chunks = nullptr;
    *size = 0;
    bool drop = (flags & LND_METADATA_DROP_UNSUPPORTED) != 0;
    bool embed = false;
#if LND_MODULE_METADATA_ID3V2
    for (uint32_t i = 0; i < m->blob_count; ++i)
        if (embedded_id3(m->blobs + i) || m->blobs[i].format == LND_METADATA_ID3V2) embed = true;
    for (uint32_t i = 0; i < m->field_count; ++i) {
        const LND_METADATA_FIELD *f = m->fields + i;
        if ((!lnd_tag_id(map, MAP_COUNT, f->key) && !(strlen(f->key) == 4 && f->key[0] == 'I')) || *f->language || *f->description) embed = true;
    }
    for (uint32_t i = 0; i < m->chapter_count; ++i)
        if (*m->chapters[i].url || m->chapters[i].start_offset_bytes != LND_METADATA_UNKNOWN || m->chapters[i].end_offset_bytes != LND_METADATA_UNKNOWN) embed = true;
#endif
    lnd_tag_buffer out = {.limit = m->limits.bytes}, info = {.limit = m->limits.bytes};
    lnd_tag_append(&info, "INFO", 4);
    for (uint32_t i = 0; !info.error && i < m->field_count; ++i) {
        const LND_METADATA_FIELD *f = m->fields + i;
        const char *id = lnd_tag_id(map, MAP_COUNT, f->key);
        if (!id && strlen(f->key) == 4 && f->key[0] == 'I') id = f->key;
        if (!id || *f->language || *f->description) {
            if (!drop && !embed) info.error = LND_ERR_UNSUPPORTED;
            if (!id) continue;
        }
        chunk(&info, id, f->value, strlen(f->value) + 1);
    }
    if (info.error) out.error = info.error;
    if (info.size > 4) {
        const uint8_t cset[8] = {0xe9, 0xfd};
        chunk(&out, "CSET", cset, sizeof cset);
        chunk(&out, "LIST", info.data, info.size);
    }
    lnd_free(info.data);
    lnd_tag_buffer cues = {.limit = out.limit}, adtl = {.limit = out.limit};
    lnd_tag_u32(&cues, m->chapter_count, false);
    lnd_tag_append(&adtl, "adtl", 4);
    bool numeric_ids = true;
    for (uint32_t i = 0; numeric_ids && i < m->chapter_count; ++i) {
        uint64_t id;
        numeric_ids = !lnd_tag_uint(m->chapters[i].id, &id) && id <= UINT32_MAX;
        for (uint32_t j = 0; numeric_ids && j < i; ++j) {
            uint64_t prior;
            numeric_ids = !lnd_tag_uint(m->chapters[j].id, &prior) && prior != id;
        }
    }
    for (uint32_t i = 0; !out.error && i < m->chapter_count; ++i) {
        const LND_METADATA_CHAPTER *c = m->chapters + i;
        uint32_t sample, end;
        if (!sample_position(c->start_us, sample_rate_hz, &sample) ||
            (c->end_us != LND_METADATA_UNKNOWN && !sample_position(c->end_us, sample_rate_hz, &end))) {
            out.error = LND_ERR_UNSUPPORTED;
            break;
        }
        if (!drop && !embed && (*c->url || c->start_offset_bytes != LND_METADATA_UNKNOWN || c->end_offset_bytes != LND_METADATA_UNKNOWN)) {
            out.error = LND_ERR_UNSUPPORTED;
            break;
        }
        uint64_t parsed_id = i + 1;
        if (numeric_ids) lnd_tag_uint(c->id, &parsed_id);
        uint32_t id = (uint32_t)parsed_id;
        uint8_t cue[24] = {0};
        lnd_tag_put32(cue, id, false);
        lnd_tag_put32(cue + 4, sample, false);
        memcpy(cue + 8, "data", 4);
        lnd_tag_put32(cue + 20, sample, false);
        lnd_tag_append(&cues, cue, sizeof cue);
        lnd_tag_buffer label = {.limit = out.limit};
        lnd_tag_u32(&label, id, false);
        lnd_tag_append(&label, c->title, strlen(c->title) + 1);
        if (label.error) out.error = label.error;
        chunk(&adtl, "labl", label.data, label.size);
        lnd_free(label.data);
        if (c->end_us != LND_METADATA_UNKNOWN) {
            const LND_METADATA_BLOB *original = nullptr;
            for (uint32_t j = 0; j < m->blob_count; ++j) {
                const LND_METADATA_BLOB *b = m->blobs + j;
                if (b->format == LND_METADATA_WAVE && !strcmp(b->scope, c->id) && !strcmp(b->key, "ltxt") && b->size >= 16) {
                    original = b;
                    break;
                }
            }
            lnd_tag_buffer region = {.limit = out.limit};
            uint8_t length[20] = {0};
            lnd_tag_put32(length, id, false);
            lnd_tag_put32(length + 4, end - sample, false);
            memcpy(length + 8, "rgn ", 4);
            lnd_tag_append(&region, length, original ? 8 : sizeof length);
            if (original) lnd_tag_append(&region, (const uint8_t *)original->data + 4, original->size - 4);
            if (region.error) out.error = region.error;
            chunk(&adtl, "ltxt", region.data, region.size);
            lnd_free(region.data);
        }
        for (uint32_t j = 0; !out.error && j < m->blob_count; ++j) {
            const LND_METADATA_BLOB *b = m->blobs + j;
            if (strcmp(b->scope, c->id)) continue;
            if (b->format != LND_METADATA_WAVE || strlen(b->key) != 4) {
                if (!drop) out.error = LND_ERR_UNSUPPORTED;
                continue;
            }
            if (!strcmp(b->key, "ltxt")) continue;
            lnd_tag_buffer body = {.limit = out.limit};
            lnd_tag_u32(&body, id, false);
            lnd_tag_append(&body, b->data, b->size);
            if (body.error) out.error = body.error;
            chunk(&adtl, b->key, body.data, body.size);
            lnd_free(body.data);
        }
    }
    if (cues.error) out.error = cues.error;
    if (adtl.error) out.error = adtl.error;
    if (m->chapter_count) {
        chunk(&out, "cue ", cues.data, cues.size);
        chunk(&out, "LIST", adtl.data, adtl.size);
        if (info.size <= 4) {
            const uint8_t cset[8] = {0xe9, 0xfd};
            chunk(&out, "CSET", cset, sizeof cset);
        }
    }
    lnd_free(cues.data);
    lnd_free(adtl.data);
    for (uint32_t i = 0; !out.error && i < m->blob_count; ++i) {
        const LND_METADATA_BLOB *b = m->blobs + i;
        if (*b->scope) continue;
        if (embed && (embedded_id3(b) || b->format == LND_METADATA_ID3V2)) continue;
        if (b->format != LND_METADATA_WAVE || strlen(b->key) != 4 || (!opaque_chunk((const uint8_t *)b->key) && !embedded_id3(b))) {
            if (!drop) out.error = LND_ERR_UNSUPPORTED;
            continue;
        }
        chunk(&out, b->key, b->data, b->size);
    }
#if LND_MODULE_METADATA_ID3V2
    if (!out.error && embed) {
        void *tag = nullptr;
        size_t bytes = 0;
        out.error = build_embedded(m, flags, &tag, &bytes);
        if (!out.error) chunk(&out, "id3 ", tag, bytes);
        lnd_free(tag);
    }
#endif
    return lnd_tag_finish(&out, chunks, size);
}
