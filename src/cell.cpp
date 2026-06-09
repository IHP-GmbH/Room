#include "cell.h"

namespace core {

Cell::Cell(std::string name) : m_name(std::move(name)) {}

CellContent *Cell::findContent(ViewType type)
{
    for (auto &content : m_contents) {
        if (content.viewType() == type) {
            return &content;
        }
    }
    return nullptr;
}

const CellContent *Cell::findContent(ViewType type) const
{
    for (const auto &content : m_contents) {
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
    m_contents.emplace_back(type, dbuPerMicron);
    return m_contents.back();
}

} // namespace core
