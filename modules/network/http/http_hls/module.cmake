set(http_hls_requirements http demux)
if(LND_HTTP_AES128)
    list(APPEND http_hls_requirements OS_MODE)
endif()
lnd_add_module(http_hls DEFAULT_WITH http REQUIRES ${http_hls_requirements} REQUIRES http_packets
    SOURCES playlist.c session.c media.c HEADER lindar_http_hls.h SETUP setup.cmake)
