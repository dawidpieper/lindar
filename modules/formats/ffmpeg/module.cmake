option(LND_FFMPEG_BUNDLED "Build and link bundled FFmpeg statically" ON)
lnd_add_module(ffmpeg DEFAULT OFF DECODER ENCODER SOURCES ffmpeg.c decoder.c encoder.c LIBS lnd_ffmpeg SETUP setup.cmake)
