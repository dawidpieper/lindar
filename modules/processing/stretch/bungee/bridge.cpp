#include "bridge.h"
#include "lindar.h"
#include "bungee/Bungee.h"

#include <cmath>
#include <new>

static int32_t exception_error() {
    try {
        throw;
    } catch (const std::bad_alloc &) {
        return LND_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return LND_ERR_EXTERNAL;
    }
}

static Bungee::Request request(const lnd_bg_request *r) {
    auto mode = r->mode == 1 || r->pitch_ratio < 0.25 ? resampleMode_autoIn : r->mode == 2 ? resampleMode_autoOut : resampleMode_autoInOut;
    return {r->position, r->speed, r->pitch_ratio, r->reset != 0, mode};
}

void *lnd_bg_create(uint32_t channels, uint32_t sample_rate_hz, int32_t grain_adjust, int32_t *error) {
    try {
        void *engine = getFunctionsBungeeBasic()->create({(int)sample_rate_hz, (int)sample_rate_hz}, (int)channels, grain_adjust);
        *error = LND_OK;
        return engine;
    } catch (...) {
        *error = exception_error();
        return nullptr;
    }
}

void lnd_bg_destroy(void *engine) { getFunctionsBungeeBasic()->destroy(engine); }
uint32_t lnd_bg_max_input(void *engine) { return (uint32_t)getFunctionsBungeeBasic()->maxInputFrameCount(engine); }

void lnd_bg_preroll(void *engine, lnd_bg_request *r) {
    auto q = request(r);
    getFunctionsBungeeBasic()->preroll(engine, &q);
    r->position = q.position;
    r->reset = q.reset;
}

void lnd_bg_next(void *engine, lnd_bg_request *r) {
    auto q = request(r);
    getFunctionsBungeeBasic()->next(engine, &q);
    r->position = q.position;
    r->reset = q.reset;
}

int32_t lnd_bg_specify(void *engine, const lnd_bg_request *r, double origin, int32_t *begin, int32_t *end) {
    try {
        auto q = request(r);
        auto chunk = getFunctionsBungeeBasic()->specifyGrain(engine, &q, origin);
        *begin = chunk.begin;
        *end = chunk.end;
        return LND_OK;
    } catch (...) {
        return exception_error();
    }
}

int32_t lnd_bg_process(void *engine, const float *pcm, uint32_t stride_samples, lnd_bg_chunk *chunk) {
    try {
        const auto *functions = getFunctionsBungeeBasic();
        functions->analyseGrain(engine, pcm, stride_samples, 0, 0);
        Bungee::OutputChunk output{};
        functions->synthesiseGrain(engine, &output);
        *chunk = {output.data, output.request[0] ? output.request[0]->position : NAN, output.request[1] ? output.request[1]->position : NAN,
                  output.channelStride, (uint32_t)output.frameCount};
        return LND_OK;
    } catch (...) {
        return exception_error();
    }
}

const char *lnd_bg_version(void) { return getFunctionsBungeeBasic()->version(); }

#ifdef LND_BG_TESTING
extern "C" void *lnd_bg_test_allocate(std::size_t size);
extern "C" void lnd_bg_test_release(void *memory);

void *operator new(std::size_t size) {
    void *p = lnd_bg_test_allocate(size ? size : 1);
    if (!p)
        throw std::bad_alloc();
    return p;
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept { lnd_bg_test_release(p); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }
#endif
