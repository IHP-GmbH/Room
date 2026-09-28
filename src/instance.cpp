#include "instance.h"

namespace room {

/*!****************************************************************************************
 * \brief Constructs a cell instance reference.
 * \param cellName     Name of the referenced child cell.
 * \param transform    Placement transform in parent coordinates.
 *****************************************************************************************/
Instance::Instance(std::string cellName, Transform transform)
    : m_cellName(std::move(cellName)), m_transform(transform) {}

} // namespace room
