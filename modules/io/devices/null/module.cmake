lnd_add_module(null PROVIDES device_backend PROVIDER_KIND FALLBACK PROVIDER lnd_backend_null_vt CONFIG config.def OS_MODE THREADS SOURCES null.c REQUIRES devices HEADER lindar_null.h)
