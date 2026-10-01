lnd_add_module(files OS_MODE REQUIRES codecs file_io SOURCES paths.c source.c HEADER lindar_files.h
    SOURCES_IF output:output.c output:render.c)
