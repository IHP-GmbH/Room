#include "cell_content.h"

namespace core {

/*!****************************************************************************************
 * \brief Constructs cell content for a single view.
 * \param viewType       Kind of view (layout, schematic, etc.).
 * \param dbuPerMicron   Database units per micron for this view.
 *****************************************************************************************/
CellContent::CellContent(ViewType viewType, double dbuPerMicron)
    : m_viewType(viewType), m_dbuPerMicron(dbuPerMicron) {}

} // namespace core
