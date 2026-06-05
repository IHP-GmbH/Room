#pragma once

#include <string>

namespace core {

class Property {
public:
    std::string name;
    std::string value;

    Property() = default;
    Property(std::string n, std::string v) : name(std::move(n)), value(std::move(v)) {}
};

} // namespace core
