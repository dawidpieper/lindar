#include "lindar_metadata_id3v1.h"
#include "metadata/internal.h"
#include <stdio.h>

static const char *const genres[] = {"Blues",
                                     "Classic Rock",
                                     "Country",
                                     "Dance",
                                     "Disco",
                                     "Funk",
                                     "Grunge",
                                     "Hip-Hop",
                                     "Jazz",
                                     "Metal",
                                     "New Age",
                                     "Oldies",
                                     "Other",
                                     "Pop",
                                     "R&B",
                                     "Rap",
                                     "Reggae",
                                     "Rock",
                                     "Techno",
                                     "Industrial",
                                     "Alternative",
                                     "Ska",
                                     "Death Metal",
                                     "Pranks",
                                     "Soundtrack",
                                     "Euro-Techno",
                                     "Ambient",
                                     "Trip-Hop",
                                     "Vocal",
                                     "Jazz+Funk",
                                     "Fusion",
                                     "Trance",
                                     "Classical",
                                     "Instrumental",
                                     "Acid",
                                     "House",
                                     "Game",
                                     "Sound Clip",
                                     "Gospel",
                                     "Noise",
                                     "Alternative Rock",
                                     "Bass",
                                     "Soul",
                                     "Punk",
                                     "Space",
                                     "Meditative",
                                     "Instrumental Pop",
                                     "Instrumental Rock",
                                     "Ethnic",
                                     "Gothic",
                                     "Darkwave",
                                     "Techno-Industrial",
                                     "Electronic",
                                     "Pop-Folk",
                                     "Eurodance",
                                     "Dream",
                                     "Southern Rock",
                                     "Comedy",
                                     "Cult",
                                     "Gangsta",
                                     "Top 40",
                                     "Christian Rap",
                                     "Pop/Funk",
                                     "Jungle",
                                     "Native US",
                                     "Cabaret",
                                     "New Wave",
                                     "Psychedelic",
                                     "Rave",
                                     "Showtunes",
                                     "Trailer",
                                     "Lo-Fi",
                                     "Tribal",
                                     "Acid Punk",
                                     "Acid Jazz",
                                     "Polka",
                                     "Retro",
                                     "Musical",
                                     "Rock & Roll",
                                     "Hard Rock",
                                     "Folk",
                                     "Folk-Rock",
                                     "National Folk",
                                     "Swing",
                                     "Fast Fusion",
                                     "Bebob",
                                     "Latin",
                                     "Revival",
                                     "Celtic",
                                     "Bluegrass",
                                     "Avantgarde",
                                     "Gothic Rock",
                                     "Progressive Rock",
                                     "Psychedelic Rock",
                                     "Symphonic Rock",
                                     "Slow Rock",
                                     "Big Band",
                                     "Chorus",
                                     "Easy Listening",
                                     "Acoustic",
                                     "Humour",
                                     "Speech",
                                     "Chanson",
                                     "Opera",
                                     "Chamber Music",
                                     "Sonata",
                                     "Symphony",
                                     "Booty Bass",
                                     "Primus",
                                     "Porn Groove",
                                     "Satire",
                                     "Slow Jam",
                                     "Club",
                                     "Tango",
                                     "Samba",
                                     "Folklore",
                                     "Ballad",
                                     "Power Ballad",
                                     "Rhythmic Soul",
                                     "Freestyle",
                                     "Duet",
                                     "Punk Rock",
                                     "Drum Solo",
                                     "A Cappella",
                                     "Euro-House",
                                     "Dance Hall",
                                     "Goa",
                                     "Drum & Bass",
                                     "Club-House",
                                     "Hardcore",
                                     "Terror",
                                     "Indie",
                                     "BritPop",
                                     "Negerpunk",
                                     "Polsk Punk",
                                     "Beat",
                                     "Christian Gangsta",
                                     "Heavy Metal",
                                     "Black Metal",
                                     "Crossover",
                                     "Contemporary Christian",
                                     "Christian Rock",
                                     "Merengue",
                                     "Salsa",
                                     "Thrash Metal",
                                     "Anime",
                                     "JPop",
                                     "SynthPop"};
static const char *const keys[] = {"TITLE", "ARTIST", "ALBUM", "DATE", "COMMENT", "TRACKNUMBER", "GENRE"};
static const unsigned offsets[] = {3, 33, 63, 93, 97};
static const unsigned lengths[] = {30, 30, 30, 4, 30};

