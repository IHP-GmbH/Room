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
    for (auto &cell : m_cells) {
        if (cell.name() == name) {
            return &cell;
        }
    }
    return nullptr;
}

/*!****************************************************************************************
 * \brief Finds a cell by name (const overload).
 * \param name     Cell name to look up.
 * \return         Pointer to the cell, or nullptr if not found.
 *****************************************************************************************/
const Cell *Lib::findCell(const std::string &name) const
{
    for (const auto &cell : m_cells) {
        if (cell.name() == name) {
            return &cell;
        }
    }
    return nullptr;
}

/*!****************************************************************************************
 * \brief Returns an existing cell or appends a new empty cell with the given name.
 * \param name     Cell name.
 * \return         Reference to the cell.
 *****************************************************************************************/
Cell &Lib::getOrCreateCell(const std::string &name)
{
    if (Cell *cell = findCell(name)) {
        return *cell;
    }
    m_cells.emplace_back(name);
    return m_cells.back();
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
