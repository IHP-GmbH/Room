#pragma once

#include "box.h"
#include "enums.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

class Lib;

struct LibIndex {
    std::vector<std::string> topCells;
    std::unordered_map<std::string, std::size_t> referenceCount;
    std::unordered_map<std::string, std::vector<std::string>> childRefs;
    std::unordered_map<std::string, Box> cellBboxes;
    std::size_t placementCount = 0;

    static LibIndex build(const Lib &lib, ViewType view = ViewType::Layout);
};

} // namespace core
