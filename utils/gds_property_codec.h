#pragma once

#include "property.h"

#include <cstdint>
#include <optional>
#include <string>

namespace core {
namespace gds_prop {

constexpr const char kPrefix[] = "gds.prop.";

inline Property make(std::int16_t attr, const std::string &typedValue)
{
    return Property{std::string(kPrefix) + std::to_string(attr), typedValue};
}

inline bool isGdsProperty(const Property &prop)
{
    return prop.name.size() > sizeof(kPrefix) - 1 &&
           prop.name.compare(0, sizeof(kPrefix) - 1, kPrefix) == 0;
}

inline std::optional<std::int16_t> attrOf(const Property &prop)
{
    if (!isGdsProperty(prop)) {
        return std::nullopt;
    }
    try {
        return static_cast<std::int16_t>(
            std::stoi(prop.name.substr(sizeof(kPrefix) - 1)));
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace gds_prop
} // namespace core
