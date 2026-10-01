function(lnd_bungee_patch file old new)
    string(MAKE_C_IDENTIFIER "${file}" key)
    set(source "${LND_BG_${key}}")
    if(NOT DEFINED LND_BG_${key})
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dir}/${file}")
        file(READ "${dir}/${file}" source)
        string(REPLACE "\r\n" "\n" source "${source}")
    endif()
    string(FIND "${source}" "${old}" position)
    if(position LESS 0)
        message(FATAL_ERROR "Bungee safety patch does not match ${file}")
    endif()
    string(REPLACE "${old}" "${new}" source "${source}")
    set(LND_BG_${key} "${source}" PARENT_SCOPE)
endfunction()

lnd_bungee_patch(src/Fourier.cpp [=[#include "Assert.h"]=] [=[#include "Assert.h"
#include <new>]=])
lnd_bungee_patch(src/Fourier.cpp [=[p(pffft_new_setup(1 << log2TransformLength, PFFFT_REAL))
{
}]=] [=[p(pffft_new_setup(1 << log2TransformLength, PFFFT_REAL))
{
    if (!p) throw std::bad_alloc();
}]=])
lnd_bungee_patch(src/Fourier.cpp [=[#include "../submodules/pffft/pffft.h"]=] [=[#include "submodules/pffft/pffft.h"]=])
lnd_bungee_patch(submodules/pffft/pffft.c [=[  int k, m;
  /* unfortunately]=] [=[  int k, m;
  if (!s) return 0;
  /* unfortunately]=])
lnd_bungee_patch(submodules/pffft/pffft.c [=[  s->e = (float*)s->data;]=] [=[  if (!s->data) { free(s); return 0; }
  s->e = (float*)s->data;]=])

lnd_bungee_patch(src/Stretcher.cpp [=[const auto x = transformed.row(i).sum();]=] [=[auto x = transformed.row(i).sum();
            if (x == std::complex<float>{})
                for (int c = 0; c < transformed.cols(); ++c)
                    if (std::norm(transformed(i, c)) > std::norm(x))
                        x = transformed(i, c);]=])

set(patched "${CMAKE_CURRENT_BINARY_DIR}/vendor/bungee")
file(MAKE_DIRECTORY "${patched}")
foreach(file src/Fourier.cpp src/Stretcher.cpp submodules/pffft/pffft.c)
    string(MAKE_C_IDENTIFIER "${file}" key)
    get_filename_component(name "${file}" NAME)
    file(WRITE "${patched}/${name}.tmp" "${LND_BG_${key}}")
    configure_file("${patched}/${name}.tmp" "${patched}/${name}" COPYONLY)
    file(REMOVE "${patched}/${name}.tmp")
endforeach()
set(pffft_source "${patched}/pffft.c")
set(bungee_sources "${patched}/Fourier.cpp" "${patched}/Stretcher.cpp")
foreach(name Assert Grain Grains Input Instrumentation Output Partials Stretch Synthesis Timing Window version)
    list(APPEND bungee_sources "${dir}/src/${name}.cpp")
endforeach()
