#pragma once

#include "enums.h"

#include <cstdint>
#include <string>

namespace core {

class LayerSpec {
public:
    std::uint16_t layerNum = 0;
    std::uint16_t dataType = 0;
    std::string name;
    LayerPurpose purpose = LayerPurpose::Drawing;

    LayerSpec() = default;
    LayerSpec(std::uint16_t ln, std::uint16_t dt, std::string n, LayerPurpose p = LayerPurpose::Drawing)
        : layerNum(ln), dataType(dt), name(std::move(n)), purpose(p) {}
};

} // namespace core
