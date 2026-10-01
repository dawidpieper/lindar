#include "stream_test.h"
#include "lindar_stretch.h"
#if LND_MODULE_SOUNDTOUCH
#include "lindar_soundtouch.h"
#endif
#if LND_MODULE_BUNGEE
#include "lindar_bungee.h"
#endif

#include <math.h>

static const LND_STRETCH_BACKEND *default_backend(void) {
    return LND_MODULE_BUNGEE ? LND_StretchBackendFind("bungee") : LND_MODULE_SOUNDTOUCH ? LND_StretchBackendFind("soundtouch") : nullptr;
}

static void selection(void) {
    CHECK((LND_StretchBackendFind("bungee") != nullptr) == !!LND_MODULE_BUNGEE);
    CHECK((LND_StretchBackendFind("soundtouch") != nullptr) == !!LND_MODULE_SOUNDTOUCH);
    CHECK(!LND_StretchBackendFind(nullptr));
    CHECK(!LND_StretchBackendFind("missing"));
    CHECK(LND_StretchBackendSetPriority((const LND_STRETCH_BACKEND *)(uintptr_t)99, 100) == LND_ERR_INVALID_ARG);
    CHECK(LND_StretchBackendSetPriority(LND_StretchBackendFind("bungee"), -1) == LND_ERR_INVALID_ARG);
    CHECK(!LND_NodeCreateStretch(1, 16000, nullptr) && LND_ErrorGetLast() == LND_ERR_STATE);
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    LND_NODE *node = LND_NodeCreateStretch(1, 16000, nullptr);
    if (!default_backend()) {
        CHECK(!node && LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
        finish();
        return;
    }
    CHECK(node && LND_NodeGetStretchBackend(node) == default_backend());
    CHECK(LND_StretchBackendSetPriority(default_backend(), 100) == LND_ERR_STATE);
    LND_STRETCH_INFO info;
    CHECK(LND_NodeGetStretchInfo(node, &info) == LND_OK);
    CHECK(info.backend == default_backend() && info.duration_ratio == 1 && !info.input_ended && !info.error);
    CHECK(LND_NodeSetStretchPitchSemitones(node, 12) == LND_OK && LND_NodeGetStretchPitchSemitones(node) == 12);
    CHECK(LND_NodeSetStretchTempoChangePercent(node, -50) == LND_OK && LND_NodeGetParam(node, LND_STRETCH_PARAM_TEMPO_RATIO) == 0.5f);
    CHECK(LND_NodeSetStretchRateChangePercent(node, 100) == LND_OK && LND_NodeGetParam(node, LND_STRETCH_PARAM_RATE_RATIO) == 2);
    CHECK(LND_NodeSetStretchPitchSemitones(node, 25) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetStretchTempoChangePercent(node, NAN) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetStretchRateChangePercent(node, -100) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeEndStretchInput(node) == LND_OK && LND_NodeResetStretch(node) == LND_OK);
    CHECK(LND_NodeFree(node) == LND_OK);
    node = LND_NodeCreatePitch(1, 16000, 2);
    CHECK(node && LND_NodeGetParam(node, LND_STRETCH_PARAM_PITCH_RATIO) == 2);
    CHECK(LND_NodeGetStretchBackend(node) == default_backend());
    CHECK(LND_NodeFree(node) == LND_OK);
    node = LND_NodeCreateTempo(1, 16000, 0.5f);
    CHECK(node && LND_NodeGetParam(node, LND_STRETCH_PARAM_TEMPO_RATIO) == 0.5f);
    CHECK(LND_NodeFree(node) == LND_OK);
    CHECK(!LND_NodeCreatePitch(1, 16000, 0));
    CHECK(!LND_NodeCreateTempo(1, 16000, INFINITY));
    CHECK(!LND_NodeCreateStretch(1, 16000, &(LND_STRETCH_CONFIG){.backend = (const LND_STRETCH_BACKEND *)(uintptr_t)99}));
    CHECK(!LND_NodeGetStretchBackend(nullptr) && LND_ErrorGetLast() == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeGetStretchInfo(nullptr, &info) == LND_ERR_INVALID_ARG);
    LND_NODE *bus = LND_NodeCreateBus(1, 16000);
    CHECK(LND_NodeResetStretch(bus) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeEndStretchInput(bus) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeSetStretchTempoChangePercent(bus, 10) == LND_ERR_INVALID_ARG);
    CHECK(LND_NodeFree(bus) == LND_OK);
    for (uint32_t i = 0; i < LND_StretchBackendGetCount(); i++) {
        const LND_STRETCH_BACKEND *backend = LND_StretchBackendGet(i);
        node = LND_NodeCreateStretch(1, 16000, &(LND_STRETCH_CONFIG){.backend = backend});
        CHECK(node && LND_NodeGetStretchBackend(node) == backend);
        CHECK(LND_NodeFree(node) == LND_OK);
    }
    finish();
}

static void priorities(void) {
    for (uint32_t i = 0; i < LND_StretchBackendGetCount(); i++) {
        const LND_STRETCH_BACKEND *backend = LND_StretchBackendGet(i);
        CHECK(LND_StretchBackendSetPriority(backend, 500) == LND_OK);
        CHECK(LND_StretchBackendGetPriority(backend) == 500);
        begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
        LND_NODE *node = LND_NodeCreateStretch(1, 16000, nullptr);
        CHECK(node && LND_NodeGetStretchBackend(node) == backend);
        CHECK(LND_NodeFree(node) == LND_OK);
        finish();
        CHECK(LND_StretchBackendGetPriority(backend) == (backend == LND_StretchBackendFind("bungee") ? 200 : 100));
    }
    for (uint32_t i = 0; i < LND_StretchBackendGetCount(); i++)
        CHECK(LND_StretchBackendSetPriority(LND_StretchBackendGet(i), 0) == LND_OK);
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    CHECK(!LND_NodeCreateStretch(1, 16000, nullptr) && LND_ErrorGetLast() == LND_ERR_UNSUPPORTED);
#if LND_MODULE_BUNGEE
    LND_NODE *bungee = LND_NodeCreateBungee(1, 16000, nullptr);
    CHECK(bungee && LND_NodeGetStretchBackend(bungee) == LND_StretchBackendFind("bungee"));
#endif
#if LND_MODULE_SOUNDTOUCH
    LND_NODE *soundtouch = LND_NodeCreateSoundTouch(1, 16000, nullptr);
    CHECK(soundtouch && LND_NodeGetStretchBackend(soundtouch) == LND_StretchBackendFind("soundtouch"));
#endif
    if (default_backend() != nullptr) {
        LND_NODE *node = LND_NodeCreateStretch(1, 16000, &(LND_STRETCH_CONFIG){.backend = default_backend()});
        CHECK(node && LND_NodeGetStretchBackend(node) == default_backend());
        CHECK(LND_NodeFree(node) == LND_OK);
    }
    finish();
#if LND_MODULE_BUNGEE && LND_MODULE_SOUNDTOUCH
    CHECK(LND_StretchBackendSetPriority(LND_StretchBackendFind("soundtouch"), 200) == LND_OK);
    begin(LND_FORMAT_F32, LND_LAYOUT_INTERLEAVED);
    LND_NODE *node = LND_NodeCreateStretch(1, 16000, nullptr);
    CHECK(node && LND_NodeGetStretchBackend(node) == LND_StretchBackendFind("bungee"));
    finish();
#endif
}

int main(void) {
    CHECK(LND_StretchBackendGetCount() == LND_MODULE_BUNGEE + LND_MODULE_SOUNDTOUCH);
    CHECK(!LND_StretchBackendGet(UINT32_MAX));
    CHECK(!LND_StretchBackendGetName(nullptr));
    CHECK(LND_StretchBackendGetPriority(nullptr) == LND_ERR_INVALID_ARG);
    for (uint32_t i = 0; i < LND_StretchBackendGetCount(); i++) {
        const LND_STRETCH_BACKEND *backend = LND_StretchBackendGet(i);
        CHECK(backend && LND_StretchBackendFind(LND_StretchBackendGetName(backend)) == backend);
    }
    selection();
    priorities();
    return report();
}
