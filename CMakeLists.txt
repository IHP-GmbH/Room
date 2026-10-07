cmake_minimum_required(VERSION 3.16)

project(ROOM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ROOM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_ROOM_TOP_LEVEL "${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")

include("${ROOM_ROOT}/cmake/Coverage.cmake")

option(ROOM_BOOTSTRAP_CAPNP "Build Cap'n Proto automatically when capnp-install is missing" ON)
option(ROOM_BUILD_EXAMPLES "Build ROOM example command-line tools" ON)
option(ROOM_BUILD_XSCHEM_TCL "Build Tcl extension (roomtcl.so) for in-process Xschem integration" OFF)

if(ROOM_BUILD_XSCHEM_TCL)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(NOT DEFINED CAPNP_ROOT)
    set(CAPNP_ROOT "${ROOM_ROOT}/third_party/capnp-install" CACHE PATH "Cap'n Proto prefix (bin, include, lib)")
endif()
set(CAPNP_BIN "${CAPNP_ROOT}/bin")
set(CAPNP_INCLUDE "${CAPNP_ROOT}/include")
set(CAPNP_LIB "${CAPNP_ROOT}/lib")

include("${ROOM_ROOT}/cmake/BootstrapCapnp.cmake")
room_bootstrap_capnp("${ROOM_ROOT}" "${CAPNP_ROOT}" "${CAPNP_INCLUDE}")

if(NOT EXISTS "${CAPNP_INCLUDE}/capnp/message.h")
    message(FATAL_ERROR
        "Cap'n Proto not found in ${CAPNP_ROOT}.\n"
        "  Enable ROOM_BOOTSTRAP_CAPNP=ON (default), or run:\n"
        "  Windows: scripts/mkcapnp.cmd\n"
        "  Linux:   ./scripts/mkcapnp.sh\n"
        "See third_party/README.md.")
endif()

find_program(CAPNP_EXECUTABLE capnp PATHS "${CAPNP_BIN}" NO_DEFAULT_PATH REQUIRED)
find_program(CAPNPC_CXX_EXECUTABLE capnpc-c++ PATHS "${CAPNP_BIN}" NO_DEFAULT_PATH REQUIRED)
find_library(CAPNP_LIBRARY capnp PATHS "${CAPNP_LIB}" NO_DEFAULT_PATH REQUIRED)
find_library(KJ_LIBRARY kj PATHS "${CAPNP_LIB}" NO_DEFAULT_PATH REQUIRED)

set(SCHEMA_DIR "${ROOM_ROOT}/schema")
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
    COMMENT "Generating Cap'n Proto sources for ROOM"
    VERBATIM
)

set(ROOM_SOURCES
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
    src/room_paths.cpp
    src/serialization.cpp
    src/compact_codec.cpp
    src/varint_codec.cpp
    ${GENERATED_SOURCES}
)

