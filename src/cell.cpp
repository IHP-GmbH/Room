#include "cell.h"

namespace core {

/*!****************************************************************************************
 * \brief Constructs a cell with the given name.
 * \param name     Unique cell name within the library.
 *****************************************************************************************/
Cell::Cell(std::string name) : m_name(std::move(name)) {}

/*!****************************************************************************************
 * \brief Finds cell content for a view type.
 * \param type     View type to search for.
 * \return         Pointer to content, or nullptr if the view is absent.
 *****************************************************************************************/
CellContent *Cell::findContent(ViewType type)
{
    for (auto &content : m_contents) {
        if (content.viewType() == type) {
            return &content;
        }
    }
    return nullptr;
}

/*!****************************************************************************************
 * \brief Finds cell content for a view type (const overload).
 * \param type     View type to search for.
 * \return         Pointer to content, or nullptr if the view is absent.
 *****************************************************************************************/
const CellContent *Cell::findContent(ViewType type) const
{
    for (const auto &content : m_contents) {
        if (content.viewType() == type) {
            return &content;
        }
    }
    return nullptr;
}

/*!****************************************************************************************
 * \brief Returns existing content for a view or creates a new CellContent entry.
 * \param type           View type.
 * \param dbuPerMicron   DBU scale for a newly created view (default 1000).
 * \return               Reference to the view body.
 *****************************************************************************************/
CellContent &Cell::getOrCreateContent(ViewType type, double dbuPerMicron)
{
    if (CellContent *content = findContent(type)) {
        return *content;
    }
    m_contents.emplace_back(type, dbuPerMicron);
    return m_contents.back();
}

} // namespace core
