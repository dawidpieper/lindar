lnd_add_module(http DEFAULT OFF ERRORS errors.def CONFIG config.def REQUIRES decode pcm_float audio text id3
    SOURCES session.c protocol.c ogg.c cache.c url.c metadata.c HEADER lindar_http.h PRIORITY 10 SETUP setup.cmake
    INIT lnd_http_init STOP lnd_http_stop FREE lnd_http_free UPDATE lnd_http_update)
