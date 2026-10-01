#pragma once

#include "lindar_http.h"
#if defined(__ANDROID__)
#include <jni.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Get the libcurl transport descriptor.
 * Manual sessions reject proxies and disable environment proxy discovery; proxies require worker
 * mode. This build omits HTTP/2, HTTP/3 and response decompression and requests identity encoding.
 *
 * @return The static libcurl transport descriptor; no release is needed.
 */
LND_API const LND_HTTP_TRANSPORT *LND_HttpCurlGetTransport(void);
#if defined(__ANDROID__)
/** Initialise curl's Android integration from vm and connectivity_manager.
 * Call from an attached JNI thread after each LND_LibraryInit, before opening HTTP sources.
 * Requires INTERNET and ACCESS_NETWORK_STATE permissions; the JNI reference is retained until shutdown.
 *
 * @param vm Android Java VM; must outlive curl use.
 * @param connectivity_manager Android ConnectivityManager used by the resolver.
 * @return LND_OK or a negative error.
 */
LND_API int32_t LND_HttpCurlInitAndroid(JavaVM *vm, jobject connectivity_manager);
#endif

#ifdef __cplusplus
}
#endif
