get_filename_component(ROOM_INSTALL_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(ROOM_INCLUDE_DIRS
    "${ROOM_INSTALL_PREFIX}/include/ROOM/src"
    "${ROOM_INSTALL_PREFIX}/include/ROOM/utils"
    "${ROOM_INSTALL_PREFIX}/include/ROOM/generated"
)

include("${CMAKE_CURRENT_LIST_DIR}/ROOMTargets.cmake")
