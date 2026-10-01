#include "internal.h"

static const char *nonull(const char *s) { return s ? s : ""; }

bool lnd_tag_equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
    }
    return !*a && !*b;
}

static bool text_valid(const char *s) { return !s || lnd_tag_utf8(s, strlen(s)); }

static int32_t reserve(void **array, uint32_t *capacity, uint32_t count, uint32_t limit, size_t width) {
    if (count >= limit) return LND_METADATA_ERR_LIMIT;
    if (count < *capacity) return LND_OK;
    uint32_t next = *capacity ? (*capacity > limit / 2 ? limit : *capacity * 2) : (limit < 8 ? limit : 8);
    if (next > SIZE_MAX / width) return LND_METADATA_ERR_LIMIT;
    void *p = lnd_realloc(*array, (size_t)next * width);
    if (!p) return LND_ERR_OUT_OF_MEMORY;
    *array = p;
    *capacity = next;
    return LND_OK;
}

static void *copy_strings(LND_METADATA *m, size_t extra, const char *a, const char *b, const char *c, const char *d, size_t credit, size_t *bytes) {
    const char *strings[] = {nonull(a), nonull(b), nonull(c), nonull(d)};
    size_t size = extra, lengths[4];
    for (unsigned i = 0; i < 4; ++i) {
        size_t n = lengths[i] = strlen(strings[i]) + 1;
        if (n > m->limits.bytes || size > m->limits.bytes - n) return nullptr;
        size += n;
    }
    *bytes = size;
    if (size > m->limits.bytes - (m->bytes - credit)) return nullptr;
    char *p = lnd_alloc(size);
    if (!p) return nullptr;
    size_t pos = extra;
    for (unsigned i = 0; i < 4; ++i) {
        size_t n = lengths[i];
        memcpy(p + pos, strings[i], n);
        pos += n;
    }
    return p;
}

static size_t field_bytes(const LND_METADATA_FIELD *f) { return strlen(f->key) + strlen(f->value) + strlen(f->language) + strlen(f->description) + 4; }
static size_t chapter_bytes(const LND_METADATA_CHAPTER *c) { return strlen(c->id) + strlen(c->title) + strlen(c->url) + 4; }
static size_t blob_bytes(const LND_METADATA_BLOB *b) { return b->size + strlen(b->key) + strlen(b->scope) + 4; }

LND_METADATA *LND_MetadataCreate(const LND_METADATA_LIMITS *limits) {
    LND_METADATA *m = lnd_alloc_zero(sizeof *m);
    if (!m) return nullptr;
    lnd_store_relaxed(&m->references, 1);
    m->limits = (LND_METADATA_LIMITS){32u * 1024u * 1024u, 4096, 1000, 4096};
    if (limits) {
        if (limits->bytes) m->limits.bytes = limits->bytes;
        if (limits->fields) m->limits.fields = limits->fields;
        if (limits->chapters) m->limits.chapters = limits->chapters;
        if (limits->blobs) m->limits.blobs = limits->blobs;
    }
    return m;
}

void LND_MetadataClear(LND_METADATA *m) {
    if (!m) return;
    for (uint32_t i = 0; i < m->field_count; ++i)
        lnd_free((void *)m->fields[i].key);
    for (uint32_t i = 0; i < m->chapter_count; ++i)
        lnd_free((void *)m->chapters[i].id);
    for (uint32_t i = 0; i < m->blob_count; ++i)
        lnd_free((void *)m->blobs[i].data);
    lnd_free(m->fields);
    lnd_free(m->chapters);
    lnd_free(m->blobs);
    lnd_free(m->vendor);
    LND_METADATA_LIMITS limits = m->limits;
    *m = (LND_METADATA){.references = 1, .limits = limits};
}

LND_METADATA *lnd_tag_ref(LND_METADATA *m) {
    lnd_add(&m->references, 1);
    return m;
}

void LND_MetadataFree(LND_METADATA *m) {
    if (m && lnd_sub(&m->references, 1) == 1) {
        LND_MetadataClear(m);
        lnd_free(m);
    }
}
void LND_MetadataBufferFree(void *buffer) { lnd_free(buffer); }
int32_t LND_MetadataGetFormat(const LND_METADATA *m) { return m ? m->format : LND_METADATA_AUTO; }
uint32_t LND_MetadataGetVersion(const LND_METADATA *m) { return m ? m->version : 0; }
const char *LND_MetadataGetVendor(const LND_METADATA *m) { return m && m->vendor ? m->vendor : "Lindar"; }

