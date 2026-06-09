#include "lib.h"

namespace core {

Lib::Lib(std::string name) : m_name(std::move(name)) {}

Cell *Lib::findCell(const std::string &name)
{
    for (auto &cell : m_cells) {
        if (cell.name() == name) {
            return &cell;
        }
    }
    return nullptr;
}

const Cell *Lib::findCell(const std::string &name) const
{
    for (const auto &cell : m_cells) {
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
    m_cells.emplace_back(name);
    return m_cells.back();
}

void Lib::recomputeAllBBoxes(ViewType view)
{
    for (auto &cell : m_cells) {
        if (CellContent *content = cell.findContent(view)) {
            content->block().recomputeBBox();
        }
    }
}

void Lib::refreshIndex(ViewType view)
{
    m_index = LibIndex::build(*this, view);
    m_hasIndex = true;
}

void Lib::setIndex(LibIndex index)
{
    m_index = std::move(index);
    m_hasIndex = true;
}

} // namespace core
