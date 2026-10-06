#include "enums.h"

namespace room {

/*!****************************************************************************************
 * \brief Converts a ViewType to a stable lowercase string.
 * \param type     View type enum value.
 * \return         String name for serialization and logging.
 *****************************************************************************************/
std::string viewTypeToString(ViewType type)
{
    switch (type) {
    case ViewType::Layout: return "layout";
    case ViewType::Schematic: return "schematic";
    case ViewType::Symbol: return "symbol";
    case ViewType::Abstract: return "abstract";
    case ViewType::EmModel: return "emmodel";
    }
    return "unknown";
}

/*!****************************************************************************************
 * \brief Converts a LayerPurpose to a stable lowercase string.
 * \param purpose  Layer purpose enum value.
 * \return         String name for serialization and logging.
 *****************************************************************************************/
std::string layerPurposeToString(LayerPurpose purpose)
{
    switch (purpose) {
    case LayerPurpose::Drawing: return "drawing";
    case LayerPurpose::Pin: return "pin";
    case LayerPurpose::Label: return "label";
    case LayerPurpose::Boundary: return "boundary";
    case LayerPurpose::Blockage: return "blockage";
    case LayerPurpose::Wire: return "wire";
    case LayerPurpose::Fill: return "fill";
    case LayerPurpose::Other: return "other";
    }
    return "unknown";
}

/*!****************************************************************************************
 * \brief Converts a SigType to a stable lowercase string.
 * \param type     Signal type enum value.
 * \return         String name for serialization and logging.
 *****************************************************************************************/
std::string sigTypeToString(SigType type)
{
    switch (type) {
    case SigType::Signal: return "signal";
    case SigType::Power: return "power";
    case SigType::Ground: return "ground";
    case SigType::Clock: return "clock";
    }
    return "unknown";
}

} // namespace room
