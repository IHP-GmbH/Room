#pragma once

#include "cell_content.h"
#include "types.h"

#include <string>
#include <vector>

namespace core {

/*!****************************************************************************************
 * \brief The Cell class represents a named design cell with one or more view-specific bodies.
 *
 * A cell may contain layout, schematic, symbol, and abstract views as separate CellContent
 * entries distinguished by ViewType.
 *****************************************************************************************/
class Cell {
public:
    explicit Cell(std::string name);

    const std::string &                                 name() const { return m_name; }

    std::vector<Property> &                             properties() { return m_properties; }
    const std::vector<Property> &                       properties() const { return m_properties; }

    std::vector<CellContent> &                          contents() { return m_contents; }
    const std::vector<CellContent> &                    contents() const { return m_contents; }

    CellContent *                                       findContent(ViewType type);
    const CellContent *                                 findContent(ViewType type) const;
    CellContent &                                       getOrCreateContent(ViewType type, double dbuPerMicron = 1000.0);

private:
    std::string                                         m_name;
    std::vector<Property>                               m_properties;
    std::vector<CellContent>                            m_contents;
};

} // namespace core
