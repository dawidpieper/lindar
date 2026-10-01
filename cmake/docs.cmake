option(LND_BUILD_DOCS "Add the Ruby/Doxygen documentation target" OFF)
if(LND_BUILD_DOCS)
    find_program(LND_RUBY_EXECUTABLE NAMES ruby REQUIRED)
    find_program(LND_DOXYGEN_EXECUTABLE NAMES doxygen REQUIRED)
    add_custom_target(lindar_docs
        COMMAND "${LND_RUBY_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/docs.rb"
            --output "${CMAKE_CURRENT_BINARY_DIR}/docs"
            --cmake "${CMAKE_COMMAND}" --doxygen "${LND_DOXYGEN_EXECUTABLE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Generating Lindar API documentation"
        VERBATIM USES_TERMINAL)
endif()
