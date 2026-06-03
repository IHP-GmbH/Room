#pragma once

#include <cstdint>
#include <string>

namespace cdb {

enum class Orient {
    R0, R90, R180, R270,
    MY, MX, MX90, MY90
};

enum class ViewType {
    Layout,
    Schematic,
    Symbol,
    Abstract
};

enum class LayerPurpose {
    Drawing, Pin, Label, Boundary, Blockage, Wire, Fill, Other
};

enum class SigType { Signal, Power, Ground, Clock };

std::string viewTypeToString(ViewType type);
std::string layerPurposeToString(LayerPurpose purpose);
std::string sigTypeToString(SigType type);

} // namespace cdb
