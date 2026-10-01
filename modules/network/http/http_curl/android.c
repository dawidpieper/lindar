#include "lindar_http_curl.h"
#include "src/context.h"
#include "src/error.h"
#include <ares.h>

int32_t LND_HttpCurlInitAndroid(JavaVM *vm, jobject connectivity_manager) {
    if (!vm || !connectivity_manager) return lnd_error(LND_ERR_INVALID_ARG);
    if (!lnd_context_enter()) return lnd_error(LND_ERR_BUSY);
    int32_t result = LND_ERR_STATE;
    if (!lnd_ctx.initialized) goto done;
    if (ares_library_android_initialized() == ARES_SUCCESS) {
        result = LND_ERR_BUSY;
        goto done;
    }
    JNIEnv *env = nullptr;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK || !env) {
        result = LND_ERR_INVALID_ARG;
        goto done;
    }
    result = LND_ERR_EXTERNAL;
    if ((*env)->ExceptionCheck(env)) goto done;
    jclass type = (*env)->FindClass(env, "android/net/ConnectivityManager");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto done;
    }
    if (!type) goto done;
    bool valid = (*env)->IsInstanceOf(env, connectivity_manager, type);
    (*env)->DeleteLocalRef(env, type);
    if (!valid) {
        result = LND_ERR_INVALID_ARG;
        goto done;
    }
    ares_library_init_jvm(vm);
    int status = ares_library_init_android(connectivity_manager);
    result = status == ARES_SUCCESS ? LND_OK : status == ARES_ENOMEM ? LND_ERR_OUT_OF_MEMORY : LND_ERR_EXTERNAL;
done:
    lnd_context_unlock();
    return lnd_error(result);
}
