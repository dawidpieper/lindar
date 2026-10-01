#include "registry.h"
#include "src/config.h"
#include "src/context.h"
#include "src/error.h"

#include <string.h>

#define LND_MAX_CODECS 32

extern const LND_CODEC *const lnd_builtin_codecs[];
extern const uint32_t lnd_builtin_codecs_count;

static const LND_CODEC *lnd_codecs[LND_MAX_CODECS];
static uint32_t lnd_codecs_count;
static bool lnd_codecs_ready;

static void lnd_codecs_init(void) {
    if (lnd_codecs_ready) return;
    lnd_codecs_ready = true;
    for (uint32_t i = 0; i < lnd_builtin_codecs_count && lnd_codecs_count < LND_MAX_CODECS; i++) lnd_codecs[lnd_codecs_count++] = lnd_builtin_codecs[i];
}

static bool lnd_codec_has_extension(const LND_CODEC *c, const char *ext) {
    if (!c->extensions || !ext || !*ext) return false;
    const char *p = c->extensions;
    size_t n = strlen(ext);
    while (*p) {
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, ext, n) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

int32_t lnd_codec_rank(lnd_io *io, const LND_CODEC *const *input, uint32_t input_count, bool forced, const char *extension, const LND_CODEC **candidates) {
    int32_t scores[LND_MAX_CODECS];
    uint32_t count = 0;
    for (uint32_t i = 0; i < input_count; i++) {
        const LND_CODEC *c = input[i];
        if (LND_IoSeekBytes(io, 0) != LND_OK) return LND_ERR_IO;
        lnd_callback_enter();
        int32_t score = forced ? INT32_MAX : c->probe ? c->probe(io) : 0;
        lnd_callback_leave();
        if (score <= 0) continue;
        if (score < INT32_MAX && lnd_codec_has_extension(c, extension)) score++;
        uint32_t j = count++;
        while (j && scores[j - 1] < score) {
            candidates[j] = candidates[j - 1];
            scores[j] = scores[j - 1];
            j--;
        }
        candidates[j] = c;
        scores[j] = score;
        if (forced) break;
    }
    return count;
}

int32_t lnd_codec_open_candidates(lnd_io *io, const LND_CODEC *const *input, uint32_t count, bool forced, const char *extension, const LND_CODEC **codec,
                                  LND_CODEC_INFO *info, void **state) {
    const LND_CODEC *candidates[LND_MAX_CODECS];
    int32_t ranked = lnd_codec_rank(io, input, count, forced, extension, candidates);
    *codec = nullptr;
    *state = nullptr;
    *info = (LND_CODEC_INFO){0};
    if (ranked < 0) return ranked;
    count = (uint32_t)ranked;
    int32_t result = LND_ERR_FORMAT;
    for (uint32_t i = 0; i < count; i++) {
        if (LND_IoSeekBytes(io, 0) != LND_OK) return LND_ERR_IO;
        LND_CODEC_INFO candidate_info = {0};
        void *candidate_state = nullptr;
        lnd_callback_enter();
        int32_t r = candidates[i]->open(io, &candidate_info, &candidate_state);
        lnd_callback_leave();
        if (r == LND_OK) {
            *codec = candidates[i];
            *info = candidate_info;
            *state = candidate_state;
            return LND_OK;
        }
        if (i == 0) result = r;
        if (r == LND_ERR_OUT_OF_MEMORY || r == LND_ERR_BUSY || forced) return r;
    }
    LND_IoSeekBytes(io, 0);
    return result;
}

int32_t lnd_codec_open(lnd_io *io, const char *extension, const char *forced, const LND_CODEC **codec, LND_CODEC_INFO *info, void **state) {
    lnd_codecs_init();
    const LND_CODEC *candidates[LND_MAX_CODECS];
    uint32_t count = 0;
    bool system = lnd_cfg_bool(LND_CFG_CODECS_SYSTEM);
    for (uint32_t i = 0; i < lnd_codecs_count; i++) {
        const LND_CODEC *c = lnd_codecs[i];
        if ((!(c->flags & LND_CODEC_FLAG_SYSTEM) || system) && (!forced || !strcmp(c->name, forced))) candidates[count++] = c;
    }
    return lnd_codec_open_candidates(io, candidates, count, forced != nullptr, extension, codec, info, state);
}

int32_t LND_CodecRegister(const LND_CODEC *codec) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!codec || !codec->name || !*codec->name || !codec->open || !codec->read || !codec->close) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_codecs_init();
    int32_t r = LND_OK;
    for (uint32_t i = 0; i < lnd_codecs_count; i++) {
        if (lnd_codecs[i] == codec) {
            lnd_context_unlock();
            return LND_OK;
        }
        if (!strcmp(lnd_codecs[i]->name, codec->name)) {
            lnd_context_unlock();
            return lnd_error(LND_ERR_INVALID_ARG);
        }
    }
    if (lnd_codecs_count >= LND_MAX_CODECS)
        r = LND_ERR_BUSY;
    else
        lnd_codecs[lnd_codecs_count++] = codec;
    lnd_context_unlock();
    return lnd_error(r);
}