int32_t LND_MetadataId3v1Read(LND_METADATA *m, const void *data, size_t size) {
    if (!m || !data) return LND_ERR_INVALID_ARG;
    if (size != 128 || memcmp(data, "TAG", 3)) return LND_ERR_FORMAT;
    const uint8_t *p = data;
    LND_METADATA *tmp = LND_MetadataCreate(&m->limits);
    if (!tmp) return LND_ERR_OUT_OF_MEMORY;
    tmp->format = LND_METADATA_ID3V1;
    bool track = !p[125] && p[126];
    tmp->version = track ? 11 : 10;
    int32_t r = LND_OK;
    for (unsigned i = 0; !r && i < 5; ++i) {
        size_t n = i == 4 && track ? 28 : lengths[i];
        const uint8_t *start = p + offsets[i];
        size_t end = 0;
        while (end < n && start[end]) ++end;
        while (end && start[end - 1] == ' ') --end;
        if (!end) continue;
        char *text = nullptr;
        r = lnd_tag_decode(start, end, 0, &text);
        if (!r) r = lnd_tag_add_text(tmp, keys[i], text, nullptr, nullptr);
        lnd_free(text);
    }
    char number[4];
    if (!r && track) {
        snprintf(number, sizeof number, "%u", p[126]);
        r = lnd_tag_add_text(tmp, "TRACKNUMBER", number, nullptr, nullptr);
    }
    if (!r && p[127] != 255) {
        snprintf(number, sizeof number, "%u", p[127]);
        r = lnd_tag_add_text(tmp, "GENRE", p[127] < sizeof genres / sizeof *genres ? genres[p[127]] : number, nullptr, nullptr);
    }
    return lnd_tag_commit(m, tmp, r);
}

int32_t LND_MetadataId3v1CreateBuffer(const LND_METADATA *m, uint32_t flags, void **data, size_t *size) {
    if (!m || !data || !size || (flags & ~LND_METADATA_DROP_UNSUPPORTED)) return LND_ERR_INVALID_ARG;
    *data = nullptr;
    *size = 0;
    bool drop = (flags & LND_METADATA_DROP_UNSUPPORTED) != 0;
    if (!drop && (m->chapter_count || m->blob_count)) return LND_ERR_UNSUPPORTED;
    for (uint32_t i = 0; !drop && i < m->field_count; ++i) {
        const LND_METADATA_FIELD *f = m->fields + i;
        unsigned k = 0;
        while (k < sizeof keys / sizeof *keys && strcmp(f->key, keys[k])) ++k;
        if (k == sizeof keys / sizeof *keys || *f->description || *f->language || LND_MetadataGetValue(m, f->key, 1)) return LND_ERR_UNSUPPORTED;
    }
    uint8_t tag[128] = {'T', 'A', 'G'};
    tag[127] = 255;
    const char *track = LND_MetadataGetValue(m, "TRACKNUMBER", 0);
    uint64_t number = 0;
    if (track && (lnd_tag_uint(track, &number) || !number || number > 255)) {
        if (!drop) return LND_ERR_UNSUPPORTED;
        number = 0;
    }
    tag[126] = (uint8_t)number;
    for (unsigned i = 0; i < 5; ++i) {
        const char *value = LND_MetadataGetValue(m, keys[i], 0);
        if (!value) continue;
        lnd_tag_buffer text = {.limit = m->limits.bytes};
        lnd_tag_text(&text, value, 0, false);
        size_t limit = i == 4 && number ? 28 : lengths[i];
        if (text.error || text.size > limit || (text.size && text.data[text.size - 1] == ' ')) {
            if (!drop || (text.error && text.error != LND_ERR_UNSUPPORTED)) {
                int32_t r = text.error ? text.error : LND_ERR_UNSUPPORTED;
                lnd_free(text.data);
                return r;
            }
        }
        size_t n = text.size < limit ? text.size : limit;
        if (n) memcpy(tag + offsets[i], text.data, n);
        lnd_free(text.data);
    }
    const char *genre = LND_MetadataGetValue(m, "GENRE", 0);
    if (genre) {
        unsigned i = 0;
        while (i < sizeof genres / sizeof *genres && !lnd_tag_equal(genre, genres[i])) ++i;
        if (i < sizeof genres / sizeof *genres)
            tag[127] = (uint8_t)i;
        else if (!lnd_tag_uint(genre, &number) && number < 255)
            tag[127] = (uint8_t)number;
        else if (!drop)
            return LND_ERR_UNSUPPORTED;
    }
    lnd_tag_buffer out = {.limit = m->limits.bytes};
    lnd_tag_append(&out, tag, sizeof tag);
    return lnd_tag_finish(&out, data, size);
}
