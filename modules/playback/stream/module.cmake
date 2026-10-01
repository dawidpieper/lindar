lnd_add_module(stream OS_MODE THREADS SOURCES stream.c REQUIRES graph PRIORITY 25 INIT lnd_stream_init FREE lnd_stream_cleanup)
