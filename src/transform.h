#pragma once

#include "enums.h"

#include <cstdint>

namespace room {

/*!****************************************************************************************
 * \brief The Transform class describes placement: translation, orientation, and magnification.
 *****************************************************************************************/
class Transform {
public:
    std::int64_t x = 0;
    std::int64_t y = 0;
    Orient orient = Orient::R0;
    double mag = 1.0;

    Transform() = default;
    Transform(std::int64_t x_, std::int64_t y_, Orient o = Orient::R0, double m = 1.0)
        : x(x_), y(y_), orient(o), mag(m) {}
};

} // namespace room
