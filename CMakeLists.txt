cmake_minimum_required(VERSION 3.16)

project(CORE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_CORE_TOP_LEVEL "${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")

include("${CORE_ROOT}/cmake/Coverage.cmake")

option(CORE_BOOTSTRAP_CAPNP "Build Cap'n Proto automatically when capnp-install is missing" ON)
option(CORE_BUILD_EXAMPLES "Build CORE example command-line tools" ON)
option(CORE_BUILD_XSCHEM_TCL "Build Tcl extension (coretcl.so) for in-process Xschem integration" OFF)

if(CORE_BUILD_XSCHEM_TCL)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT DEFINED CAPNP_ROOT)
    set(CAPNP_ROOT "${CORE_ROOT}/third_party/capnp-install" CACHE PATH "Cap'n Proto prefix (bin, include, lib)")
endif()
set(CAPNP_BIN "${CAPNP_ROOT}/bin")
set(CAPNP_INCLUDE "${CAPNP_ROOT}/include")
set(CAPNP_LIB "${CAPNP_ROOT}/lib")

include("${CORE_ROOT}/cmake/BootstrapCapnp.cmake")
core_bootstrap_capnp("${CORE_ROOT}" "${CAPNP_ROOT}" "${CAPNP_INCLUDE}")

if(NOT EXISTS "${CAPNP_INCLUDE}/capnp/message.h")
    message(FATAL_ERROR
        "Cap'n Proto not found in ${CAPNP_ROOT}.\n"
        "  Enable CORE_BOOTSTRAP_CAPNP=ON (default), or run:\n"
        "  Windows: scripts/mkcapnp.cmd\n"
        "  Linux:   ./scripts/mkcapnp.sh\n"
        "See third_party/README.md.")
endif()

find_program(CAPNP_EXECUTABLE capnp PATHS "${CAPNP_BIN}" NO_DEFAULT_PATH REQUIRED)
find_program(CAPNPC_CXX_EXECUTABLE capnpc-c++ PATHS "${CAPNP_BIN}" NO_DEFAULT_PATH REQUIRED)
find_library(CAPNP_LIBRARY capnp PATHS "${CAPNP_LIB}" NO_DEFAULT_PATH REQUIRED)
find_library(KJ_LIBRARY kj PATHS "${CAPNP_LIB}" NO_DEFAULT_PATH REQUIRED)

set(SCHEMA_DIR "${CORE_ROOT}/schema")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

file(GLOB SCHEMA_FILES CONFIGURE_DEPENDS "${SCHEMA_DIR}/*.capnp")

set(GENERATED_SOURCES "")
set(GENERATED_HEADERS "")

foreach(SCHEMA ${SCHEMA_FILES})
    get_filename_component(SCHEMA_NAME ${SCHEMA} NAME_WE)
    list(APPEND GENERATED_SOURCES "${GEN_DIR}/${SCHEMA_NAME}.capnp.c++")
    list(APPEND GENERATED_HEADERS "${GEN_DIR}/${SCHEMA_NAME}.capnp.h")
endforeach()

add_custom_command(
    OUTPUT ${GENERATED_SOURCES} ${GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN_DIR}"
    COMMAND ${CAPNP_EXECUTABLE} compile
            -o${CAPNPC_CXX_EXECUTABLE}:${GEN_DIR}
            -I${CAPNP_INCLUDE}
            -I${SCHEMA_DIR}
            common.capnp dm.capnp design.capnp database.capnp views.capnp index.capnp compact.capnp
    DEPENDS ${SCHEMA_FILES}
    WORKING_DIRECTORY ${SCHEMA_DIR}
    COMMENT "Generating Cap'n Proto sources for CORE"
    VERBATIM
)

set(CORE_SOURCES
    src/box.cpp
    src/enums.cpp
    src/shape.cpp
    src/instance.cpp
    src/term.cpp
    src/net.cpp
    src/block.cpp
    src/cell_content.cpp
    src/source_info.cpp
    src/cell.cpp
    src/layer_utils.cpp
    src/lib.cpp
    src/lib_index.cpp
    src/database.cpp
    src/file_summary.cpp
    src/core_paths.cpp
    src/serialization.cpp
    src/compact_codec.cpp
    src/varint_codec.cpp
    ${GENERATED_SOURCES}
)

