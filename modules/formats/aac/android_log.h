#pragma once

#include <android/log.h>

static inline int android_errorWriteLog(int tag, const char *issue) {
    return __android_log_print(ANDROID_LOG_WARN, "lindar.fdk-aac", "%x: %s", (unsigned)tag, issue);
}