int32_t LND_MetadataSetVendor(LND_METADATA *m, const char *vendor) {
    if (!m || !vendor || !text_valid(vendor)) return LND_ERR_INVALID_ARG;
    size_t old = m->vendor ? strlen(m->vendor) + 1 : 0, size = strlen(vendor) + 1;
    if (size > m->limits.bytes - (m->bytes - old)) return LND_METADATA_ERR_LIMIT;
    char *p = lnd_strdup(vendor);
    if (!p) return LND_ERR_OUT_OF_MEMORY;
    lnd_free(m->vendor);
    m->vendor = p;
    m->bytes = m->bytes - old + size;
    return LND_OK;
}

uint32_t LND_MetadataGetFieldCount(const LND_METADATA *m) { return m ? m->field_count : 0; }
const LND_METADATA_FIELD *LND_MetadataGetField(const LND_METADATA *m, uint32_t i) { return m && i < m->field_count ? m->fields + i : nullptr; }
const char *LND_MetadataGetValue(const LND_METADATA *m, const char *key, uint32_t occurrence) {
    if (m && key)
        for (uint32_t i = 0; i < m->field_count; ++i)
            if (lnd_tag_equal(m->fields[i].key, key) && !occurrence--) return m->fields[i].value;
    return nullptr;
}

int32_t LND_MetadataAddField(LND_METADATA *m, const LND_METADATA_FIELD *f) {
    if (!m || !f || !f->key || !*f->key || !f->value || !text_valid(f->key) || !text_valid(f->value) || !text_valid(f->language) || !text_valid(f->description))
        return LND_ERR_INVALID_ARG;
    for (const unsigned char *p = (const unsigned char *)f->key; *p; ++p)
        if (*p < 32) return LND_ERR_INVALID_ARG;
    LND_METADATA_FIELD input = *f;
    f = &input;
    int32_t r = reserve((void **)&m->fields, &m->field_capacity, m->field_count, m->limits.fields, sizeof *m->fields);
    if (r) return r;
    size_t bytes = SIZE_MAX;
    char *p = copy_strings(m, 0, f->key, f->value, f->language, f->description, 0, &bytes);
    if (!p) return bytes > m->limits.bytes - m->bytes ? LND_METADATA_ERR_LIMIT : LND_ERR_OUT_OF_MEMORY;
    LND_METADATA_FIELD field = {.key = p};
    field.value = p + strlen(p) + 1;
    field.language = field.value + strlen(field.value) + 1;
    field.description = field.language + strlen(field.language) + 1;
    for (char *q = p; *q; ++q)
        if (*q >= 'a' && *q <= 'z') *q -= 32;
    m->fields[m->field_count++] = field;
    m->bytes += bytes;
    return LND_OK;
}

int32_t lnd_tag_add_text(LND_METADATA *m, const char *key, const char *value, const char *language, const char *description) {
    LND_METADATA_FIELD field = {key, value, language, description};
    return LND_MetadataAddField(m, &field);
}

int32_t LND_MetadataRemoveField(LND_METADATA *m, uint32_t i) {
    if (!m || i >= m->field_count) return LND_ERR_INVALID_ARG;
    m->bytes -= field_bytes(m->fields + i);
    lnd_free((void *)m->fields[i].key);
    --m->field_count;
    memmove(m->fields + i, m->fields + i + 1, (m->field_count - i) * sizeof *m->fields);
    return LND_OK;
}

int32_t LND_MetadataRemove(LND_METADATA *m, const char *key) {
    if (!m || !key) return LND_ERR_INVALID_ARG;
    uint32_t first = 0;
    while (first < m->field_count && !lnd_tag_equal(m->fields[first].key, key))
        ++first;
    if (first == m->field_count) return LND_OK;
    LND_METADATA_FIELD removed = m->fields[first];
    uint32_t count = first;
    for (uint32_t i = first + 1; i < m->field_count; i++) {
        if (lnd_tag_equal(m->fields[i].key, removed.key)) {
            m->bytes -= field_bytes(m->fields + i);
            lnd_free((void *)m->fields[i].key);
        } else m->fields[count++] = m->fields[i];
    }
    m->field_count = count;
    m->bytes -= field_bytes(&removed);
    lnd_free((void *)removed.key);
    return LND_OK;
}

