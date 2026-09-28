#pragma once

#include <string>

namespace room {

/*!****************************************************************************************
 * \brief The Property class is a string key/value pair attached to cells, shapes, or instances.
 *****************************************************************************************/
class Property {
public:
    std::string name;
    std::string value;

    Property() = default;
    Property(std::string n, std::string v) : name(std::move(n)), value(std::move(v)) {}
};

} // namespace room
