lnd_add_module(wasapi PROVIDES device_backend PROVIDER_KIND NATIVE PROVIDER lnd_backend_wasapi_vt PLATFORM WINDOWS OS_MODE THREADS SOURCES wasapi.c REQUIRES devices LIBS avrt ole32)
