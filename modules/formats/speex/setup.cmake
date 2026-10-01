if(NOT TARGET lnd_speex)
    set(speex_dir ${LND_VENDOR_DIR}/speex)
    set(speex_sources cb_search.c exc_10_32_table.c exc_8_128_table.c filters.c gain_table.c hexc_table.c high_lsp_tables.c
        lsp.c ltp.c speex.c stereo.c vbr.c vq.c bits.c exc_10_16_table.c exc_20_32_table.c exc_5_256_table.c exc_5_64_table.c
        gain_table_lbr.c hexc_10_32_table.c lpc.c lsp_tables_nb.c modes.c modes_wb.c nb_celp.c quant_lsp.c sb_celp.c
        speex_callbacks.c speex_header.c window.c)
    list(TRANSFORM speex_sources PREPEND ${speex_dir}/libspeex/)
    add_library(lnd_speex STATIC ${speex_sources})
    set(speex_gen ${LND_GENERATED_DIR}/speex)
    file(MAKE_DIRECTORY ${speex_gen}/speex)
    file(WRITE ${speex_gen}/speex/speex_config_types.h "#pragma once\n#include <stdint.h>\ntypedef int16_t spx_int16_t;\ntypedef uint16_t spx_uint16_t;\ntypedef int32_t spx_int32_t;\ntypedef uint32_t spx_uint32_t;\n")
    target_include_directories(lnd_speex PUBLIC ${speex_dir}/include ${speex_gen} ${speex_gen}/speex PRIVATE ${speex_dir}/libspeex)
    target_compile_definitions(lnd_speex PRIVATE FLOATING_POINT USE_ALLOCA HAVE_ALLOCA_H EXPORT=)
    set_target_properties(lnd_speex PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
    lnd_vendor_quiet(lnd_speex)
endif()
