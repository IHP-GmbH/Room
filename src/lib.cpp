#include "lib.h"

namespace room {

/*!****************************************************************************************
 * \brief Constructs a library with the given name.
 * \param name     Library name stored in the .room file.
 *****************************************************************************************/
Lib::Lib(std::string name) : m_name(std::move(name)) {}

/*!****************************************************************************************
 * \brief Finds a cell by name.
 * \param name     Cell name to look up.
 * \return         Pointer to the cell, or nullptr if not found.
 *****************************************************************************************/
Cell *Lib::findCell(const std::string &name)
{
    ensureCellIndex();
    const auto found = m_cellIndex.find(name);
    if (found == m_cellIndex.end() || found->second >= m_cells.size()) {
        return nullptr;
    }
    return &m_cells[found->second];
}

const Cell *Lib::findCell(const std::string &name) const
{
    ensureCellIndex();
    const auto found = m_cellIndex.find(name);
    if (found == m_cellIndex.end() || found->second >= m_cells.size()) {
        return nullptr;
    }
    return &m_cells[found->second];
}

Cell &Lib::getOrCreateCell(const std::string &name)
{
    if (Cell *cell = findCell(name)) {
        return *cell;
    }
    m_cellIndex.emplace(name, m_cells.size());
    m_cells.emplace_back(name);
    return m_cells.back();
}

void Lib::renameCell(Cell &cell, std::string name)
{
    if (cell.name() == name) {
        return;
    }
    ensureCellIndex();
    const auto found = m_cellIndex.find(cell.name());
    const std::size_t index = found == m_cellIndex.end() ? m_cells.size() : found->second;
    if (found != m_cellIndex.end()) {
        m_cellIndex.erase(found);
    }
    cell.setName(std::move(name));
    if (index < m_cells.size()) {
        m_cellIndex[m_cells[index].name()] = index;
    }
}

void Lib::ensureCellIndex() const
{
    if (m_cellIndex.size() == m_cells.size()) {
        return;
    }
    m_cellIndex.clear();
    for (std::size_t i = 0; i < m_cells.size(); ++i) {
        m_cellIndex.emplace(m_cells[i].name(), i);
    }
}

/*!****************************************************************************************
 * \brief Recomputes bounding boxes for all cells that have the given view.
 * \param view     View type used to select CellContent (default: layout).
 *****************************************************************************************/
void Lib::recomputeAllBBoxes(ViewType view)
{
    for (auto &cell : m_cells) {
        if (CellContent *content = cell.findContent(view)) {
            content->block().recomputeBBox();
        }
    }
}

/*!****************************************************************************************
 * \brief Rebuilds the derived library index from instance hierarchy in the given view.
 * \param view     View type used for hierarchy traversal (default: layout).
 *****************************************************************************************/
void Lib::refreshIndex(ViewType view)
{
    m_index = LibIndex::build(*this, view);
    m_hasIndex = true;
}

/*!****************************************************************************************
 * \brief Replaces the in-memory index (e.g. after loading from disk).
 * \param index    Pre-built index to store.
 *****************************************************************************************/
void Lib::setIndex(LibIndex index)
{
    m_index = std::move(index);
    m_hasIndex = true;
}

} // namespace room
