#pragma once

#include <string>

namespace cdb {

class Property {
public:
    std::string name;
    std::string value;

    Property() = default;
    Property(std::string n, std::string v) : name(std::move(n)), value(std::move(v)) {}
};

} // namespace cdb
