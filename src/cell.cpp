#include "cell.h"

namespace cdb {

Cell::Cell(std::string name) : name_(std::move(name)) {}

CellContent *Cell::findContent(ViewType type)
{
    for (auto &content : contents_) {
        if (content.viewType() == type) {
            return &content;
        }
    }
    return nullptr;
}

const CellContent *Cell::findContent(ViewType type) const
{
    for (const auto &content : contents_) {
        if (content.viewType() == type) {
            return &content;
        }
    }
    return nullptr;
}

CellContent &Cell::getOrCreateContent(ViewType type, double dbuPerMicron)
{
    if (CellContent *content = findContent(type)) {
        return *content;
    }
    contents_.emplace_back(type, dbuPerMicron);
    return contents_.back();
}

} // namespace cdb
