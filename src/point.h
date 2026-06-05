#pragma once

#include <cstdint>

namespace core {

class Point {
public:
    std::int64_t x = 0;
    std::int64_t y = 0;

    Point() = default;
    Point(std::int64_t x_, std::int64_t y_) : x(x_), y(y_) {}
};

} // namespace core
