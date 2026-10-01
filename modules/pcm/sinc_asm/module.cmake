lnd_add_module(sinc_asm CONFIG config.def DEFAULT OFF LANGUAGES ASM HEADER lindar_sinc_asm.h SOURCES sinc_asm.c SETUP setup.cmake
    REQUIRES pcm_float audio INIT lnd_sinc_asm_init PRIORITY 06)
