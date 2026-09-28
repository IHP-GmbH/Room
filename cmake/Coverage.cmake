option(ROOM_ENABLE_COVERAGE "Enable GCC/MinGW coverage instrumentation" OFF)

if(ROOM_ENABLE_COVERAGE)
    if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(STATUS "ROOM code coverage enabled")
        add_compile_options(-O0 -g --coverage -fprofile-abs-path)
        add_link_options(--coverage)
        add_compile_definitions(COVERAGE_BUILD)
    else()
        message(WARNING "ROOM_ENABLE_COVERAGE is only supported with GCC/MinGW")
    endif()
endif()

function(room_add_coverage_report)
    if(NOT ROOM_ENABLE_COVERAGE)
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
    set(_by_file "${CMAKE_SOURCE_DIR}/coverage_by_file.txt")
    set(_gcovr_base
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
    )
    add_custom_target(coverage-report
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage by file:"
        COMMAND ${_gcovr_cmd} ${_gcovr_base} --txt "${_by_file}" --sort filename
        COMMAND ${CMAKE_COMMAND} -E cat "${_by_file}"
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage summary:"
        COMMAND ${_gcovr_cmd} ${_gcovr_base} --html-details -o "${_report}" --print-summary
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Generating coverage reports (${_by_file}, ${_report})"
        USES_TERMINAL
    )
endfunction()
