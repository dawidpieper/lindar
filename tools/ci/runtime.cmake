cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()
find_program(objdump NAMES llvm-objdump objdump REQUIRED)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${objdump}")
file(GET_RUNTIME_DEPENDENCIES
    LIBRARIES "${LND_LIBRARY}"
    DIRECTORIES "${LND_RUNTIME_DIR}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    PRE_EXCLUDE_REGEXES "^[Aa][Pp][Ii]-" "^[Ee][Xx][Tt]-"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*")
if(unresolved)
    message(FATAL_ERROR "Missing DLL dependencies: ${unresolved}")
endif()
get_filename_component(destination "${LND_LIBRARY}" DIRECTORY)
foreach(library IN LISTS resolved)
    file(COPY "${library}" DESTINATION "${destination}")
endforeach()
