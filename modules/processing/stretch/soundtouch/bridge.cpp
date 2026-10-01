#include "bridge.h"
#include "../stretch.h"
#include "SoundTouch.h"

#include <new>

using soundtouch::SoundTouch;

static_assert(LND_ST_INPUT_SEQUENCE_FRAMES == SETTING_NOMINAL_INPUT_SEQUENCE);
static_assert(LND_ST_OUTPUT_SEQUENCE_FRAMES == SETTING_NOMINAL_OUTPUT_SEQUENCE);
static_assert(LND_ST_INITIAL_LATENCY_FRAMES == SETTING_INITIAL_LATENCY);

static int32_t exception_error() {
    try {
        throw;
    } catch (const std::bad_alloc &) {
        return LND_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return LND_ERR_EXTERNAL;
    }
}

void *lnd_st_create(uint32_t channels, uint32_t sample_rate_hz, int32_t *error) {
    SoundTouch *engine = nullptr;
    try {
        engine = new SoundTouch;
        engine->setChannels(channels);
        engine->setSampleRate(sample_rate_hz);
        *error = LND_OK;
        return engine;
    } catch (...) {
        *error = exception_error();
        delete engine;
        return nullptr;
    }
}

void lnd_st_destroy(void *engine) { delete static_cast<SoundTouch *>(engine); }

int32_t lnd_st_param(void *engine, int32_t param, float value) {
    try {
        auto *s = static_cast<SoundTouch *>(engine);
        switch (param) {
        case LND_STRETCH_TEMPO_INDEX:
            s->setTempo(value);
            break;
        case LND_STRETCH_PITCH_INDEX:
            s->setPitch(value);
            break;
        case LND_STRETCH_RATE_INDEX:
            s->setRate(value);
            break;
        default:
            return LND_ERR_INVALID_ARG;
        }
        return LND_OK;
    } catch (...) {
        return exception_error();
    }
}

int32_t lnd_st_setting(void *engine, int32_t setting, int32_t value) {
    try {
        return static_cast<SoundTouch *>(engine)->setSetting(setting, value) ? LND_OK : LND_ERR_INVALID_ARG;
    } catch (...) {
        return exception_error();
    }
}

int32_t lnd_st_get_setting(void *engine, int32_t setting) { return static_cast<SoundTouch *>(engine)->getSetting(setting); }

int32_t lnd_st_put(void *engine, const float *pcm, uint32_t frames) {
    try {
        static_cast<SoundTouch *>(engine)->putSamples(pcm, frames);
        return LND_OK;
    } catch (...) {
        return exception_error();
    }
}

int64_t lnd_st_receive(void *engine, float *pcm, uint32_t frames) {
    try {
        return static_cast<SoundTouch *>(engine)->FIFOProcessor::receiveSamples(pcm, frames);
    } catch (...) {
        return exception_error();
    }
}

uint32_t lnd_st_available(void *engine) { return static_cast<SoundTouch *>(engine)->numSamples(); }
uint32_t lnd_st_pending(void *engine) { return static_cast<SoundTouch *>(engine)->numUnprocessedSamples(); }

int32_t lnd_st_clear(void *engine) {
    try {
        static_cast<SoundTouch *>(engine)->clear();
        return LND_OK;
    } catch (...) {
        return exception_error();
    }
}

const char *lnd_st_version(void) { return SoundTouch::getVersionString(); }

#ifdef LND_ST_TESTING
#include <cstdlib>

static int32_t allocation_limit = -1;
static uint32_t live_allocations;

void *operator new(std::size_t size) {
    if (allocation_limit == 0) throw std::bad_alloc();
    if (allocation_limit > 0) allocation_limit--;
    void *p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    live_allocations++;
    return p;
}

void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept {
    if (p) live_allocations--;
    std::free(p);
}
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }

extern "C" void lnd_st_test_alloc(int32_t after) { allocation_limit = after; }
extern "C" uint32_t lnd_st_test_live(void) { return live_allocations; }
#endif
