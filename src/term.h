#pragma once

#include "types.h"

#include <cstdint>
#include <string>

namespace core {

class Term {
public:
    Term(std::string name, std::uint32_t layerId, Point position);

    const std::string &name() const { return name_; }
    std::uint32_t layerId() const { return layerId_; }
    const Point &position() const { return position_; }

private:
    std::string name_;
    std::uint32_t layerId_;
    Point position_;
};

} // namespace core
