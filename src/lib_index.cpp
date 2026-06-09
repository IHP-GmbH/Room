#include "lib_index.h"

#include "block.h"
#include "cell.h"
#include "cell_content.h"
#include "lib.h"

#include <algorithm>

namespace core {
namespace {

void addChildRef(std::unordered_map<std::string, std::vector<std::string>> &childRefs,
                 const std::string &parent,
                 const std::string &child)
{
    auto &refs = childRefs[parent];
    if (std::find(refs.begin(), refs.end(), child) == refs.end()) {
        refs.push_back(child);
    }
}

} // namespace

LibIndex LibIndex::build(const Lib &lib, ViewType view)
{
    LibIndex index;

    for (const auto &cell : lib.cells()) {
        index.referenceCount.emplace(cell.name(), 0);
        index.childRefs.emplace(cell.name(), std::vector<std::string>{});

        if (const CellContent *content = cell.findContent(view)) {
            index.cellBboxes.emplace(cell.name(), Block::computeBBox(content->block()));
        } else {
            index.cellBboxes.emplace(cell.name(), Box{});
        }
    }

    for (const auto &cell : lib.cells()) {
        const CellContent *content = cell.findContent(view);
        if (content == nullptr) {
            continue;
        }

        for (const auto &instance : content->block().instances()) {
            ++index.placementCount;
            ++index.referenceCount[instance.cellName()];
            addChildRef(index.childRefs, cell.name(), instance.cellName());
        }
    }

    for (const auto &cell : lib.cells()) {
        const auto countIt = index.referenceCount.find(cell.name());
        if (countIt != index.referenceCount.end() && countIt->second == 0) {
            index.topCells.push_back(cell.name());
        }
    }

    std::sort(index.topCells.begin(), index.topCells.end());
    for (auto &entry : index.childRefs) {
        std::sort(entry.second.begin(), entry.second.end());
    }

    return index;
}

} // namespace core
