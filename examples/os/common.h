#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static inline void sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <errno.h>
#include <time.h>
static inline void sleep_ms(unsigned ms) {
    struct timespec delay = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000};
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {}
}
#endif
