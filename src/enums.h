#pragma once

#include <cstdint>
#include <string>

namespace room {

/*! \brief Cell or instance orientation (GDS-style). */
enum class Orient {
    R0, R90, R180, R270,
    MY, MX, MX90, MY90
};

/*! \brief Kind of cell view stored in CellContent. */
enum class ViewType {
    Layout,
    Schematic,
    Symbol,
    Abstract
};

/*! \brief Semantic purpose of a layer in the layer table. */
enum class LayerPurpose {
    Drawing, Pin, Label, Boundary, Blockage, Wire, Fill, Other
};

/*! \brief Electrical signal class for a net. */
enum class SigType { Signal, Power, Ground, Clock };

std::string viewTypeToString(ViewType type);
std::string layerPurposeToString(LayerPurpose purpose);
std::string sigTypeToString(SigType type);

} // namespace room
