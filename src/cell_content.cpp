#include "cell_content.h"

namespace room {

/*!****************************************************************************************
 * \brief Constructs cell content for a single view.
 * \param viewType       Kind of view (layout, schematic, etc.).
 * \param dbuPerMicron   Database units per micron for this view.
 *****************************************************************************************/
CellContent::CellContent(ViewType viewType, double dbuPerMicron)
    : m_viewType(viewType), m_dbuPerMicron(dbuPerMicron) {}

void CellContent::setOpaquePayload(std::string mimeType, std::vector<std::uint8_t> data)
{
    clearEmModel();
    m_opaqueHasValue = true;
    m_opaqueMimeType = std::move(mimeType);
    m_opaqueData = std::move(data);
}

void CellContent::clearOpaquePayload()
{
    m_opaqueHasValue = false;
    m_opaqueMimeType.clear();
    m_opaqueData.clear();
}

void CellContent::setEmModel(EmModelViewData data)
{
    clearOpaquePayload();
    m_emModelHasValue = true;
    m_emModel = std::move(data);
    m_viewType = ViewType::EmModel;
}

void CellContent::clearEmModel()
{
    m_emModelHasValue = false;
    m_emModel = EmModelViewData{};
}

} // namespace room
