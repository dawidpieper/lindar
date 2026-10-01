if(LND_HTTP_AES128)
    lnd_vendor_openssl()
    list(APPEND ARG_LIBS lnd_openssl_crypto)
endif()
