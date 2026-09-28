find_package(TCL REQUIRED)

add_library(roomtcl MODULE core_tcl.cpp)
set_target_properties(roomtcl PROPERTIES PREFIX "" OUTPUT_NAME "roomtcl")

target_include_directories(roomtcl PRIVATE
    ${TCL_INCLUDE_PATH}
    "${ROOM_ROOT}/utils"
)

target_link_libraries(roomtcl PRIVATE room_utils ${TCL_LIBRARY})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(roomtcl PROPERTIES SUFFIX ".so")
endif()

install(TARGETS roomtcl LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
