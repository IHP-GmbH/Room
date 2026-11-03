#include "instance.h"

#include <utility>

namespace room {

/*!****************************************************************************************
 * \brief Constructs a cell instance reference.
 * \param cellName     Name of the referenced child cell.
 * \param transform    Placement transform in parent coordinates.
 *****************************************************************************************/
Instance::Instance(std::string cellName, Transform transform)
    : m_cellName(std::move(cellName)), m_transform(transform) {}

void Instance::setArray(std::uint32_t columns, std::uint32_t rows, std::int64_t stepX, std::int64_t stepY,
                        std::int64_t colY, std::int64_t rowX)
{
    if (columns <= 1 && rows <= 1) {
        m_columns = 0;
        m_rows = 0;
        m_stepX = 0;
        m_stepY = 0;
        m_colY = 0;
        m_rowX = 0;
        return;
    }
    m_columns = columns == 0 ? 1 : columns;
    m_rows = rows == 0 ? 1 : rows;
    m_stepX = stepX;
    m_stepY = stepY;
    m_colY = colY;
    m_rowX = rowX;
    m_offsets.clear();
}

void Instance::setOffsets(std::vector<Point> offsets)
{
    m_columns = 0;
    m_rows = 0;
    m_stepX = 0;
    m_stepY = 0;
    m_colY = 0;
    m_rowX = 0;
    m_offsets = std::move(offsets);
}

} // namespace room
