option(CORE_BUILD_TESTS "Build CORE tests" ON)
option(CORE_BUILD_OAS_TESTS "Build OAS hierarchy tests (requires zlib)" ON)

if(CORE_BUILD_TESTS)
    enable_testing()

    set(_sample_gds "${CMAKE_SOURCE_DIR}/testdata/sample.gds")
    set(_sg13g2_gds "${CMAKE_SOURCE_DIR}/examples/gds_to_core/data/sg13g2_stdcell.gds")

    add_executable(gds_core_roundtrip tests/gds_core_roundtrip.cpp)
    target_link_libraries(gds_core_roundtrip PRIVATE core_utils)

    find_program(KLAYOUT_EXECUTABLE
        NAMES klayout klayout.exe klayout_app klayout_app.exe
        HINTS
            "$ENV{LOCALAPPDATA}/KLayout"
            "$ENV{APPDATA}/KLayout"
            "C:/Program Files/KLayout"
            "C:/Program Files (x86)/KLayout"
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
            )
            target_include_directories(core_oas PUBLIC
                "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/utils>"
            )
            target_link_libraries(core_oas PUBLIC ZLIB::ZLIB)
            target_compile_definitions(core_oas PRIVATE OAS_TRACE=0)

            add_executable(oas_hierarchy tests/oas_hierarchy.cpp)
            target_link_libraries(oas_hierarchy PRIVATE core_oas)

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
        else()
            message(STATUS "zlib not found; OAS hierarchy test not registered")
        endif()
    endif()

    add_custom_target(check-gds
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" -V -R "roundtrip|oas_hierarchy"
        COMMENT "Run GDS/OAS tests with timing output"
        USES_TERMINAL
    )
endif()
