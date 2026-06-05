#include "lib.h"

namespace core {

Lib::Lib(std::string name) : name_(std::move(name)) {}

Cell *Lib::findCell(const std::string &name)
{
    for (auto &cell : cells_) {
        if (cell.name() == name) {
            return &cell;
        }
    }
    return nullptr;
}

const Cell *Lib::findCell(const std::string &name) const
{
    for (const auto &cell : cells_) {
        if (cell.name() == name) {
            return &cell;
        }
    }
    return nullptr;
}

Cell &Lib::getOrCreateCell(const std::string &name)
{
    if (Cell *cell = findCell(name)) {
        return *cell;
    }
    cells_.emplace_back(name);
    return cells_.back();
}

} // namespace core
