option(CORE_BUILD_TESTS "Build CORE tests" ON)
option(CORE_BUILD_OAS_TESTS "Build OAS tests (requires zlib; round-trip also needs KLayout)" ON)

if(CORE_BUILD_TESTS)
    enable_testing()

    set(_sample_gds "${CMAKE_SOURCE_DIR}/testdata/sample.gds")
    set(_sg13g2_gds "${CMAKE_SOURCE_DIR}/examples/gds_to_core/data/sg13g2_stdcell.gds")
    set(_ctest_out "${CMAKE_BINARY_DIR}/tests")

    add_executable(gds_core_roundtrip tests/gds_core_roundtrip.cpp)
    target_link_libraries(gds_core_roundtrip PRIVATE core_utils)

    add_executable(lib_index tests/lib_index.cpp)
    target_link_libraries(lib_index PRIVATE core_utils)

    add_executable(view_payload_roundtrip tests/view_payload_roundtrip.cpp)
    target_link_libraries(view_payload_roundtrip PRIVATE core_utils)

    add_executable(lib_index_persist tests/lib_index_persist.cpp)
    target_link_libraries(lib_index_persist PRIVATE core_utils)

    add_executable(compact_geometry_size tests/compact_geometry_size.cpp)
    target_link_libraries(compact_geometry_size PRIVATE core_utils)

    add_executable(compact_repetition_unit tests/compact_repetition_unit.cpp)
    target_link_libraries(compact_repetition_unit PRIVATE core)

    add_test(
        NAME compact_repetition_unit
        COMMAND compact_repetition_unit
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_tests_properties(compact_repetition_unit PROPERTIES
        LABELS "core;compact"
        TIMEOUT 30
    )

    find_program(KLAYOUT_EXECUTABLE
        NAMES klayout klayout.exe klayout_app klayout_app.exe
        HINTS
            "$ENV{LOCALAPPDATA}/KLayout"
            "$ENV{APPDATA}/KLayout"
            "C:/Program Files/KLayout"
            "C:/Program Files (x86)/KLayout"
    )

    add_test(
        NAME lib_index
        COMMAND lib_index "${_sample_gds}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_tests_properties(lib_index PROPERTIES
        LABELS "gds;index"
        TIMEOUT 60
    )

    add_test(
        NAME view_payload_roundtrip
        COMMAND view_payload_roundtrip "${_sample_gds}" "${_ctest_out}/view_payload_roundtrip.core"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_tests_properties(view_payload_roundtrip PROPERTIES
        LABELS "core;payload"
        TIMEOUT 60
    )

    add_test(
        NAME lib_index_persist
        COMMAND lib_index_persist "${_sample_gds}" "${_ctest_out}/lib_index_persist.core"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_tests_properties(lib_index_persist PROPERTIES
        LABELS "core;index"
        TIMEOUT 60
    )

    add_test(
        NAME compact_geometry_size
        COMMAND compact_geometry_size "${_sg13g2_gds}"
                "${_ctest_out}/compact_compare_verbose.core"
                "${_ctest_out}/compact_compare_compact.core"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_tests_properties(compact_geometry_size PROPERTIES
        LABELS "core;compact"
        TIMEOUT 120
    )

    add_test(
        NAME sample_gds_roundtrip
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/run_sample_gds_roundtrip_test.sh"
                "${CMAKE_BINARY_DIR}"
                "${_sample_gds}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_tests_properties(sample_gds_roundtrip PROPERTIES
        LABELS "gds;sample;roundtrip"
        TIMEOUT 60
    )

    if(KLAYOUT_EXECUTABLE)
        add_test(
            NAME sg13g2_stdcell_gds_roundtrip
            COMMAND "${CMAKE_SOURCE_DIR}/scripts/run_sg13g2_stdcell_gds_roundtrip_test.sh"
                    "${CMAKE_BINARY_DIR}"
                    "${_sg13g2_gds}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
        set_tests_properties(sg13g2_stdcell_gds_roundtrip PROPERTIES
            LABELS "gds;sg13g2;roundtrip"
            TIMEOUT 180
        )
    else()
        message(STATUS "KLayout not found; sg13g2_stdcell_gds_roundtrip test not registered")
    endif()

    if(CORE_BUILD_OAS_TESTS)
        find_package(ZLIB QUIET)
        if(ZLIB_FOUND)
            add_library(core_oas STATIC
                utils/oas_reader.cpp
                utils/oas_writer.cpp
                utils/klayout_util.cpp
                utils/oas_importer.cpp
                utils/oas_exporter.cpp
            )
            target_include_directories(core_oas PUBLIC
                "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/utils>"
            )
            target_link_libraries(core_oas PUBLIC core_utils ZLIB::ZLIB)
            target_compile_definitions(core_oas PRIVATE OAS_TRACE=0)

            add_executable(oas_hierarchy tests/oas_hierarchy.cpp)
            target_link_libraries(oas_hierarchy PRIVATE core_oas)

            add_executable(oas_core_roundtrip tests/oas_core_roundtrip.cpp)
            target_link_libraries(oas_core_roundtrip PRIVATE core_oas)

            add_test(
                NAME oas_hierarchy
                COMMAND "${CMAKE_SOURCE_DIR}/scripts/run_oas_hierarchy_test.sh"
                        "${CMAKE_BINARY_DIR}"
                        "${_sg13g2_gds}"
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            )
            set_tests_properties(oas_hierarchy PROPERTIES
                LABELS "oas;hierarchy"
                TIMEOUT 120
            )

            if(KLAYOUT_EXECUTABLE)
                add_test(
                    NAME oas_core_roundtrip
                    COMMAND "${CMAKE_SOURCE_DIR}/scripts/run_oas_core_roundtrip_test.sh"
                            "${CMAKE_BINARY_DIR}"
                            ""
                            "${_sg13g2_gds}"
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                )
                set_tests_properties(oas_core_roundtrip PROPERTIES
                    LABELS "oas;roundtrip"
                    TIMEOUT 300
                )
            else()
                message(STATUS "KLayout not found; oas_core_roundtrip test not registered")
            endif()
        else()
            message(STATUS "zlib not found; OAS tests not registered")
        endif()
    endif()

    add_custom_target(check-gds
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" -V -R "roundtrip|oas_"
        COMMENT "Run GDS/OAS tests with timing output"
        USES_TERMINAL
    )

    core_add_coverage_report()
endif()
