#include "enums.h"

namespace cdb {

std::string viewTypeToString(ViewType type)
{
    switch (type) {
    case ViewType::Layout: return "layout";
    case ViewType::Schematic: return "schematic";
    case ViewType::Symbol: return "symbol";
    case ViewType::Abstract: return "abstract";
    }
    return "unknown";
}

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

} // namespace cdb
