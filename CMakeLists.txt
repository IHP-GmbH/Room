cmake_minimum_required(VERSION 3.16)

project(CommonDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(COMMONDB_BOOTSTRAP_CAPNP "Build Cap'n Proto automatically when third_party/capnp-install is missing" ON)

set(CAPNP_ROOT "${CMAKE_SOURCE_DIR}/third_party/capnp-install" CACHE PATH "Cap'n Proto prefix (bin, include, lib)")
set(CAPNP_BIN "${CAPNP_ROOT}/bin")
set(CAPNP_INCLUDE "${CAPNP_ROOT}/include")
set(CAPNP_LIB "${CAPNP_ROOT}/lib")

include("${CMAKE_SOURCE_DIR}/cmake/BootstrapCapnp.cmake")
commondb_bootstrap_capnp("${CAPNP_ROOT}" "${CAPNP_INCLUDE}")

if(NOT EXISTS "${CAPNP_INCLUDE}/capnp/message.h")
    message(FATAL_ERROR
        "Cap'n Proto not found in ${CAPNP_ROOT}.\n"
        "  Enable COMMONDB_BOOTSTRAP_CAPNP=ON (default), or run:\n"
        "  Windows: scripts/mkcapnp.cmd\n"
        "  Linux:   ./scripts/mkcapnp.sh\n"
        "See third_party/README.md.")
endif()

find_program(CAPNP_EXECUTABLE capnp PATHS "${CAPNP_BIN}" NO_DEFAULT_PATH REQUIRED)
find_program(CAPNPC_CXX_EXECUTABLE capnpc-c++ PATHS "${CAPNP_BIN}" NO_DEFAULT_PATH REQUIRED)
find_library(CAPNP_LIBRARY capnp PATHS "${CAPNP_LIB}" NO_DEFAULT_PATH REQUIRED)
find_library(KJ_LIBRARY kj PATHS "${CAPNP_LIB}" NO_DEFAULT_PATH REQUIRED)

set(SCHEMA_DIR "${CMAKE_SOURCE_DIR}/schema")
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")

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
            common.capnp dm.capnp design.capnp database.capnp
    DEPENDS ${SCHEMA_FILES}
    WORKING_DIRECTORY ${SCHEMA_DIR}
    COMMENT "Generating Cap'n Proto sources for CommonDB"
    VERBATIM
)

set(CDB_CORE_SOURCES
    src/box.cpp
    src/enums.cpp
    src/shape.cpp
    src/instance.cpp
    src/term.cpp
    src/net.cpp
    src/block.cpp
    src/cell_content.cpp
    src/cell.cpp
    src/lib.cpp
    src/database.cpp
    src/serialization.cpp
    ${GENERATED_SOURCES}
)

add_library(cdb_core STATIC ${CDB_CORE_SOURCES})

target_include_directories(cdb_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>"
    "$<BUILD_INTERFACE:${GEN_DIR}>"
    "$<BUILD_INTERFACE:${CAPNP_INCLUDE}>"
    "$<INSTALL_INTERFACE:include/CommonDB/src>"
    "$<INSTALL_INTERFACE:include/CommonDB/generated>"
)

target_link_libraries(cdb_core PUBLIC ${CAPNP_LIBRARY} ${KJ_LIBRARY})

add_library(cdb_utils STATIC
    utils/gds_importer.cpp
    utils/qucs_importer.cpp
    utils/qucs_exporter.cpp
    utils/text_dump.cpp
)

target_include_directories(cdb_utils PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/utils>"
    "$<INSTALL_INTERFACE:include/CommonDB/utils>"
)

target_link_libraries(cdb_utils PUBLIC cdb_core)

add_executable(gds_to_cdb examples/gds_to_cdb/main.cpp)
target_link_libraries(gds_to_cdb PRIVATE cdb_utils)

add_executable(qucs_to_cdb examples/qucs_to_cdb/main.cpp)
target_link_libraries(qucs_to_cdb PRIVATE cdb_utils)

add_executable(make_sample_gds tools/make_sample_gds.cpp)

install(TARGETS cdb_core cdb_utils EXPORT CommonDBTargets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

install(TARGETS gds_to_cdb qucs_to_cdb make_sample_gds
        RUNTIME DESTINATION bin)

install(DIRECTORY src/ DESTINATION include/CommonDB/src FILES_MATCHING PATTERN "*.h")
install(DIRECTORY utils/ DESTINATION include/CommonDB/utils FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${GEN_DIR}/" DESTINATION include/CommonDB/generated
        FILES_MATCHING PATTERN "*.h")

install(EXPORT CommonDBTargets
        FILE CommonDBTargets.cmake
        NAMESPACE CommonDB::
        DESTINATION lib/cmake/CommonDB)

install(FILES cmake/CommonDBConfig.cmake
        DESTINATION lib/cmake/CommonDB)
