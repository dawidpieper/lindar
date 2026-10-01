lnd_add_module(alsa PROVIDES device_backend PROVIDER_KIND NATIVE PROVIDER lnd_backend_alsa_vt PLATFORM LINUX OS_MODE THREADS SOURCES alsa.c REQUIRES devices LIBS ALSA::ALSA SETUP setup.cmake)
