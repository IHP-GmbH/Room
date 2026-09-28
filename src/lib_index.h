#pragma once

#include "box.h"
#include "enums.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace room {

class Lib;

/*!****************************************************************************************
 * \brief Derived library index: hierarchy, bounding boxes, and placement statistics.
 *
 * Built from cell instances in a chosen view. Persisted in .room and rebuilt on save when
 * refreshIndex() is called.
 *****************************************************************************************/
struct LibIndex {
    std::vector<std::string> topCells;                              /*!< Cells not referenced by others. */
    std::unordered_map<std::string, std::size_t> referenceCount;  /*!< Incoming instance count per cell. */
    std::unordered_map<std::string, std::vector<std::string>> childRefs; /*!< Direct child cell names per parent. */
    std::unordered_map<std::string, Box> cellBboxes;                /*!< Axis-aligned bbox per cell in the view. */
    std::size_t placementCount = 0;                                 /*!< Total instance count in the library. */

    static LibIndex build(const Lib &lib, ViewType view = ViewType::Layout);
};

} // namespace room