int32_t LND_CodecUnregister(const LND_CODEC *codec) {
    if (lnd_callback_active()) return lnd_error(LND_ERR_BUSY);

    if (!codec) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    lnd_codecs_init();
    int32_t r = LND_ERR_INVALID_ARG;
    for (uint32_t i = 0; i < lnd_codecs_count; i++) {
        if (lnd_codecs[i] == codec) {
            memmove(&lnd_codecs[i], &lnd_codecs[i + 1], sizeof(lnd_codecs[0]) * (lnd_codecs_count - i - 1));
            lnd_codecs_count--;
            r = LND_OK;
            break;
        }
    }
    lnd_context_unlock();
    return lnd_error(r);
}

uint32_t LND_CodecGetCount(void) {
    if (lnd_callback_active()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }

    if (!lnd_context_enter()) {
        lnd_error(LND_ERR_BUSY);
        return 0;
    }
    lnd_codecs_init();
    uint32_t n = lnd_codecs_count;
    lnd_context_unlock();
    return n;
}

const LND_CODEC *LND_CodecGet(uint32_t index) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_codecs_init();
    const LND_CODEC *c = index < lnd_codecs_count ? lnd_codecs[index] : nullptr;
    lnd_context_unlock();
    return c;
}

const char *LND_CodecGetName(const LND_CODEC *codec) { return codec ? codec->name : nullptr; }

const char *LND_CodecGetExtensions(const LND_CODEC *codec) { return codec ? codec->extensions : nullptr; }

uint32_t LND_CodecGetFlags(const LND_CODEC *codec) { return codec ? codec->flags : 0; }

const LND_CODEC *LND_CodecFind(const char *name) {
    if (lnd_callback_active()) return lnd_error_null(LND_ERR_BUSY);
    if (!name) return nullptr;
    if (!lnd_context_enter()) return lnd_error_null(LND_ERR_BUSY);
    lnd_codecs_init();
    const LND_CODEC *codec = nullptr;
    for (uint32_t i = 0; i < lnd_codecs_count; i++)
        if (!strcmp(lnd_codecs[i]->name, name)) {
            codec = lnd_codecs[i];
            break;
        }
    lnd_context_unlock();
    return codec;
}

const char *LND_CodecGetStreamIdentifiers(const LND_CODEC *codec) { return codec && codec->stream ? codec->stream_identifiers : nullptr; }

static bool lnd_codec_supports_stream(const LND_CODEC *codec, const char *identifier, size_t length) {
    const char *patterns = LND_CodecGetStreamIdentifiers(codec);
    if (!patterns || !length) return false;
    for (const char *p = patterns; *p;) {
        size_t n = strcspn(p, ";");
        bool prefix = n && p[n - 1] == '*';
        size_t match = n - prefix;
        if ((prefix ? length >= match : length == match) && !memcmp(p, identifier, match)) return true;
        p += n;
        if (*p) p++;
    }
    return false;
}

bool LND_CodecSupportsStream(const LND_CODEC *codec, const char *identifier) {
    return identifier && lnd_codec_supports_stream(codec, identifier, strlen(identifier));
}

bool lnd_codec_stream_supported(const LND_CODEC *const *codecs, uint32_t count, const char *identifiers) {
    if (!identifiers) return true;
    bool supported = false;
    for (const char *p = identifiers; *p && !supported;) {
        size_t span = strcspn(p, ","), n = span;
        while (n && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        const char *start = p;
        while (n && (*start == ' ' || *start == '\t')) {
            start++;
            n--;
        }
        for (uint32_t i = 0; i < count && !supported; i++)
            supported = lnd_codec_supports_stream(codecs[i], start, n);
        p += span;
        if (*p) p++;
    }
    return supported;
}