add_library(core STATIC ${CORE_SOURCES})
set_target_properties(core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(core PUBLIC
    "$<BUILD_INTERFACE:${CORE_ROOT}/src>"
    "$<BUILD_INTERFACE:${GEN_DIR}>"
    "$<BUILD_INTERFACE:${CAPNP_INCLUDE}>"
    "$<INSTALL_INTERFACE:include/CORE/src>"
    "$<INSTALL_INTERFACE:include/CORE/generated>"
)

target_link_libraries(core PUBLIC ${CAPNP_LIBRARY} ${KJ_LIBRARY})

add_library(core_utils STATIC
    utils/gds_importer.cpp
    utils/gds_exporter.cpp
    utils/qucs_importer.cpp
    utils/qucs_exporter.cpp
    utils/xschem_format.cpp
    utils/xschem_io.cpp
    utils/xschem_importer.cpp
    utils/xschem_exporter.cpp
    utils/xschem_bridge.cpp
    utils/text_dump.cpp
)
set_target_properties(core_utils PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(core_utils PUBLIC
    "$<BUILD_INTERFACE:${CORE_ROOT}/utils>"
    "$<INSTALL_INTERFACE:include/CORE/utils>"
)

target_link_libraries(core_utils PUBLIC core)

if(NOT TARGET CORE::core)
    add_library(CORE::core ALIAS core)
endif()
if(NOT TARGET CORE::core_utils)
    add_library(CORE::core_utils ALIAS core_utils)
endif()

if(CORE_BUILD_EXAMPLES)
    add_executable(gds_to_core examples/gds_to_core/main.cpp)
    target_link_libraries(gds_to_core PRIVATE core_utils)

    add_executable(core_to_gds examples/core_to_gds/main.cpp)
    target_link_libraries(core_to_gds PRIVATE core_utils)

    add_executable(qucs_to_core examples/qucs_to_core/main.cpp)
    target_link_libraries(qucs_to_core PRIVATE core_utils)

    add_executable(xschem_to_core examples/xschem_to_core/main.cpp)
    target_link_libraries(xschem_to_core PRIVATE core_utils)

    add_executable(core_to_xschem examples/core_to_xschem/main.cpp)
    target_link_libraries(core_to_xschem PRIVATE core_utils)

    add_executable(make_sample_gds tools/make_sample_gds.cpp)
endif()

add_executable(make_empty_core_views tools/make_empty_core_views.cpp)
target_link_libraries(make_empty_core_views PRIVATE core)

if(_CORE_TOP_LEVEL)
    install(TARGETS core core_utils EXPORT CORETargets
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
            RUNTIME DESTINATION bin)

    if(CORE_BUILD_EXAMPLES)
        install(TARGETS gds_to_core core_to_gds qucs_to_core xschem_to_core core_to_xschem make_sample_gds
                RUNTIME DESTINATION bin)
    endif()

    install(DIRECTORY src/ DESTINATION include/CORE/src FILES_MATCHING PATTERN "*.h")
    install(DIRECTORY utils/ DESTINATION include/CORE/utils FILES_MATCHING PATTERN "*.h")
    install(DIRECTORY "${GEN_DIR}/" DESTINATION include/CORE/generated
            FILES_MATCHING PATTERN "*.h")

    install(EXPORT CORETargets
            FILE CORETargets.cmake
            NAMESPACE CORE::
            DESTINATION lib/cmake/CORE)

    install(FILES cmake/COREConfig.cmake
            DESTINATION lib/cmake/CORE)
endif()

include("${CORE_ROOT}/cmake/Tests.cmake")

if(CORE_BUILD_XSCHEM_TCL)
    add_subdirectory(integrations/xschem_tcl)
endif()
