get_filename_component(COMMONDB_INSTALL_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(COMMONDB_INCLUDE_DIRS
    "${COMMONDB_INSTALL_PREFIX}/include/CommonDB/src"
    "${COMMONDB_INSTALL_PREFIX}/include/CommonDB/utils"
    "${COMMONDB_INSTALL_PREFIX}/include/CommonDB/generated"
)

include("${CMAKE_CURRENT_LIST_DIR}/CommonDBTargets.cmake")
