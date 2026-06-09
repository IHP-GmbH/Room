option(CORE_ENABLE_COVERAGE "Enable GCC/MinGW coverage instrumentation" OFF)

if(CORE_ENABLE_COVERAGE)
    if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(STATUS "CORE code coverage enabled")
        add_compile_options(-O0 -g --coverage -fprofile-abs-path)
        add_link_options(--coverage)
        add_compile_definitions(COVERAGE_BUILD)
    else()
        message(WARNING "CORE_ENABLE_COVERAGE is only supported with GCC/MinGW")
    endif()
endif()

function(core_add_coverage_report)
    if(NOT CORE_ENABLE_COVERAGE)
        return()
    endif()

    find_program(GCOVR_EXECUTABLE gcovr)
    set(_gcovr_cmd "")
    if(GCOVR_EXECUTABLE)
        set(_gcovr_cmd "${GCOVR_EXECUTABLE}")
    else()
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_FOUND)
            set(_gcovr_cmd "${Python3_EXECUTABLE}" -m gcovr)
        endif()
    endif()

    if(NOT _gcovr_cmd)
        message(STATUS "gcovr not found; coverage-report target not available")
        return()
    endif()

    set(_report "${CMAKE_SOURCE_DIR}/coverage.html")
    add_custom_target(coverage-report
        COMMAND ${_gcovr_cmd}
            -r "${CMAKE_SOURCE_DIR}"
            --object-directory "${CMAKE_BINARY_DIR}"
            --merge-mode-functions=merge-use-line-min
            --gcov-ignore-errors=all
            --filter "${CMAKE_SOURCE_DIR}/src/.*"
            --filter "${CMAKE_SOURCE_DIR}/utils/.*"
            --exclude "${CMAKE_SOURCE_DIR}/tests/.*"
            --exclude ".*/build/.*"
            --exclude ".*/build-coverage/.*"
            --exclude ".*/third_party/.*"
            --exclude ".*/generated/.*"
            --exclude ".*/tools/.*"
            --exclude ".*/examples/.*"
            --html-details
            -o "${_report}"
            --print-summary
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Generating coverage report (${_report})"
        USES_TERMINAL
    )
endfunction()
