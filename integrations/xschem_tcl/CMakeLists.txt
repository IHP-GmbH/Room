find_package(TCL REQUIRED)

add_library(coretcl MODULE core_tcl.cpp)
set_target_properties(coretcl PROPERTIES PREFIX "" OUTPUT_NAME "coretcl")

target_include_directories(coretcl PRIVATE
    ${TCL_INCLUDE_PATH}
    "${CORE_ROOT}/utils"
)

target_link_libraries(coretcl PRIVATE core_utils ${TCL_LIBRARY})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(coretcl PROPERTIES SUFFIX ".so")
endif()

install(TARGETS coretcl LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