add_library(room STATIC ${ROOM_SOURCES})
set_target_properties(room PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(room PUBLIC
    "$<BUILD_INTERFACE:${ROOM_ROOT}/src>"
    "$<BUILD_INTERFACE:${GEN_DIR}>"
    "$<BUILD_INTERFACE:${CAPNP_INCLUDE}>"
    "$<INSTALL_INTERFACE:include/ROOM/src>"
    "$<INSTALL_INTERFACE:include/ROOM/generated>"
)

target_link_libraries(room PUBLIC ${CAPNP_LIBRARY} ${KJ_LIBRARY})

add_library(room_utils STATIC
    utils/gds_importer.cpp
    utils/gds_exporter.cpp
    utils/qucs_importer.cpp
    utils/qucs_exporter.cpp
    utils/pulse_params.cpp
    utils/qucs_block_codec.cpp
    utils/coord_scale.cpp
    utils/net_name_propagation.cpp
    utils/pin_retarget.cpp
    utils/em_lookalike_symbol.cpp
    utils/xschem_format.cpp
    utils/xschem_io.cpp
    utils/xschem_importer.cpp
    utils/xschem_exporter.cpp
    utils/primitive_resolver.cpp
    utils/xschem_bridge.cpp
    utils/text_dump.cpp
)
set_target_properties(room_utils PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(room_utils PUBLIC
    "$<BUILD_INTERFACE:${ROOM_ROOT}/utils>"
    "$<INSTALL_INTERFACE:include/ROOM/utils>"
)

target_link_libraries(room_utils PUBLIC room)

if(NOT TARGET ROOM::room)
    add_library(ROOM::room ALIAS room)
endif()
if(NOT TARGET ROOM::room_utils)
    add_library(ROOM::room_utils ALIAS room_utils)
endif()

# OAS support is independent of example tools (needed by Python bindings).
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    add_library(room_oas STATIC
        utils/oas_reader.cpp
        utils/oas_writer.cpp
        utils/oas_geometry.cpp
        utils/oas_strict.cpp
        utils/klayout_util.cpp
        utils/oas_importer.cpp
        utils/oas_exporter.cpp
    )
    set_target_properties(room_oas PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_include_directories(room_oas PUBLIC
        "$<BUILD_INTERFACE:${ROOM_ROOT}/utils>"
        "$<INSTALL_INTERFACE:include/ROOM/utils>"
    )
    target_link_libraries(room_oas PUBLIC room_utils ZLIB::ZLIB)
    target_compile_definitions(room_oas PRIVATE OAS_TRACE=0)
    if(NOT TARGET ROOM::room_oas)
        add_library(ROOM::room_oas ALIAS room_oas)
    endif()
endif()

if(ROOM_BUILD_EXAMPLES)
    add_executable(gds_to_room examples/gds_to_room/main.cpp)
    target_link_libraries(gds_to_room PRIVATE room_utils)

    add_executable(room_to_gds examples/room_to_gds/main.cpp)
    target_link_libraries(room_to_gds PRIVATE room_utils)

    add_executable(qucs_to_room examples/qucs_to_room/main.cpp)
    target_link_libraries(qucs_to_room PRIVATE room_utils)

    add_executable(xschem_to_room examples/xschem_to_room/main.cpp)
    target_link_libraries(xschem_to_room PRIVATE room_utils)

    add_executable(room_to_xschem examples/room_to_xschem/main.cpp)
    target_link_libraries(room_to_xschem PRIVATE room_utils)

    add_executable(room_to_qucs examples/room_to_qucs/main.cpp)
    target_link_libraries(room_to_qucs PRIVATE room_utils)

    add_executable(make_sample_gds tools/make_sample_gds.cpp)

    if(TARGET room_oas)
        add_executable(oas_to_room examples/oas_to_room/main.cpp)
        target_link_libraries(oas_to_room PRIVATE room_oas)
    endif()
endif()

add_executable(make_empty_room_views tools/make_empty_room_views.cpp)
target_link_libraries(make_empty_room_views PRIVATE room)

if(_ROOM_TOP_LEVEL)
    install(TARGETS room room_utils EXPORT ROOMTargets
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
            RUNTIME DESTINATION bin)
    if(TARGET room_oas)
        install(TARGETS room_oas EXPORT ROOMTargets
                ARCHIVE DESTINATION lib
                LIBRARY DESTINATION lib
                RUNTIME DESTINATION bin)
    endif()

    if(ROOM_BUILD_EXAMPLES)
        install(TARGETS gds_to_room room_to_gds qucs_to_room room_to_qucs xschem_to_room room_to_xschem make_sample_gds
                RUNTIME DESTINATION bin)
        if(TARGET oas_to_room)
            install(TARGETS oas_to_room RUNTIME DESTINATION bin)
        endif()
    endif()

    install(DIRECTORY src/ DESTINATION include/ROOM/src FILES_MATCHING PATTERN "*.h")
    install(DIRECTORY utils/ DESTINATION include/ROOM/utils FILES_MATCHING PATTERN "*.h")
    install(DIRECTORY "${GEN_DIR}/" DESTINATION include/ROOM/generated
            FILES_MATCHING PATTERN "*.h")

    install(EXPORT ROOMTargets
            FILE ROOMTargets.cmake
            NAMESPACE ROOM::
            DESTINATION lib/cmake/ROOM)

    install(FILES cmake/ROOMConfig.cmake
            DESTINATION lib/cmake/ROOM)
endif()

include("${ROOM_ROOT}/cmake/Tests.cmake")

if(ROOM_BUILD_XSCHEM_TCL)
    add_subdirectory(integrations/xschem_tcl)
endif()

option(ROOM_BUILD_PYTHON "Build ROOM Python bindings (room_c + ctypes package)" ON)
if(ROOM_BUILD_PYTHON)
    add_subdirectory(python)
endif()
