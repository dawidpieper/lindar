function(lnd_soundtouch_patch file old new)
    set(key "${file}")
    string(REPLACE "." "_" key "${key}")
    set(source "${LND_ST_${key}}")
    if(NOT DEFINED LND_ST_${key})
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dir}/source/SoundTouch/${file}")
        file(READ "${dir}/source/SoundTouch/${file}" source)
        string(REPLACE "\r\n" "\n" source "${source}")
    endif()
    string(FIND "${source}" "${old}" position)
    if(position LESS 0)
        message(FATAL_ERROR "SoundTouch safety patch does not match ${file}")
    endif()
    string(REPLACE "${old}" "${new}" source "${source}")
    set(LND_ST_${key} "${source}" PARENT_SCOPE)
endfunction()

lnd_soundtouch_patch(SoundTouch.cpp [=[    pRateTransposer = new RateTransposer();]=] [=[    pRateTransposer = nullptr;
    pTDStretch = nullptr;
    try {
    pRateTransposer = new RateTransposer();]=])
lnd_soundtouch_patch(SoundTouch.cpp [=[    bSrateSet = false;
}]=] [=[    bSrateSet = false;
    } catch (...) {
        delete pRateTransposer;
        delete pTDStretch;
        throw;
    }
}]=])
lnd_soundtouch_patch(RateTransposer.cpp [=[    pAAFilter = new AAFilter(64);
    pTransposer = TransposerBase::newInstance();
    clear();]=] [=[    pAAFilter = nullptr;
    pTransposer = nullptr;
    try {
        pAAFilter = new AAFilter(64);
        pTransposer = TransposerBase::newInstance();
        clear();
    } catch (...) {
        delete pAAFilter;
        delete pTransposer;
        throw;
    }]=])
lnd_soundtouch_patch(AAFilter.cpp [=[using namespace soundtouch;]=] [=[#include <memory>

using namespace soundtouch;]=])
lnd_soundtouch_patch(AAFilter.cpp [=[    setLength(len);]=] [=[    try { setLength(len); }
    catch (...) { delete pFIR; throw; }]=])
lnd_soundtouch_patch(AAFilter.cpp [=[    work = new double[length];
    coeffs = new SAMPLETYPE[length];]=] [=[    std::unique_ptr<double[]> workStorage(new double[length]);
    std::unique_ptr<SAMPLETYPE[]> coeffStorage(new SAMPLETYPE[length]);
    work = workStorage.get();
    coeffs = coeffStorage.get();]=])
lnd_soundtouch_patch(AAFilter.cpp [=[    delete[] work;
    delete[] coeffs;]=] [=[]=])
lnd_soundtouch_patch(TDStretch.cpp [=[    setParameters(44100, DEFAULT_SEQUENCE_MS, DEFAULT_SEEKWINDOW_MS, DEFAULT_OVERLAP_MS);
    setTempo(1.0f);

    clear();]=] [=[    try {
        setParameters(44100, DEFAULT_SEQUENCE_MS, DEFAULT_SEEKWINDOW_MS, DEFAULT_OVERLAP_MS);
        setTempo(1.0f);
        clear();
    } catch (...) {
        delete[] pMidBufferUnaligned;
        throw;
    }]=])
lnd_soundtouch_patch(TDStretch.cpp [=[        delete[] pMidBufferUnaligned;

        pMidBufferUnaligned = new]=] [=[        delete[] pMidBufferUnaligned;
        pMidBufferUnaligned = nullptr;

        pMidBufferUnaligned = new]=])
lnd_soundtouch_patch(FIRFilter.cpp [=[    delete[] filterCoeffs;
    filterCoeffs = new]=] [=[    delete[] filterCoeffs;
    filterCoeffs = nullptr;
    filterCoeffs = new]=])
lnd_soundtouch_patch(FIRFilter.cpp [=[    delete[] filterCoeffsStereo;
    filterCoeffsStereo = new]=] [=[    delete[] filterCoeffsStereo;
    filterCoeffsStereo = nullptr;
    filterCoeffsStereo = new]=])
lnd_soundtouch_patch(sse_optimized.cpp [=[    delete[] filterCoeffsUnalign;
    filterCoeffsUnalign = new]=] [=[    delete[] filterCoeffsUnalign;
    filterCoeffsUnalign = nullptr;
    filterCoeffsUnalign = new]=])

foreach(file IN LISTS sources)
    string(REPLACE "." "_" key "${file}")
    if(DEFINED LND_ST_${key})
        set(path "${CMAKE_CURRENT_BINARY_DIR}/vendor/soundtouch/${file}")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/vendor/soundtouch")
        file(WRITE "${path}.tmp" "${LND_ST_${key}}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${path}.tmp" "${path}" COMMAND_ERROR_IS_FATAL ANY)
        file(REMOVE "${path}.tmp")
        list(APPEND patched_sources "${path}")
    else()
        list(APPEND patched_sources "${dir}/source/SoundTouch/${file}")
    endif()
endforeach()
