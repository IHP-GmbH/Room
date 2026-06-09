#include "term.h"

namespace core {

Term::Term(std::string name, std::uint32_t layerId, Point position)
    : m_name(std::move(name)), m_layerId(layerId), m_position(position) {}

} // namespace core