int32_t LND_MetadataSetValue(LND_METADATA *m, const char *key, const char *value) {
    if (!m || !key || !value) return LND_ERR_INVALID_ARG;
    if (!*key || !text_valid(key) || !text_valid(value)) return LND_ERR_INVALID_ARG;
    for (const unsigned char *q = (const unsigned char *)key; *q; ++q)
        if (*q < 32) return LND_ERR_INVALID_ARG;
    size_t credit = 0;
    uint32_t matches = 0;
    for (uint32_t i = 0; i < m->field_count; ++i)
        if (lnd_tag_equal(m->fields[i].key, key)) {
            credit += field_bytes(m->fields + i);
            ++matches;
        }
    int32_t r = reserve((void **)&m->fields, &m->field_capacity, m->field_count - matches, m->limits.fields, sizeof *m->fields);
    if (r) return r;
    size_t bytes = SIZE_MAX;
    char *p = copy_strings(m, 0, key, value, nullptr, nullptr, credit, &bytes);
    if (!p) return bytes > m->limits.bytes - (m->bytes - credit) ? LND_METADATA_ERR_LIMIT : LND_ERR_OUT_OF_MEMORY;
    LND_METADATA_FIELD field = {.key = p};
    field.value = p + strlen(p) + 1;
    field.language = field.value + strlen(field.value) + 1;
    field.description = field.language + 1;
    for (char *q = p; *q; ++q)
        if (*q >= 'a' && *q <= 'z') *q -= 32;
    LND_MetadataRemove(m, p);
    m->fields[m->field_count++] = field;
    m->bytes += bytes;
    return LND_OK;
}

uint32_t LND_MetadataGetChapterCount(const LND_METADATA *m) { return m ? m->chapter_count : 0; }
const LND_METADATA_CHAPTER *LND_MetadataGetChapter(const LND_METADATA *m, uint32_t i) { return m && i < m->chapter_count ? m->chapters + i : nullptr; }

int32_t LND_MetadataSetChapter(LND_METADATA *m, const LND_METADATA_CHAPTER *c) {
    if (!m || !c || !c->id || !*c->id || !text_valid(c->id) || !text_valid(c->title) || !text_valid(c->url) || c->start_us == LND_METADATA_UNKNOWN ||
        (c->end_us != LND_METADATA_UNKNOWN && c->end_us < c->start_us) ||
        (c->start_offset_bytes != LND_METADATA_UNKNOWN && c->end_offset_bytes != LND_METADATA_UNKNOWN && c->end_offset_bytes < c->start_offset_bytes))
        return LND_ERR_INVALID_ARG;
    LND_METADATA_CHAPTER input = *c;
    c = &input;
    uint32_t i = 0;
    while (i < m->chapter_count && strcmp(m->chapters[i].id, c->id))
        ++i;
    size_t credit = i < m->chapter_count ? chapter_bytes(m->chapters + i) : 0;
    if (i == m->chapter_count) {
        int32_t r = reserve((void **)&m->chapters, &m->chapter_capacity, i, m->limits.chapters, sizeof *m->chapters);
        if (r) return r;
    }
    size_t bytes = SIZE_MAX;
    char *p = copy_strings(m, 0, c->id, c->title, c->url, nullptr, credit, &bytes);
    if (!p) return bytes > m->limits.bytes - (m->bytes - credit) ? LND_METADATA_ERR_LIMIT : LND_ERR_OUT_OF_MEMORY;
    LND_METADATA_CHAPTER chapter = *c;
    chapter.id = p;
    chapter.title = p + strlen(p) + 1;
    chapter.url = chapter.title + strlen(chapter.title) + 1;
    if (i < m->chapter_count) {
        lnd_free((void *)m->chapters[i].id);
        --m->chapter_count;
        memmove(m->chapters + i, m->chapters + i + 1, (m->chapter_count - i) * sizeof *m->chapters);
    }
    uint32_t lo = 0, hi = m->chapter_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (m->chapters[mid].start_us <= chapter.start_us)
            lo = mid + 1;
        else
            hi = mid;
    }
    memmove(m->chapters + lo + 1, m->chapters + lo, (m->chapter_count - lo) * sizeof *m->chapters);
    m->chapters[lo] = chapter;
    ++m->chapter_count;
    m->bytes = m->bytes - credit + bytes;
    return LND_OK;
}

int32_t LND_MetadataFindChapter(const LND_METADATA *m, uint64_t position_us) {
    if (!m) return LND_ERR_INVALID_ARG;
    uint32_t lo = 0, hi = m->chapter_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (m->chapters[mid].start_us <= position_us)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo ? (int32_t)(lo - 1) : LND_METADATA_ERR_NOT_FOUND;
}

int32_t LND_MetadataRemoveChapter(LND_METADATA *m, const char *id) {
    if (!m || !id) return LND_ERR_INVALID_ARG;
    uint32_t i = 0;
    while (i < m->chapter_count && strcmp(m->chapters[i].id, id))
        ++i;
    if (i == m->chapter_count) return LND_METADATA_ERR_NOT_FOUND;
    id = m->chapters[i].id;
    for (uint32_t j = m->blob_count; j-- > 0;)
        if (!strcmp(m->blobs[j].scope, id) || (m->blobs[j].format == LND_METADATA_ID3V2 && !strcmp(m->blobs[j].key, "CTOC"))) LND_MetadataRemoveBlob(m, j);
    m->bytes -= chapter_bytes(m->chapters + i);
    lnd_free((void *)m->chapters[i].id);
    --m->chapter_count;
    memmove(m->chapters + i, m->chapters + i + 1, (m->chapter_count - i) * sizeof *m->chapters);
    return LND_OK;
}

