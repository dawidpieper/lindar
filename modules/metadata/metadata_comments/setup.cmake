if(LND_MODULE_METADATA_IO)
    lnd_vendor_ogg()
    list(APPEND ARG_LIBS lnd_ogg)
endif()
