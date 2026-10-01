#include "lindar_http_curl.h"
#include "src/context.h"
#include <ares.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                                                                                                               \
    do {                                                                                                                                                       \
        if (!(x)) {                                                                                                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                                                                            \
            return 1;                                                                                                                                          \
        }                                                                                                                                                      \
    } while (0)

lnd_context lnd_ctx;
static bool busy, initialized, exception, class_error, valid = true;
static int env_status, init_status, entered, unlocked, calls, references;
static JavaVM *registered_vm;
static jobject registered_manager;
static int manager_value, type_value;

bool lnd_context_enter(void) {
    if (busy) return false;
    entered++;
    return true;
}
void lnd_context_unlock(void) { unlocked++; }
int32_t lnd_error(int32_t result) { return result; }
int ares_library_android_initialized(void) { return initialized ? ARES_SUCCESS : ARES_ENOTINITIALIZED; }
void ares_library_init_jvm(JavaVM *vm) { registered_vm = vm; }
int ares_library_init_android(jobject manager) {
    calls++;
    registered_manager = manager;
    initialized = init_status == ARES_SUCCESS;
    return init_status;
}

static jboolean exception_check(JNIEnv *env) { return exception; }
static void exception_clear(JNIEnv *env) { exception = false; }
static jclass find_class(JNIEnv *env, const char *name) {
    if (strcmp(name, "android/net/ConnectivityManager")) return nullptr;
    if (class_error) {
        exception = true;
        return nullptr;
    }
    references++;
    return &type_value;
}
static jboolean is_instance(JNIEnv *env, jobject object, jclass type) { return valid && object == &manager_value && type == &type_value; }
static void delete_ref(JNIEnv *env, jobject object) { references--; }
static const struct JNINativeInterface_ native_api = {exception_check, exception_clear, find_class, is_instance, delete_ref};
static JNIEnv env = &native_api;
static jint get_env(JavaVM *vm, void **out, jint version) {
    if (version != JNI_VERSION_1_6) return -1;
    *out = &env;
    return env_status;
}

int main(void) {
    const struct JNIInvokeInterface_ invoke = {get_env};
    JavaVM vm = &invoke;
    CHECK(LND_HttpCurlInitAndroid(nullptr, &manager_value) == LND_ERR_INVALID_ARG);
    CHECK(LND_HttpCurlInitAndroid(&vm, nullptr) == LND_ERR_INVALID_ARG);
    CHECK(entered == 0);
    busy = true;
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_ERR_BUSY);
    busy = false;
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_ERR_STATE);
    lnd_ctx.initialized = true;
    env_status = -1;
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_ERR_INVALID_ARG);
    env_status = JNI_OK;
    exception = true;
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_ERR_EXTERNAL);
    CHECK(exception);
    exception = false;
    class_error = true;
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_ERR_EXTERNAL);
    CHECK(!exception);
    class_error = false;
    valid = false;
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_ERR_INVALID_ARG);
    CHECK(calls == 0 && references == 0);
    valid = true;
    init_status = ARES_ENOMEM;
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_ERR_OUT_OF_MEMORY);
    init_status = ARES_ENOTINITIALIZED;
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_ERR_EXTERNAL);
    init_status = ARES_SUCCESS;
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_OK);
    CHECK(registered_vm == &vm && registered_manager == &manager_value && calls == 3);
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_ERR_BUSY);
    CHECK(calls == 3 && references == 0 && entered == unlocked);
    initialized = false;
    CHECK(LND_HttpCurlInitAndroid(&vm, &manager_value) == LND_OK);
    CHECK(calls == 4 && entered == unlocked);
    return 0;
}