uint32_t LND_MetadataGetBlobCount(const LND_METADATA *m) { return m ? m->blob_count : 0; }
const LND_METADATA_BLOB *LND_MetadataGetBlob(const LND_METADATA *m, uint32_t i) { return m && i < m->blob_count ? m->blobs + i : nullptr; }

int32_t LND_MetadataAddBlob(LND_METADATA *m, const LND_METADATA_BLOB *b) {
    if (!m || !b || !b->key || !*b->key || !text_valid(b->key) || !text_valid(b->scope) || (!b->data && b->size) || b->format < LND_METADATA_ID3V1 ||
        b->format > LND_METADATA_FLAC)
        return LND_ERR_INVALID_ARG;
    LND_METADATA_BLOB input = *b;
    b = &input;
    int32_t r = reserve((void **)&m->blobs, &m->blob_capacity, m->blob_count, m->limits.blobs, sizeof *m->blobs);
    if (r) return r;
    size_t bytes = SIZE_MAX;
    char *p = copy_strings(m, b->size, b->key, b->scope, nullptr, nullptr, 0, &bytes);
    if (!p) return bytes > m->limits.bytes - m->bytes ? LND_METADATA_ERR_LIMIT : LND_ERR_OUT_OF_MEMORY;
    if (b->size) memcpy(p, b->data, b->size);
    LND_METADATA_BLOB blob = *b;
    blob.data = p;
    blob.key = p + b->size;
    blob.scope = blob.key + strlen(blob.key) + 1;
    m->blobs[m->blob_count++] = blob;
    m->bytes += bytes;
    return LND_OK;
}

int32_t LND_MetadataRemoveBlob(LND_METADATA *m, uint32_t i) {
    if (!m || i >= m->blob_count) return LND_ERR_INVALID_ARG;
    m->bytes -= blob_bytes(m->blobs + i);
    lnd_free((void *)m->blobs[i].data);
    --m->blob_count;
    memmove(m->blobs + i, m->blobs + i + 1, (m->blob_count - i) * sizeof *m->blobs);
    return LND_OK;
}

LND_METADATA *LND_MetadataClone(const LND_METADATA *m) {
    if (!m) return nullptr;
    LND_METADATA *copy = LND_MetadataCreate(&m->limits);
    if (!copy) return nullptr;
    int32_t r = m->vendor ? LND_MetadataSetVendor(copy, m->vendor) : LND_OK;
    for (uint32_t i = 0; !r && i < m->field_count; ++i)
        r = LND_MetadataAddField(copy, m->fields + i);
    for (uint32_t i = 0; !r && i < m->chapter_count; ++i)
        r = LND_MetadataSetChapter(copy, m->chapters + i);
    for (uint32_t i = 0; !r && i < m->blob_count; ++i)
        r = LND_MetadataAddBlob(copy, m->blobs + i);
    if (r) {
        LND_MetadataFree(copy);
        return nullptr;
    }
    copy->format = m->format;
    copy->version = m->version;
    return copy;
}

int32_t lnd_tag_assign(LND_METADATA *dst, const LND_METADATA *source) {
    LND_METADATA *tmp = LND_MetadataCreate(&dst->limits);
    if (!tmp) return LND_ERR_OUT_OF_MEMORY;
    int32_t r = source->vendor ? LND_MetadataSetVendor(tmp, source->vendor) : LND_OK;
    for (uint32_t i = 0; !r && i < source->field_count; ++i)
        r = LND_MetadataAddField(tmp, source->fields + i);
    for (uint32_t i = 0; !r && i < source->chapter_count; ++i)
        r = LND_MetadataSetChapter(tmp, source->chapters + i);
    for (uint32_t i = 0; !r && i < source->blob_count; ++i)
        r = LND_MetadataAddBlob(tmp, source->blobs + i);
    tmp->format = source->format;
    tmp->version = source->version;
    return lnd_tag_commit(dst, tmp, r);
}

int32_t lnd_tag_commit(LND_METADATA *dst, LND_METADATA *parsed, int32_t result) {
    if (!result) {
        LND_METADATA old = *dst;
        *dst = *parsed;
        *parsed = old;
    }
    LND_MetadataFree(parsed);
    return result;
}

static lnd_atomic_u64 revision_counter;
uint64_t lnd_tag_next_revision(void) { return lnd_add(&revision_counter, 1) + 1; }
