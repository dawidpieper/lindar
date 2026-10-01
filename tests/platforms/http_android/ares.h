#pragma once

#include <jni.h>

#define ARES_SUCCESS 0
#define ARES_ENOMEM 15
#define ARES_ENOTINITIALIZED 21

int ares_library_android_initialized(void);
void ares_library_init_jvm(JavaVM *vm);
int ares_library_init_android(jobject manager);
