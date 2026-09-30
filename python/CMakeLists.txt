# Python bindings for ROOM (C API shared library + ctypes package).
if(NOT ROOM_BUILD_PYTHON)
    return()
endif()

add_library(room_c SHARED
    capi/room_c.cpp
)

target_include_directories(room_c
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/capi>"
        "$<INSTALL_INTERFACE:include/ROOM/python>"
)

target_link_libraries(room_c PUBLIC room_utils)

if(TARGET room_oas)
    target_link_libraries(room_c PUBLIC room_oas)
    target_compile_definitions(room_c PRIVATE ROOM_HAS_OAS=1)
else()
    target_compile_definitions(room_c PRIVATE ROOM_HAS_OAS=0)
endif()

target_compile_definitions(room_c PRIVATE ROOM_C_EXPORTS)

# Static ROOM into a shared module needs PIC on ELF.
set_target_properties(room room_utils PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(TARGET room_oas)
    set_target_properties(room_oas PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

set_target_properties(room_c PROPERTIES
    OUTPUT_NAME "room_c"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/python"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/python"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/python"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/python"
)

# Copy package sources + native DLL into the source package for pip install.
add_custom_command(TARGET room_c POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/python/room"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/room"
            "${CMAKE_BINARY_DIR}/python/room"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:room_c>"
            "${CMAKE_CURRENT_SOURCE_DIR}/room/$<TARGET_FILE_NAME:room_c>"
    COMMENT "Syncing Python package and native library"
)

if(_ROOM_TOP_LEVEL)
    find_program(ROOM_PYTHON_EXECUTABLE NAMES python3 python)
    if(ROOM_PYTHON_EXECUTABLE)
        execute_process(
            COMMAND ${ROOM_PYTHON_EXECUTABLE} -c "import pytest"
            RESULT_VARIABLE _ROOM_PYTEST_OK
            OUTPUT_QUIET ERROR_QUIET
        )
        if(_ROOM_PYTEST_OK EQUAL 0)
            set(_py_env "ROOM_C_DLL=$<TARGET_FILE:room_c>;PYTHONPATH=${CMAKE_SOURCE_DIR}/python")
            if(WIN32 AND DEFINED ENV{MINGW_BIN})
                set(_py_env "${_py_env};MINGW_BIN=$ENV{MINGW_BIN};PATH=$ENV{MINGW_BIN};$ENV{PATH}")
            elseif(WIN32)
                set(_py_env "${_py_env};MINGW_BIN=C:/msys64/mingw64/bin;PATH=C:/msys64/mingw64/bin;$ENV{PATH}")
            endif()
            add_test(
                NAME python_room_tests
                COMMAND ${ROOM_PYTHON_EXECUTABLE} -m pytest -q
                        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            )
            set_tests_properties(python_room_tests PROPERTIES
                LABELS "python"
                TIMEOUT 120
                ENVIRONMENT "${_py_env}"
            )
        else()
            message(STATUS "pytest not found for ${ROOM_PYTHON_EXECUTABLE}; skipping python_room_tests")
        endif()
    endif()
endif()
