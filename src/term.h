#pragma once

#include "types.h"

#include <cstdint>
#include <string>

namespace core {

class Term {
public:
    Term(std::string name, std::uint32_t layerId, Point position);

    const std::string &                                 name() const { return m_name; }
    std::uint32_t                                       layerId() const { return m_layerId; }
    const Point &                                       position() const { return m_position; }

private:
    std::string                                         m_name;
    std::uint32_t                                       m_layerId;
    Point                                               m_position;
};

} // namespace core
