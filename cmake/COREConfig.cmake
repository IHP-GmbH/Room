get_filename_component(CORE_INSTALL_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(CORE_INCLUDE_DIRS
    "${CORE_INSTALL_PREFIX}/include/CORE/src"
    "${CORE_INSTALL_PREFIX}/include/CORE/utils"
    "${CORE_INSTALL_PREFIX}/include/CORE/generated"
)

include("${CMAKE_CURRENT_LIST_DIR}/CORETargets.cmake")
