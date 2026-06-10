#pragma once

#include <cstdint>

namespace core {

/*!****************************************************************************************
 * \brief The Point class is a 2D integer coordinate in database units.
 *****************************************************************************************/
class Point {
public:
    std::int64_t x = 0;
    std::int64_t y = 0;

    Point() = default;
    Point(std::int64_t x_, std::int64_t y_) : x(x_), y(y_) {}
};

} // namespace core
