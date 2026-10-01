lnd_add_module(aac DECODER ENCODER LANGUAGES CXX DECODER_REQUIRES mp4 SOURCES decoder.c encoder.c ENCODER_SOURCES mp4.c LIBS lnd_fdk SETUP setup.cmake)
