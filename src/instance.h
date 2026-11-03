#pragma once

#include "types.h"

#include <string>
#include <vector>

namespace room {

/*!****************************************************************************************
 * \brief The Instance class places a reference to another cell with a transform and properties.
 *****************************************************************************************/
class Instance {
public:
    Instance(std::string cellName, Transform transform);

    const std::string &                                 cellName() const { return m_cellName; }
    void                                                setCellName(std::string name) { m_cellName = std::move(name); }
    Transform &                                         transform() { return m_transform; }
    const Transform &                                   transform() const { return m_transform; }

    std::uint32_t                                       columns() const { return m_columns; }
    std::uint32_t                                       rows() const { return m_rows; }
    std::int64_t                                        stepX() const { return m_stepX; }
    std::int64_t                                        stepY() const { return m_stepY; }
    std::int64_t                                        colY() const { return m_colY; }
    std::int64_t                                        rowX() const { return m_rowX; }
    const std::vector<Point> &                          offsets() const { return m_offsets; }
    void                                                setArray(std::uint32_t columns, std::uint32_t rows,
                                                                 std::int64_t stepX, std::int64_t stepY,
                                                                 std::int64_t colY = 0, std::int64_t rowX = 0);
    void                                                setOffsets(std::vector<Point> offsets);

    std::vector<Property> &                             properties() { return m_properties; }
    const std::vector<Property> &                       properties() const { return m_properties; }

private:
    std::string                                         m_cellName;
    Transform                                           m_transform;
    std::uint32_t                                       m_columns = 0;
    std::uint32_t                                       m_rows = 0;
    std::int64_t                                        m_stepX = 0;
    std::int64_t                                        m_stepY = 0;
    std::int64_t                                        m_colY = 0;
    std::int64_t                                        m_rowX = 0;
    std::vector<Point>                                  m_offsets;
    std::vector<Property>                               m_properties;
};

} // namespace room
