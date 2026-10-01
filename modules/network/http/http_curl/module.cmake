lnd_add_module(http_curl PROVIDES http_transport PROVIDER_KIND NATIVE PROVIDER lnd_http_curl DEFAULT OFF OS_MODE REQUIRES http SOURCES transport.c HEADER lindar_http_curl.h LIBS lnd_curl
    SETUP setup.cmake PRIORITY 05 INIT lnd_http_curl_init FREE lnd_http_curl_free)
