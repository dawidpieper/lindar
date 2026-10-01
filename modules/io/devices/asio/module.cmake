lnd_add_module(asio PROVIDES device_backend PROVIDER_KIND EXTENSION PROVIDER lnd_backend_asio_vt DEFAULT OFF PLATFORM WINDOWS OS_MODE THREADS
    SOURCES backend.c driver.c session.c pcm.c api.c
    REQUIRES devices HEADER lindar_asio.h
    LIBS ole32 advapi32 user32 avrt SETUP setup.cmake)
